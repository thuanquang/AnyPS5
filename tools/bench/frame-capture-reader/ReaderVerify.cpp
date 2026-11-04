#include "prx/libSceAgcDriver/Execution/include/FrameCapture.hpp"
#include <zstd.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <thread>
#include <vector>

using namespace AgcDriver;

static std::uint64_t fnv(const std::byte* data, std::size_t size) {
    std::uint64_t h = 1469598103934665603ull;
    for (std::size_t i = 0; i < size; ++i) h = (h ^ static_cast<std::uint8_t>(data[i])) * 1099511628211ull;
    return h;
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::filesystem::path directory = argv[1];
    const FrameCaptureReader reader(directory);
    const auto& chunks = reader.Chunks();
    std::vector<const FrameMemoryChunk*> data;
    for (const auto& chunk : chunks) if (chunk.kind != FrameMemoryKind::Range) data.push_back(&chunk);
    std::vector<std::uint64_t> want(data.size());
    {
        std::ifstream file(directory / FrameCaptureWriter::MemoryName, std::ios::binary);
        std::vector<std::byte> in, out;
        for (std::size_t i = 0; i < data.size(); ++i) {
            in.resize(data[i]->compressedBytes);
            out.resize(data[i]->bytes);
            file.seekg(static_cast<std::streamoff>(data[i]->offset));
            file.read(reinterpret_cast<char*>(in.data()), static_cast<std::streamsize>(in.size()));
            const auto n = ZSTD_decompress(out.data(), out.size(), in.data(), in.size());
            if (ZSTD_isError(n) || n != out.size()) { std::printf("independent decode failed at %zu\n", i); return 1; }
            want[i] = fnv(out.data(), out.size());
        }
    }
    std::size_t bad = 0;
    std::vector<std::byte> buffer;
    for (std::size_t i = 0; i < data.size(); ++i) {
        buffer.resize(data[i]->bytes);
        reader.Read(*data[i], buffer);
        if (fnv(buffer.data(), buffer.size()) != want[i]) ++bad;
    }
    std::printf("{\"chunks\":%zu,\"serial_mismatch\":%zu", data.size(), bad);
    std::atomic<std::size_t> next{0}, badc{0};
    const auto threads = std::max(2u, std::thread::hardware_concurrency());
    std::vector<std::future<void>> tasks;
    for (unsigned t = 0; t < threads; ++t) {
        tasks.push_back(std::async(std::launch::async, [&] {
            std::vector<std::byte> local;
            for (std::size_t i; (i = next++) < data.size();) {
                if (i % 2 == 0) {
                    local.resize(data[i]->bytes);
                    reader.Read(*data[i], local);
                    if (fnv(local.data(), local.size()) != want[i]) ++badc;
                } else {
                    const auto v = reader.Read(*data[i]);
                    if (fnv(v.data(), v.size()) != want[i]) ++badc;
                }
            }
        }));
    }
    for (auto& t : tasks) t.get();
    std::printf(",\"threads\":%u,\"concurrent_mismatch\":%zu}\n", threads, badc.load());
    return (bad == 0 && badc == 0) ? 0 : 1;
}
