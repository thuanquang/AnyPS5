#include "prx/libSceAgcDriver/Execution/include/FrameCapture.hpp"
#include <zstd.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <span>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

using namespace AgcDriver;
using Clock = std::chrono::steady_clock;

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: reader-bench <capture directory>\n");
        return 2;
    }
    const std::filesystem::path directory = argv[1];
    const FrameCaptureReader reader(directory);
    std::map<std::uint64_t, std::uint64_t> groupBytes;
    for (const auto& chunk : reader.Chunks()) {
        if (chunk.kind == FrameMemoryKind::Delta) groupBytes[chunk.group] += chunk.bytes;
    }
    std::vector<const FrameMemoryChunk*> steady;
    std::uint64_t raw = 0, stored = 0, large = 0;
    for (const auto& chunk : reader.Chunks()) {
        if (chunk.kind != FrameMemoryKind::Delta) continue;
        if (groupBytes[chunk.group] >= (512ull << 20)) {
            large += chunk.bytes;
            continue;
        }
        steady.push_back(&chunk);
        raw += chunk.bytes;
        stored += chunk.compressedBytes;
    }
    std::size_t frames = 0;
    bool started = false;
    for (const auto& call : reader.Calls()) {
        if (call.kind == FrameCallKind::FrameStart) started = true;
        else if (started && call.kind == FrameCallKind::Suspend) ++frames;
    }
    std::vector<std::byte> warm(FrameCaptureWriter::ChunkBytes);
    const auto pass = [&](auto&& body) {
        const auto began = Clock::now();
        for (std::size_t index = 0; index < steady.size(); ++index) body(index, *steady[index]);
        return std::chrono::duration<double, std::milli>(Clock::now() - began).count();
    };
    const auto into = [&](std::size_t, const FrameMemoryChunk& chunk) { reader.Read(chunk, std::span(warm).first(static_cast<std::size_t>(chunk.bytes))); };
    const auto first = pass(into);
    const auto span = pass(into);
    const auto vector = pass([&](std::size_t, const FrameMemoryChunk& chunk) { warm[0] = reader.Read(chunk)[0]; });
    std::vector<std::byte> input(static_cast<std::size_t>(stored));
    std::vector<std::size_t> starts(steady.size());
    {
        std::ifstream file(directory / FrameCaptureWriter::MemoryName, std::ios::binary);
        std::size_t at = 0;
        for (std::size_t index = 0; index < steady.size(); ++index) {
            starts[index] = at;
            file.seekg(static_cast<std::streamoff>(steady[index]->offset));
            file.read(reinterpret_cast<char*>(input.data() + at), static_cast<std::streamsize>(steady[index]->compressedBytes));
            at += static_cast<std::size_t>(steady[index]->compressedBytes);
        }
        if (!file.good()) {
            std::fprintf(stderr, "cannot read the compressed deltas\n");
            return 1;
        }
    }
    std::size_t errors = 0;
    auto* const context = ZSTD_createDCtx();
    const auto decode = pass([&](std::size_t index, const FrameMemoryChunk& chunk) {
        if (ZSTD_decompressDCtx(context, warm.data(), static_cast<std::size_t>(chunk.bytes), input.data() + starts[index], static_cast<std::size_t>(chunk.compressedBytes)) != chunk.bytes) ++errors;
    });
    const auto fresh = pass([&](std::size_t index, const FrameMemoryChunk& chunk) {
        if (ZSTD_decompress(warm.data(), static_cast<std::size_t>(chunk.bytes), input.data() + starts[index], static_cast<std::size_t>(chunk.compressedBytes)) != chunk.bytes) ++errors;
    });
    ZSTD_freeDCtx(context);
    double plain = 0;
    double watched = 0;
#ifdef _WIN32
    constexpr std::size_t Region = 64ull << 20;
    const auto copy = [&](DWORD flags) {
        auto* const region = static_cast<std::byte*>(VirtualAlloc(nullptr, Region, MEM_RESERVE | MEM_COMMIT | flags, PAGE_READWRITE));
        std::memset(region, 1, Region);
        if (flags != 0) ResetWriteWatch(region, Region);
        std::size_t at = 0;
        const auto spent = pass([&](std::size_t, const FrameMemoryChunk& chunk) {
            const auto bytes = static_cast<std::size_t>(chunk.bytes);
            if (at + bytes > Region) {
                at = 0;
                if (flags != 0) ResetWriteWatch(region, Region);
            }
            std::memcpy(region + at, warm.data(), bytes);
            at += (bytes + 4095) & ~std::size_t{4095};
        });
        VirtualFree(region, 0, MEM_RELEASE);
        return spent;
    };
    plain = copy(0);
    watched = copy(MEM_WRITE_WATCH);
#endif
    std::printf("{\"frames\":%zu,\"steady_chunks\":%zu,\"steady_mib\":%.1f,\"steady_stored_mib\":%.1f,\"large_mib\":%.1f,\"read_first_ms\":%.1f,\"read_span_ms\":%.1f,\"read_vector_ms\":%.1f,\"decode_ms\":%.1f,\"decode_fresh_context_ms\":%.1f,\"copy_ms\":%.1f,\"copy_watched_ms\":%.1f,\"errors\":%zu}\n",
                frames, steady.size(), static_cast<double>(raw) / 1048576.0, static_cast<double>(stored) / 1048576.0, static_cast<double>(large) / 1048576.0, first, span, vector, decode, fresh, plain, watched, errors);
    return errors == 0 ? 0 : 1;
}
