#include "prx/libSceAgcDriver/Execution/include/FrameCapture.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <span>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

namespace {

using AgcDriver::FrameCall;
using AgcDriver::FrameCallKind;
using AgcDriver::FrameCaptureReader;
using AgcDriver::FrameCaptureWriter;
using AgcDriver::FrameMemoryChunk;
using AgcDriver::FrameMemoryKind;
using AgcDriver::FrameRangeKind;

void check(bool condition, const std::string& message) {
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", message.c_str());
        std::exit(1);
    }
}

template <typename TFunction>
void checkThrows(TFunction&& function, const std::string& message) {
    try {
        function();
    } catch (const std::runtime_error&) {
        return;
    }
    check(false, message);
}

template <typename TFunction>
void checkThrowsWith(TFunction&& function, const std::string& expected, const std::string& message) {
    try {
        function();
    } catch (const std::runtime_error& error) {
        check(std::string(error.what()).find(expected) != std::string::npos, message + ": it threw '" + error.what() + "', not '" + expected + "'");
        return;
    }
    check(false, message);
}

bool sameRegisters(const std::vector<ShaderRegister>& left, const std::vector<ShaderRegister>& right) {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (left[index].offset != right[index].offset || left[index].value != right[index].value) return false;
    }
    return true;
}

bool sameCall(const FrameCall& left, const FrameCall& right) {
    return left.kind == right.kind && left.ordinal == right.ordinal && left.timeNs == right.timeNs && left.previousExitNs == right.previousExitNs && left.thread == right.thread && left.accepted == right.accepted && left.completed == right.completed && left.queue == right.queue &&
           left.flags == right.flags && left.cpuFlip == right.cpuFlip && left.address == right.address && left.size == right.size && left.storeFloor == right.storeFloor && left.floorFlags == right.floorFlags && left.waitFloor == right.waitFloor && left.values == right.values &&
           sameRegisters(left.context, right.context) && sameRegisters(left.primitive, right.primitive);
}

std::vector<FrameCall> sampleCalls() {
    std::vector<FrameCall> calls;
    FrameCall shader;
    shader.kind = FrameCallKind::RegisterShader;
    shader.ordinal = 1;
    shader.address = 0x2000010000;
    calls.push_back(shader);
    FrameCall abi;
    abi.kind = FrameCallKind::ResolveGraphicsStagesAbi;
    abi.ordinal = 2;
    abi.values = {0x2000010000, 0x2000020000, 0};
    abi.context = {{0x1a0, 7}, {0x1a1, 0xffffffffu}};
    abi.primitive = {{0x242, 3}};
    calls.push_back(abi);
    FrameCall submit;
    submit.kind = FrameCallKind::Submit;
    submit.ordinal = 3;
    submit.timeNs = 123456789;
    submit.previousExitNs = 123000000;
    submit.thread = 4242;
    submit.accepted = 20;
    submit.completed = 17;
    submit.queue = 0x21;
    submit.address = 0x2000030000;
    submit.size = 4096;
    calls.push_back(submit);
    FrameCall flip;
    flip.kind = FrameCallKind::Flip;
    flip.ordinal = 4;
    flip.cpuFlip = true;
    flip.values = {1, 2, 0, static_cast<std::uint64_t>(-5)};
    calls.push_back(flip);
    FrameCall unregister;
    unregister.kind = FrameCallKind::UnregisterVideoOutput;
    unregister.ordinal = 5;
    unregister.values = {1};
    calls.push_back(unregister);
    FrameCall floored;
    floored.kind = FrameCallKind::Memory;
    floored.ordinal = 6;
    floored.accepted = 21;
    floored.completed = 19;
    floored.storeFloor = 18;
    floored.waitFloor = 17;
    floored.values = {1};
    calls.push_back(floored);
    FrameCall unknown;
    unknown.kind = FrameCallKind::Memory;
    unknown.ordinal = 7;
    unknown.accepted = 21;
    unknown.completed = 20;
    unknown.floorFlags = AgcDriver::FrameFloorFlag::UnattributedStore | AgcDriver::FrameFloorFlag::StoreDuringRead | AgcDriver::FrameFloorFlag::WaitValueLost;
    unknown.values = {2};
    calls.push_back(unknown);
    FrameCall end;
    end.kind = FrameCallKind::End;
    end.ordinal = 8;
    calls.push_back(end);
    return calls;
}

std::vector<std::byte> sampleMemory() {
    constexpr std::size_t Block = FrameCaptureWriter::BlockBytes;
    std::vector<std::byte> memory(Block * 150 + 4096);
    for (std::size_t block = 0; block < 150; ++block) {
        if (block % 3 == 1 || (block >= 10 && block < 90)) {
            for (std::size_t at = 0; at < Block; at += 97) memory[block * Block + at] = static_cast<std::byte>((block * 131 + at) & 0xff);
        }
    }
    memory[memory.size() - 1] = std::byte{0x5a};
    return memory;
}

void truncateFile(const std::filesystem::path& path, std::uintmax_t removed) {
    const auto size = std::filesystem::file_size(path);
    std::filesystem::resize_file(path, size - removed);
}

void testRoundTrip(const std::filesystem::path& directory) {
    constexpr std::uint64_t Base = 0x2000000000;
    const auto calls = sampleCalls();
    const auto memory = sampleMemory();
    std::vector<std::byte> delta(8192);
    for (std::size_t at = 0; at < delta.size(); ++at) delta[at] = static_cast<std::byte>(at * 7);
    std::uint64_t stored = 0;
    {
        FrameCaptureWriter writer(directory);
        writer.WriteRange(Base, memory.size(), FrameRangeKind::Watched, 3, AgcDriver::FrameRangeAccess::Readable | AgcDriver::FrameRangeAccess::Gpu);
        stored = writer.WriteSnapshot(Base, memory);
        for (const auto& call : calls) writer.WriteCall(call);
        writer.WriteDelta(1, 17, Base + 0x3000, delta);
        writer.Finish("{\"magic\":\"aps5-frame-capture 1\"}\n");
        check(writer.RawBytes() == stored + delta.size(), "raw byte count does not add the snapshot and the delta");
    }
    std::size_t nonzero = 0;
    for (std::size_t at = 0; at < memory.size(); at += FrameCaptureWriter::BlockBytes) {
        const auto bytes = std::min(FrameCaptureWriter::BlockBytes, memory.size() - at);
        if (!AgcDriver::AllZero(std::span(memory).subspan(at, bytes))) nonzero += bytes;
    }
    check(stored == nonzero, "the snapshot stored zero blocks or lost data blocks");

    const FrameCaptureReader reader(directory);
    check(!reader.Truncated(), "a finished capture reads as truncated");
    check(reader.Header() == "{\"magic\":\"aps5-frame-capture 1\"}\n", "header does not round-trip");
    check(reader.Calls().size() == calls.size(), "call count does not round-trip");
    for (std::size_t index = 0; index < calls.size(); ++index) check(sameCall(reader.Calls()[index], calls[index]), "call " + std::to_string(index) + " does not round-trip");

    std::vector<std::byte> restored(memory.size());
    std::size_t ranges = 0, snapshots = 0, deltas = 0;
    for (const auto& chunk : reader.Chunks()) {
        if (chunk.kind == FrameMemoryKind::Range) {
            ++ranges;
            check(chunk.address == Base && chunk.bytes == memory.size() && chunk.rangeKind == FrameRangeKind::Watched && chunk.protection == 3 &&
                      chunk.access == (AgcDriver::FrameRangeAccess::Readable | AgcDriver::FrameRangeAccess::Gpu), "range does not round-trip");
            continue;
        }
        if (chunk.kind == FrameMemoryKind::Snapshot) {
            ++snapshots;
            check(chunk.bytes <= FrameCaptureWriter::ChunkBytes, "snapshot chunk is larger than the chunk limit");
            check(chunk.address >= Base && chunk.address - Base + chunk.bytes <= restored.size(), "snapshot chunk outside its range");
            reader.Read(chunk, std::span(restored).subspan(chunk.address - Base, chunk.bytes));
            continue;
        }
        ++deltas;
        check(chunk.group == 1 && chunk.completed == 17 && chunk.address == Base + 0x3000, "delta header does not round-trip");
        check(reader.Read(chunk) == delta, "delta bytes do not round-trip");
    }
    check(ranges == 1 && deltas == 1 && snapshots >= 2, "unexpected chunk counts");
    check(restored == memory, "snapshot does not restore the memory");
    std::vector<std::byte> wrong(1);
    checkThrows([&] { reader.Read(reader.Chunks()[1], wrong); }, "a read into a wrongly sized destination does not throw");
}

void testTruncated(const std::filesystem::path& source, const std::filesystem::path& directory) {
    std::filesystem::remove_all(directory);
    std::filesystem::copy(source, directory, std::filesystem::copy_options::recursive);
    truncateFile(directory / FrameCaptureWriter::CallsName, 3);
    truncateFile(directory / FrameCaptureWriter::MemoryName, 10);
    std::filesystem::remove(directory / FrameCaptureWriter::HeaderName);
    const FrameCaptureReader reader(directory);
    const auto calls = sampleCalls();
    check(reader.Truncated(), "a cut capture does not read as truncated");
    check(reader.Calls().size() == calls.size() - 1, "a cut call record is not dropped");
    check(reader.Chunks().back().kind != FrameMemoryKind::Delta, "a cut memory chunk is not dropped");
}

void testCorrupt(const std::filesystem::path& source, const std::filesystem::path& directory) {
    std::filesystem::remove_all(directory);
    std::filesystem::copy(source, directory, std::filesystem::copy_options::recursive);
    {
        std::fstream file(directory / FrameCaptureWriter::CallsName, std::ios::binary | std::ios::in | std::ios::out);
        file.seekp(static_cast<std::streamoff>(FrameCaptureWriter::CallsMagic.size()));
        const std::uint32_t kind = 99;
        file.write(reinterpret_cast<const char*>(&kind), sizeof(kind));
    }
    checkThrows([&] { FrameCaptureReader reader(directory); }, "an unknown call kind does not throw");
    std::ofstream(directory / FrameCaptureWriter::CallsName, std::ios::binary | std::ios::trunc) << "not a capture";
    checkThrows([&] { FrameCaptureReader reader(directory); }, "a file without the magic does not throw");
}

void testWriter(const std::filesystem::path& directory) {
    FrameCaptureWriter writer(directory);
    writer.Finish("{}");
    FrameCall call;
    call.kind = FrameCallKind::Suspend;
    checkThrows([&] { writer.WriteCall(call); }, "a call after Finish does not throw");
    checkThrows([&] { writer.Finish("{}"); }, "a second Finish does not throw");
    FrameCall unknown;
    unknown.kind = static_cast<FrameCallKind>(77);
    FrameCaptureWriter other(directory / "other");
    checkThrows([&] { other.WriteCall(unknown); }, "an unknown call kind is written");
}

std::vector<FrameCall> callsOfFormat(std::uint32_t version) {
    auto calls = sampleCalls();
    for (auto& call : calls) {
        if (version < AgcDriver::ExitCallsVersion) call.previousExitNs = 0;
        if (version < AgcDriver::StoreFloorCallsVersion) {
            call.storeFloor = 0;
            call.floorFlags = 0;
        }
        if (version < AgcDriver::WaitFloorCallsVersion) {
            call.waitFloor = 0;
            call.floorFlags &= ~AgcDriver::FrameFloorFlag::WaitUnknown;
        }
    }
    return calls;
}

void writeCalls(const std::filesystem::path& directory, const std::vector<FrameCall>& calls, std::uint32_t version) {
    FrameCaptureWriter writer(directory, 1, version);
    for (const auto& call : calls) writer.WriteCall(call);
    writer.Finish("{}");
}

void testCallsVersions(const std::filesystem::path& directory) {
    for (std::uint32_t version = 1; version <= AgcDriver::CallsVersion; ++version) {
        const auto path = directory / ("format " + std::to_string(version));
        const auto calls = callsOfFormat(version);
        writeCalls(path, calls, version);
        const FrameCaptureReader reader(path);
        const auto name = "calls format " + std::to_string(version);
        check(reader.Version() == version, name + " reads back as calls format " + std::to_string(reader.Version()));
        check(reader.Calls().size() == calls.size(), "the call count of " + name + " does not round-trip");
        for (std::size_t index = 0; index < calls.size(); ++index) check(sameCall(reader.Calls()[index], calls[index]), "call " + std::to_string(index) + " of " + name + " does not round-trip");
    }
    const auto newer = directory / "newer";
    writeCalls(newer, sampleCalls(), AgcDriver::CallsVersion);
    {
        std::fstream file(newer / FrameCaptureWriter::CallsName, std::ios::binary | std::ios::in | std::ios::out);
        file.seekp(static_cast<std::streamoff>(FrameCaptureWriter::CallsMagicPrefix.size()));
        file.put(static_cast<char>('0' + AgcDriver::CallsVersion + 1));
        check(file.good(), "cannot patch the calls format of a capture");
    }
    const auto next = std::to_string(AgcDriver::CallsVersion + 1);
    checkThrowsWith([&] { FrameCaptureReader reader(newer); }, "calls format " + next + " is newer than this build reads (" + std::to_string(AgcDriver::CallsVersion) + ")", "a calls format newer than this build was read");
    checkThrowsWith([&] { FrameCaptureWriter writer(directory / "zero", 1, 0); }, "cannot write calls format 0", "calls format 0 was written");
    checkThrowsWith([&] { FrameCaptureWriter writer(directory / "next", 1, AgcDriver::CallsVersion + 1); }, "cannot write calls format " + next, "a calls format newer than this build was written");
    FrameCaptureWriter previous(directory / "previous", 1, AgcDriver::InFlightCallsVersion);
    checkThrowsWith([&] { previous.WriteCall(sampleCalls()[2]); }, "calls format " + std::to_string(AgcDriver::InFlightCallsVersion) + " does not record the previous exit time", "a previous exit time was written in a calls format without one");
    FrameCaptureWriter exits(directory / "exits", 1, AgcDriver::ExitCallsVersion);
    checkThrowsWith([&] { exits.WriteCall(sampleCalls()[5]); }, "calls format " + std::to_string(AgcDriver::ExitCallsVersion) + " does not record store floors", "a store floor was written in a calls format without one");
    checkThrowsWith([&] { exits.WriteCall(sampleCalls()[6]); }, "calls format " + std::to_string(AgcDriver::ExitCallsVersion) + " does not record store floors", "floor flags were written in a calls format without them");
    FrameCaptureWriter stores(directory / "stores", 1, AgcDriver::StoreFloorCallsVersion);
    checkThrowsWith([&] { stores.WriteCall(sampleCalls()[5]); }, "calls format " + std::to_string(AgcDriver::StoreFloorCallsVersion) + " does not record wait floors", "a wait floor was written in a calls format without one");
}

void refusedRecord(const std::filesystem::path& directory, const std::string& name, const FrameCall& call, const std::string& expected, std::uint32_t version = AgcDriver::CallsVersion) {
    const auto path = directory / name;
    {
        FrameCaptureWriter writer(path, 1, version);
        writer.WriteCall(call);
        writer.Finish("{}");
    }
    checkThrowsWith([&] { FrameCaptureReader reader(path); }, expected, "a record that breaks " + name + " was accepted");
}

void testCallsInvariants(const std::filesystem::path& directory) {
    FrameCall early;
    early.kind = FrameCallKind::Submit;
    early.ordinal = 1;
    early.timeNs = 100;
    early.previousExitNs = 200;
    refusedRecord(directory, "X1", early, "call 1 (submit) was entered at 100 ns, before its thread's previous call returned at 200 ns");
    FrameCall delta;
    delta.kind = FrameCallKind::Memory;
    delta.ordinal = 2;
    delta.timeNs = 300;
    delta.previousExitNs = 100;
    delta.values = {1};
    refusedRecord(directory, "X2", delta, "call 2 (memory) is not a title call but carries a previous exit time");
    FrameCall floored;
    floored.kind = FrameCallKind::Submit;
    floored.ordinal = 3;
    floored.completed = 10;
    floored.storeFloor = 4;
    refusedRecord(directory, "F1", floored, "call 3 (submit) is not a memory delta but carries a floor");
    const auto memory = [](std::uint64_t ordinal) {
        FrameCall call;
        call.kind = FrameCallKind::Memory;
        call.ordinal = ordinal;
        call.accepted = 12;
        call.completed = 10;
        call.values = {ordinal};
        return call;
    };
    auto flagged = memory(4);
    flagged.floorFlags = 1u << 7;
    refusedRecord(directory, "F2", flagged, "memory delta 4 carries unknown floor flags 0x80");
    auto named = memory(5);
    named.storeFloor = 5;
    named.floorFlags = AgcDriver::FrameFloorFlag::UnattributedStore;
    refusedRecord(directory, "F3", named, "memory delta 5 marks its store floor unknown but names submission 5");
    auto ahead = memory(6);
    ahead.storeFloor = 11;
    refusedRecord(directory, "F4", ahead, "memory delta 6 names store floor 11 above its completed serial 10");
    auto inFlight = memory(7);
    inFlight.flags = AgcDriver::InFlightDeltaFlag;
    inFlight.storeFloor = 3;
    refusedRecord(directory, "F5", inFlight, "memory delta 7 was read in flight but carries a floor");
    inFlight.storeFloor = 0;
    inFlight.floorFlags = AgcDriver::FrameFloorFlag::IncompleteSerialStore;
    refusedRecord(directory, "F5 flags", inFlight, "memory delta 7 was read in flight but carries a floor");
    inFlight.floorFlags = 0;
    inFlight.waitFloor = 3;
    refusedRecord(directory, "F5 wait", inFlight, "memory delta 7 was read in flight but carries a floor");
    inFlight.waitFloor = 0;
    inFlight.floorFlags = AgcDriver::FrameFloorFlag::IncompleteSerialWait;
    refusedRecord(directory, "F5 wait flags", inFlight, "memory delta 7 was read in flight but carries a floor");
    auto waitNamed = memory(8);
    waitNamed.waitFloor = 6;
    waitNamed.floorFlags = AgcDriver::FrameFloorFlag::WaitValueLost;
    refusedRecord(directory, "W1", waitNamed, "memory delta 8 marks its wait floor unknown but names submission 6");
    auto waitAhead = memory(9);
    waitAhead.waitFloor = 11;
    refusedRecord(directory, "W2", waitAhead, "memory delta 9 names wait floor 11 above its completed serial 10");
    auto earlier = memory(10);
    earlier.floorFlags = AgcDriver::FrameFloorFlag::WaitValueLost;
    refusedRecord(directory, "F2 store format", earlier, "memory delta 10 carries unknown floor flags 0x200", AgcDriver::StoreFloorCallsVersion);
    auto refusedInFlight = memory(11);
    refusedInFlight.flags = AgcDriver::InFlightDeltaFlag;
    refusedInFlight.floorFlags = AgcDriver::FrameFloorFlag::WaitValueLost | AgcDriver::FrameFloorFlag::WaitOrderLost;
    const auto path = directory / "in flight refusals";
    {
        FrameCaptureWriter writer(path);
        writer.WriteCall(refusedInFlight);
        writer.Finish("{}");
    }
    const FrameCaptureReader reader(path);
    check(reader.Calls().size() == 1 && sameCall(reader.Calls()[0], refusedInFlight), "a delta read in flight with refused waits does not round-trip");
}

std::vector<std::byte> guest(64);
std::vector<std::pair<std::uint64_t, std::uint64_t>> reads;
constexpr std::uint64_t GuestBase = 0x10000;

void readFake(std::uint64_t address, std::span<std::byte> destination) {
    reads.emplace_back(address, destination.size());
    std::memcpy(destination.data(), guest.data() + (address - GuestBase), destination.size());
}

void checkReadAround(std::vector<std::pair<std::uint64_t, std::uint64_t>> skipped, std::uint64_t zeroBegin, std::uint64_t zeroEnd, const std::string& what) {
    reads.clear();
    std::vector<std::byte> bytes(48, std::byte{0xcc});
    const auto zeroed = AgcDriver::ReadAroundRanges(GuestBase + 8, bytes, skipped, readFake);
    check(zeroed == zeroEnd - zeroBegin, what + ": the zeroed byte count is wrong");
    for (std::size_t at = 0; at < bytes.size(); ++at) {
        const auto address = GuestBase + 8 + at;
        const bool zero = address >= GuestBase + zeroBegin && address < GuestBase + zeroEnd;
        check(bytes[at] == (zero ? std::byte{0} : guest[address - GuestBase]), what + ": byte " + std::to_string(at) + " is wrong");
    }
    for (const auto& [address, size] : reads) {
        for (const auto& [begin, length] : skipped) check(size == 0 || address + size <= begin || address >= begin + length, what + ": a skipped range was read");
    }
}

void testReadAroundRanges() {
    for (std::size_t at = 0; at < guest.size(); ++at) guest[at] = static_cast<std::byte>(at + 1);
    checkReadAround({{GuestBase + 20, 8}}, 20, 28, "a range in the middle");
    checkReadAround({{GuestBase + 4, 10}}, 8, 14, "a range over the start");
    checkReadAround({{GuestBase + 50, 20}}, 50, 56, "a range over the end");
    checkReadAround({{GuestBase + 16, 8}, {GuestBase + 20, 8}}, 16, 28, "overlapping ranges");
    checkReadAround({{GuestBase, 4}, {GuestBase + 60, 4}}, 0, 0, "ranges outside the block");
    checkReadAround({{GuestBase, 64}}, 8, 56, "a range over the whole block");
}

void testAllZero() {
    std::vector<std::byte> bytes(37);
    check(AgcDriver::AllZero(bytes), "zero bytes are not all zero");
    check(AgcDriver::AllZero({}), "an empty span is not all zero");
    bytes[36] = std::byte{1};
    check(!AgcDriver::AllZero(bytes), "a nonzero tail byte is missed");
    bytes[36] = std::byte{0};
    bytes[3] = std::byte{1};
    check(!AgcDriver::AllZero(bytes), "a nonzero word is missed");
}

}

int main() {
    const auto root = std::filesystem::temp_directory_path() / ("aps5-frame-capture-test-" + std::to_string(getpid()));
    std::filesystem::remove_all(root);
    try {
        testAllZero();
        testReadAroundRanges();
        testRoundTrip(root / "capture");
        testTruncated(root / "capture", root / "truncated");
        testCorrupt(root / "capture", root / "corrupt");
        testWriter(root / "writer");
        testCallsVersions(root / "versions");
        testCallsInvariants(root / "invariants");
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return 1;
    }
    std::filesystem::remove_all(root);
    std::puts("frame capture tests passed");
    return 0;
}
