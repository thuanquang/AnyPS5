#define main frameReplayToolMain
#include "../tools/FrameReplay.cpp"
#undef main
#include "prx/libSceAgcDriver/Execution/include/FrameRecorder.hpp"
#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "prx/libSceAgcDriver/Execution/include/Presentation.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Recorder.hpp"
#include "prx/libc/include/GuestWriteWatch.hpp"
#include <SDL_loadso.h>
#include <string_view>
#include <atomic>
#include <barrier>
#include <functional>
#include <new>
#include <optional>

namespace {

using AgcDriver::FrameCaptureWriter;

constexpr std::uint32_t ReadWrite = 0x04;

void check(bool condition, const char* reason) {
    if (!condition) throw std::runtime_error(reason);
}

struct Inconclusive : std::runtime_error {
    using std::runtime_error::runtime_error;
};

void checkRanToEnd(const FrameCaptureReader& reader) {
    const auto& header = reader.Header();
    check(!header.empty(), "the capture has no capture.json, so the recorder never finished it");
    const auto warnings = header.find("\"warnings\":[");
    check(warnings != std::string::npos, ("the capture header has no warnings list: " + header).c_str());
    check(header.find("[\"stopped:", warnings) == std::string::npos && header.find(",\"stopped:", warnings) == std::string::npos, ("the capture stopped: " + header).c_str());
}

std::uint64_t address(const void* pointer) {
    return reinterpret_cast<std::uintptr_t>(pointer);
}

std::byte* allocate(std::size_t size, bool gpu = true) {
    auto* bytes = static_cast<std::byte*>(GuestArena::GuestArenaAllocate_nid_postfix(size, 65536));
    check(bytes != nullptr, "guest allocation failed");
    GuestArena::GuestArenaCommit_nid_postfix(bytes, size, 0x04, size);
#ifdef __linux__
    GuestWriteWatch::GuestWriteWatchRegister_nid_postfix(bytes, size);
#endif
    GuestAllocations::Mutation mutation;
    mutation.Add(bytes, size, true, true, gpu);
    return bytes;
}

void collectGroups(Replay& replay) {
    for (const auto& chunk : replay.reader.Chunks()) {
        if (chunk.kind == FrameMemoryKind::Delta) replay.groups[chunk.group].push_back(&chunk);
    }
}

FrameCall memoryCall(std::uint64_t group, std::uint64_t thread) {
    FrameCall call;
    call.kind = FrameCallKind::Memory;
    call.values = {group};
    call.thread = thread;
    return call;
}

void testMemoryOrder(const std::filesystem::path& directory) {
    constexpr std::size_t Size = 64u * 1024u * 1024u;
    auto* bytes = allocate(Size);
    const std::vector<std::byte> first(Size, std::byte{0x11});
    const std::vector<std::byte> second(4096, std::byte{0x22});
    FrameCaptureWriter writer(directory);
    writer.WriteDelta(1, 0, address(bytes), first);
    writer.WriteDelta(2, 0, address(bytes + Size - second.size()), second);
    writer.Finish("{}");
    const FrameCaptureReader reader(directory);
    Options options;
    options.release = false;
    Replay replay{options, reader};
    replay.recording = true;
    collectGroups(replay);
    const std::array calls{memoryCall(1, 1), memoryCall(2, 2)};
    replay.runWindow(calls);
    check(std::all_of(bytes, bytes + Size - second.size(), [](std::byte value) { return value == std::byte{0x11}; }), "first delta was not restored");
    check(std::equal(second.begin(), second.end(), bytes + Size - second.size()), "an earlier delta overwrote the later thread's delta");
}

void testReusedMemory(const std::filesystem::path& directory) {
#if defined(_WIN32) || defined(__linux__)
    constexpr std::size_t Size = 2 * FrameCaptureWriter::BlockBytes;
    auto* bytes = allocate(Size);
    auto* cpu = allocate(FrameCaptureWriter::BlockBytes, false);
    std::fill_n(bytes, Size, std::byte{0x7f});
    std::fill_n(cpu, FrameCaptureWriter::BlockBytes, std::byte{0x55});
    std::vector<std::byte> expected(Size);
    std::fill(expected.begin() + FrameCaptureWriter::BlockBytes, expected.end(), std::byte{0x22});
    std::uintptr_t base = 0;
    std::size_t arenaSize = 0;
    GuestArena::GuestArenaRange_nid_postfix(&base, &arenaSize);
    FrameCaptureWriter writer(directory);
    writer.WriteRange(address(bytes), Size, FrameRangeKind::Watched, ReadWrite, FrameRangeAccess::Readable | FrameRangeAccess::Writable | FrameRangeAccess::Gpu);
    writer.WriteRange(address(cpu), FrameCaptureWriter::BlockBytes, FrameRangeKind::Unwatched, ReadWrite, FrameRangeAccess::Readable | FrameRangeAccess::Writable);
    check(writer.WriteSnapshot(address(bytes), expected) == FrameCaptureWriter::BlockBytes, "zero snapshot block was not elided");
    writer.Finish("{\"arena_base\":" + std::to_string(base) + "}");
    const FrameCaptureReader reader(directory);
    const auto skipped = restoreMemory(reader);
    check(std::equal(expected.begin(), expected.end(), bytes), "reused GPU memory did not restore zero and nonzero snapshot blocks");
    check(skipped.bytes == FrameCaptureWriter::BlockBytes, "unexpected skipped memory");
    check(std::all_of(cpu, cpu + FrameCaptureWriter::BlockBytes, [](std::byte value) { return value == std::byte{0x55}; }), "reused CPU memory was overwritten");
#endif
}

void testSharedPage(const std::filesystem::path& directory) {
#ifdef __linux__
    constexpr std::size_t Page = 65536;
    auto* page = static_cast<std::byte*>(GuestArena::GuestArenaAllocate_nid_postfix(Page, Page));
    check(page != nullptr, "guest reservation failed");
    GuestArena::GuestArenaRelease_nid_postfix(page, Page);
    const std::vector<std::byte> gpu(0x20, std::byte{0x3c});
    const std::vector<std::byte> cpu(0x40, std::byte{0x5a});
    std::uintptr_t base = 0;
    std::size_t arenaSize = 0;
    GuestArena::GuestArenaRange_nid_postfix(&base, &arenaSize);
    FrameCaptureWriter writer(directory);
    writer.WriteRange(address(page + 0x20), gpu.size(), FrameRangeKind::Watched, ReadWrite, FrameRangeAccess::Readable | FrameRangeAccess::Writable | FrameRangeAccess::Gpu);
    writer.WriteRange(address(page + 0x40), cpu.size(), FrameRangeKind::Unwatched, ReadWrite, FrameRangeAccess::Readable | FrameRangeAccess::Writable);
    writer.WriteSnapshot(address(page + 0x20), gpu);
    writer.WriteSnapshot(address(page + 0x40), cpu);
    writer.Finish("{\"arena_base\":" + std::to_string(base) + "}");
    const FrameCaptureReader reader(directory);
    const auto skipped = restoreMemory(reader);
    check(skipped.bytes == 0, "a range sharing a page with an earlier one was skipped");
    check(std::equal(gpu.begin(), gpu.end(), page + 0x20) && std::equal(cpu.begin(), cpu.end(), page + 0x40), "ranges sharing a page were not restored");
#else
    static_cast<void>(directory);
#endif
}

void testHostNeighbors(const std::filesystem::path& directory) {
#ifdef __linux__
    const auto page = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
    void* mapped = mmap(nullptr, 2 * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    check(mapped != MAP_FAILED, "host allocation failed");
    auto* occupied = static_cast<std::byte*>(mapped);
    auto* available = occupied + page;
    check(munmap(available, page) == 0, "host page release failed");
    std::uintptr_t base = 0;
    std::size_t arenaSize = 0;
    GuestArena::GuestArenaRange_nid_postfix(&base, &arenaSize);
    FrameCaptureWriter writer(directory);
    writer.WriteRange(address(occupied), page, FrameRangeKind::Host, ReadWrite, FrameRangeAccess::Readable | FrameRangeAccess::Writable);
    writer.WriteRange(address(available), page, FrameRangeKind::Host, ReadWrite, FrameRangeAccess::Readable | FrameRangeAccess::Writable | FrameRangeAccess::Gpu);
    writer.Finish("{\"arena_base\":" + std::to_string(base) + "}");
    const FrameCaptureReader reader(directory);
    const auto skipped = restoreMemory(reader);
    check(skipped.bytes == page, "an occupied CPU host range was not the only range skipped");
#else
    static_cast<void>(directory);
#endif
}

void testUnwatched() {
    auto* recorder = AgcDriver::FrameRecorder::Active();
    check(recorder != nullptr, "frame recorder is disabled");
    try {
        recorder->FrameBoundary();
    } catch (const std::runtime_error& error) {
        check(std::string_view(error.what()).find("write watch is unavailable") != std::string_view::npos, "a capture without the write watch failed for another reason");
        return;
    }
    check(false, "a capture started without the write watch");
}

void testEopTokens() {
    Frames frames;
    auto output = std::make_shared<ReplayOutput>(frames);
    AgcDriverRegisterVideoOutput_nid_postfix(7, output);
    std::barrier together(2);
    std::atomic<int> wrong{0};
    const auto worker = [&](int lane) {
        for (int round = 0; round < 2000; ++round) {
            const auto requested = AgcDriver::EopFlipTokenBase + round * 8 + lane * 2;
            together.arrive_and_wait();
            try {
                if (AgcDriverReserveEopFlipAt_nid_postfix({7, -1, 1, 0}, requested) != requested) ++wrong;
            } catch (const std::exception&) { ++wrong; }
        }
    };
    std::thread first(worker, 0), second(worker, 1);
    first.join();
    second.join();
    check(wrong.load() == 0, "concurrent explicit EOP reservations returned the wrong token");
    const auto high = AgcDriver::EopFlipTokenBase + 20000;
    check(AgcDriverReserveEopFlipAt_nid_postfix({7, -1, 1, 0}, high) == high, "explicit high token was not reserved");
    check(AgcDriverReserveEopFlipAt_nid_postfix({7, -1, 1, 0}, high - 1) == high - 1, "explicit lower token was not reserved");
    check(AgcDriverReserveEopFlip_nid_postfix({7, -1, 1, 0}) == high + 1, "ordinary reservation reused an explicit token");
    bool rejected = false;
    try { AgcDriverReserveEopFlipAt_nid_postfix({7, -1, 1, 0}, high); }
    catch (const std::runtime_error&) { rejected = true; }
    check(rejected, "duplicate explicit token was not rejected");
    AgcDriverUnregisterVideoOutput_nid_postfix(7, output);
}

class ReservationOutput final : public AgcDriver::IVideoOutput {
public:
    explicit ReservationOutput(Frames& frames) : frames(frames) {}
    std::shared_ptr<AgcDriver::IFlipRequest> Reserve(const AgcDriver::FlipInfo&) override {
        ++reservations;
        if (failure) std::rethrow_exception(failure);
        return available ? std::make_shared<ReplayFlip>(frames) : nullptr;
    }
    void Fail(std::exception_ptr error) noexcept override { frames.Failed(error); }
    std::exception_ptr failure;
    bool available = true;
    std::size_t reservations = 0;

private:
    Frames& frames;
};

FrameCall reservationCall(std::uint32_t token) {
    FrameCall call;
    call.kind = FrameCallKind::ReserveEopFlip;
    call.values = {7, static_cast<std::uint64_t>(-1), 1, 99, token};
    return call;
}

template <typename TException>
void expectReservationFailure(Replay& replay, const FrameCall& call) {
    const auto kept = replay.keptReservations.load();
    bool failed = false;
    try { replay.runWindow(std::span(&call, 1)); }
    catch (const TException&) { failed = true; }
    check(failed, "the replay accepted a failed EOP reservation");
    check(replay.keptReservations.load() == kept, "a failed EOP reservation was counted as kept");
}

void testRepeatedReservations(const std::filesystem::path& directory, bool driverFailure) {
    FrameCaptureWriter writer(directory);
    writer.Finish("{}");
    const FrameCaptureReader reader(directory);
    Options options;
    options.pace = false;
    options.release = false;
    Replay replay{options, reader};
    auto output = std::make_shared<ReservationOutput>(replay.frames);
    AgcDriverRegisterVideoOutput_nid_postfix(7, output);
    AgcDriverRegisterVideoOutput_nid_postfix(8, output);
    const auto call = reservationCall(AgcDriver::EopFlipTokenBase);
    replay.execute(call);
    expectReservationFailure<AgcDriver::EopFlipAlreadyReserved>(replay, call);
    replay.pass = 1;
    if (driverFailure) {
        AgcDriverReportFailure_nid_postfix(std::make_exception_ptr(std::runtime_error("reservation driver failure")));
        expectReservationFailure<std::runtime_error>(replay, call);
        bool failed = false;
        try { AgcDriverShutdown_nid_postfix(); }
        catch (const std::runtime_error& error) { failed = std::string_view(error.what()) == "reservation driver failure"; }
        check(failed, "the injected driver failure was not propagated at shutdown");
        return;
    }
    replay.runWindow(std::span(&call, 1));
    check(replay.keptReservations.load() == 1 && output->reservations == 1, "a compatible pending reservation was not kept without allocating again");
    for (std::size_t field = 0; field < 4; ++field) {
        auto different = call;
        ++different.values[field];
        expectReservationFailure<std::runtime_error>(replay, different);
    }
    auto missing = call;
    missing.values[0] = 9;
    expectReservationFailure<std::runtime_error>(replay, missing);
    expectReservationFailure<std::runtime_error>(replay, reservationCall(AgcDriver::EopFlipTokenBase - 1));
    expectReservationFailure<std::runtime_error>(replay, reservationCall(AgcDriver::EopFlipTokenLimit + 1));
    const auto fresh = reservationCall(AgcDriver::EopFlipTokenBase + 1);
    output->failure = std::make_exception_ptr(std::bad_alloc{});
    expectReservationFailure<std::bad_alloc>(replay, fresh);
    output->failure = std::make_exception_ptr(std::runtime_error("reservation output failure"));
    expectReservationFailure<std::runtime_error>(replay, fresh);
    output->failure = nullptr;
    output->available = false;
    expectReservationFailure<std::runtime_error>(replay, fresh);
    output->available = true;
    AgcDriverUnregisterVideoOutput_nid_postfix(7, output);
    auto replacement = std::make_shared<ReservationOutput>(replay.frames);
    AgcDriverRegisterVideoOutput_nid_postfix(7, replacement);
    replay.runWindow(std::span(&call, 1));
    check(replay.keptReservations.load() == 1 && replacement->reservations == 1, "a closed output's token was kept after reopening");
    AgcDriverUnregisterVideoOutput_nid_postfix(7, replacement);
    AgcDriverUnregisterVideoOutput_nid_postfix(8, output);
}

class WaitingOutput final : public AgcDriver::IVideoOutput {
public:
    WaitingOutput(Frames& frames, const std::uint32_t* label) : frames(frames), label(label) {}
    std::shared_ptr<AgcDriver::IFlipRequest> Reserve(const AgcDriver::FlipInfo&) override {
        const auto deadline = Clock::now() + std::chrono::seconds(2);
        while (*static_cast<const volatile std::uint32_t*>(label) == 0) {
            check(Clock::now() < deadline, "a blocking driver call prevented the later memory update");
            std::this_thread::yield();
        }
        return std::make_shared<ReplayFlip>(frames);
    }
    void Fail(std::exception_ptr error) noexcept override { frames.Failed(error); }

private:
    Frames& frames;
    const std::uint32_t* label;
};

void testBlockingSubmission(const std::filesystem::path& directory) {
    auto* bytes = allocate(FrameCaptureWriter::BlockBytes);
    auto* label = reinterpret_cast<std::uint32_t*>(bytes);
    const std::uint32_t ready = 1;
    FrameCaptureWriter writer(directory);
    writer.WriteDelta(1, 0, address(label), std::as_bytes(std::span(&ready, 1)));
    writer.Finish("{}");
    const FrameCaptureReader reader(directory);
    Options options;
    options.pace = false;
    options.release = false;
    Replay replay{options, reader};
    collectGroups(replay);
    auto output = std::make_shared<WaitingOutput>(replay.frames, label);
    AgcDriverRegisterVideoOutput_nid_postfix(7, output);
    std::array<std::uint32_t, AgcDriver::FlipPacketWords> words{AgcDriver::FlipPacketHeader, 7, 0xffffffffu, 1, 0, 0};
    for (const auto kind : {FrameCallKind::Submit, FrameCallKind::Flip, FrameCallKind::ReserveEopFlip}) {
        *label = 0;
        FrameCall submit;
        submit.kind = kind;
        submit.thread = 1;
        submit.address = address(words.data());
        submit.size = words.size();
        submit.values = {7, static_cast<std::uint64_t>(-1), 1, 0, AgcDriver::EopFlipTokenBase};
        const std::array calls{submit, memoryCall(1, 2)};
        replay.runWindow(calls);
        check(*label == ready && replay.unfinished == 0, "blocking submission did not complete");
    }
    check(replay.frames.Times().size() == 2, "blocking submissions lost a flip");
    AgcDriverUnregisterVideoOutput_nid_postfix(7, output);
}

void testGateSettled(const std::filesystem::path& directory) {
    auto* bytes = allocate(FrameCaptureWriter::BlockBytes);
    auto* label = reinterpret_cast<std::uint32_t*>(bytes);
    auto* commands = reinterpret_cast<std::uint32_t*>(bytes + 4096);
    auto* restored = reinterpret_cast<std::uint32_t*>(bytes + 8192);
    *label = 0;
    *restored = 0;
    const std::array<std::uint32_t, 7> wait{0xc0053c00u, 0x13u, static_cast<std::uint32_t>(address(label)), static_cast<std::uint32_t>(address(label) >> 32u), 1u, 0xffffffffu, 4u};
    std::copy(wait.begin(), wait.end(), commands);
    const std::uint32_t stored = 7;
    FrameCaptureWriter writer(directory);
    writer.WriteDelta(1, 0, address(restored), std::as_bytes(std::span(&stored, 1)));
    writer.Finish("{}");
    const FrameCaptureReader reader(directory);
    Options options;
    options.release = false;
    options.paceTimeout = std::chrono::milliseconds(300);
    options.busyTimeout = std::chrono::milliseconds(20000);
    Replay replay{options, reader};
    replay.recording = true;
    collectGroups(replay);
    AgcDriverSubmitGraphics_nid_postfix(commands, static_cast<std::uint32_t>(wait.size()));
    const auto parked = [&] {
        std::array<AgcDriver::AwaitedWait, 64> awaited{};
        const auto count = AgcDriverAwaitedWaits_nid_postfix(awaited.data(), awaited.size());
        return std::any_of(awaited.begin(), awaited.begin() + static_cast<std::ptrdiff_t>(count), [&](const AgcDriver::AwaitedWait& slot) { return slot.address == address(label); });
    };
    const auto deadline = Clock::now() + std::chrono::seconds(10);
    while (!parked()) {
        check(Clock::now() < deadline, "the submission did not wait on its unwritten label");
        std::this_thread::yield();
    }
    const auto [accepted, completed] = progress();
    check(accepted == completed + 1, "the parked submission was not the only one in flight");
    auto call = memoryCall(1, 1);
    call.ordinal = 1;
    call.flags = AgcDriver::InFlightDeltaFlag;
    call.accepted = accepted;
    call.completed = completed;
    call.values = {1, accepted};
    std::string failure;
    try {
        replay.runWindow(std::span(&call, 1));
    } catch (const std::runtime_error& error) {
        failure = error.what();
    }
    const auto delta = std::atomic_ref<std::uint32_t>(*restored).load(std::memory_order_acquire);
    std::atomic_ref<std::uint32_t>(*label).store(1, std::memory_order_release);
    AgcDriverWaitIdle_nid_postfix();
    char blocked[64];
    std::snprintf(blocked, sizeof(blocked), "; blocked on 0x%llx (", static_cast<unsigned long long>(address(label)));
    check(failure.find("but no queue runs and the serials stopped at " + std::to_string(completed) + "/" + std::to_string(accepted) + blocked) != std::string::npos, ("the gate of a delta read in flight did not end once no queue ran while the submission it waits for stayed parked on title memory: " + failure).c_str());
    check(delta == 0, "the replay restored a delta read in flight although the submission it waits for never completed");
}

void testDepthAfterSubmits(const std::filesystem::path& directory) {
    auto* bytes = allocate(FrameCaptureWriter::BlockBytes);
    auto* label = reinterpret_cast<std::uint32_t*>(bytes);
    auto* commands = reinterpret_cast<std::uint32_t*>(bytes + 4096);
    *label = 0;
    const std::array<std::uint32_t, 7> wait{0xc0053c00u, 0x13u, static_cast<std::uint32_t>(address(label)), static_cast<std::uint32_t>(address(label) >> 32u), 1u, 0xffffffffu, 4u};
    std::copy(wait.begin(), wait.end(), commands);
    const std::uint32_t ready = 1;
    FrameCaptureWriter writer(directory);
    writer.WriteDelta(1, 0, address(label), std::as_bytes(std::span(&ready, 1)));
    writer.Finish("{}");
    const FrameCaptureReader reader(directory);
    Options options;
    options.release = false;
    options.paceTimeout = std::chrono::milliseconds(300);
    options.busyTimeout = std::chrono::milliseconds(5000);
    Replay replay{options, reader};
    replay.recording = true;
    collectGroups(replay);
    auto output = std::make_shared<ReplayOutput>(replay.frames);
    AgcDriverRegisterVideoOutput_nid_postfix(7, output);
    FrameCall parked;
    parked.kind = FrameCallKind::Submit;
    parked.ordinal = 1;
    parked.thread = 1;
    parked.address = address(commands);
    parked.size = wait.size();
    auto reservation = reservationCall(AgcDriver::EopFlipTokenBase);
    reservation.ordinal = 2;
    reservation.thread = 1;
    reservation.accepted = 1;
    auto delta = memoryCall(1, 1);
    delta.ordinal = 3;
    const std::array calls{parked, reservation, delta};
    replay.runWindow(calls);
    AgcDriverUnregisterVideoOutput_nid_postfix(7, output);
    check(*label == ready && replay.unfinished == 0, "the parked submission did not complete once the delta restored its label");
    check(replay.depthSamples.load() == 1 && replay.depthMax.load() == 0, ("the replay sampled the in-flight depth at an EOP flip reservation, while submit_depths counts only submissions: " + std::to_string(replay.depthSamples.load()) + " samples, in_flight_max " + std::to_string(replay.depthMax.load())).c_str());
}

void testDepthOfTimedPasses(const std::filesystem::path& directory) {
    auto* bytes = allocate(FrameCaptureWriter::BlockBytes);
    auto* label = reinterpret_cast<std::uint32_t*>(bytes);
    auto* commands = reinterpret_cast<std::uint32_t*>(bytes + 4096);
    auto* nop = reinterpret_cast<std::uint32_t*>(bytes + 8192);
    *label = 0;
    const std::array<std::uint32_t, 7> wait{0xc0053c00u, 0x13u, static_cast<std::uint32_t>(address(label)), static_cast<std::uint32_t>(address(label) >> 32u), 1u, 0xffffffffu, 4u};
    std::copy(wait.begin(), wait.end(), commands);
    const std::array<std::uint32_t, 2> empty{0xc0001000u, 0u};
    std::copy(empty.begin(), empty.end(), nop);
    const std::uint32_t ready = 1;
    FrameCaptureWriter writer(directory);
    writer.WriteDelta(1, 0, address(label), std::as_bytes(std::span(&ready, 1)));
    writer.Finish("{}");
    const FrameCaptureReader reader(directory);
    Options options;
    options.release = false;
    options.paceTimeout = std::chrono::milliseconds(300);
    options.busyTimeout = std::chrono::milliseconds(5000);
    Replay replay{options, reader};
    collectGroups(replay);
    FrameCall prologue;
    prologue.kind = FrameCallKind::Submit;
    prologue.ordinal = 1;
    prologue.thread = 1;
    prologue.address = address(commands);
    prologue.size = wait.size();
    replay.execute(prologue);
    auto submit = prologue;
    submit.ordinal = 2;
    submit.address = address(nop);
    submit.size = empty.size();
    submit.accepted = 1;
    auto delta = memoryCall(1, 1);
    delta.ordinal = 3;
    const std::array window{submit, delta};
    replay.recording = true;
    replay.runWindow(window);
    check(*label == ready && replay.unfinished == 0 && replay.depthSamples.load() == 1 && replay.depthMax.load() == 1, "the first pass did not sample its submission at depth 1 behind the prologue's submission parked on a label");
    AgcDriverWaitIdle_nid_postfix();
    replay.pass = 1;
    replay.runWindow(window);
    check(replay.unfinished == 0, "the second pass did not complete");
    check(replay.depthSamples.load() == 1 && replay.depthMax.load() == 0, ("the in-flight depth of the second pass kept the first pass's sample, which warmed the caches and which frame_ms leaves out: " + std::to_string(replay.depthSamples.load()) + " samples, in_flight_max " + std::to_string(replay.depthMax.load())).c_str());
}

void testGateFailure(const std::filesystem::path& directory) {
    auto* bytes = allocate(FrameCaptureWriter::BlockBytes);
    auto* label = reinterpret_cast<std::uint32_t*>(bytes);
    auto* commands = reinterpret_cast<std::uint32_t*>(bytes + 4096);
    *label = 0;
    const std::array<std::uint32_t, 7> wait{0xc0053c00u, 0x13u, static_cast<std::uint32_t>(address(label)), static_cast<std::uint32_t>(address(label) >> 32u), 1u, 0xffffffffu, 4u};
    std::copy(wait.begin(), wait.end(), commands);
    const std::uint32_t stored = 7;
    FrameCaptureWriter writer(directory);
    writer.WriteDelta(1, 0, address(bytes + 8192), std::as_bytes(std::span(&stored, 1)));
    writer.Finish("{}");
    const FrameCaptureReader reader(directory);
    Options options;
    options.release = false;
    options.busyTimeout = std::chrono::milliseconds(3000);
    Replay replay{options, reader};
    replay.recording = true;
    collectGroups(replay);
    AgcDriverSubmitGraphics_nid_postfix(commands, static_cast<std::uint32_t>(wait.size()));
    const auto parked = [&] {
        std::array<AgcDriver::AwaitedWait, 64> awaited{};
        const auto count = AgcDriverAwaitedWaits_nid_postfix(awaited.data(), awaited.size());
        return std::any_of(awaited.begin(), awaited.begin() + static_cast<std::ptrdiff_t>(count), [&](const AgcDriver::AwaitedWait& slot) { return slot.address == address(label); });
    };
    const auto deadline = Clock::now() + std::chrono::seconds(10);
    while (!parked()) {
        check(Clock::now() < deadline, "the submission did not wait on its unwritten label");
        std::this_thread::yield();
    }
    const auto [accepted, completed] = progress();
    AgcDriverReportFailure_nid_postfix(std::make_exception_ptr(std::runtime_error("gate driver failure")));
    auto call = memoryCall(1, 1);
    call.ordinal = 1;
    call.flags = AgcDriver::InFlightDeltaFlag;
    call.accepted = accepted;
    call.completed = completed;
    call.values = {1, accepted};
    std::string failure;
    try {
        replay.runWindow(std::span(&call, 1));
    } catch (const std::runtime_error& error) {
        failure = error.what();
    }
    check(failure == "gate driver failure", "a driver failure during a flagged delta's gate did not end the replay with the driver's error");
    bool rethrown = false;
    try { AgcDriverShutdown_nid_postfix(); }
    catch (const std::runtime_error& error) { rethrown = std::string_view(error.what()) == "gate driver failure"; }
    check(rethrown, "the injected driver failure was not propagated at shutdown");
}

void testRewindBreaker(const std::filesystem::path& directory, bool previous) {
    auto* bytes = allocate(FrameCaptureWriter::BlockBytes);
    auto* commands = reinterpret_cast<std::uint32_t*>(bytes);
    auto* label = reinterpret_cast<std::uint32_t*>(bytes + 4096);
    *label = 0;
    const std::array<std::uint32_t, 7> rewound{0xc0005900u, 0u, 0xc0033700u, 0x00100200u, static_cast<std::uint32_t>(address(label)), static_cast<std::uint32_t>(address(label) >> 32u), 1u};
    std::copy(rewound.begin(), rewound.end(), commands);
    auto* const control = commands + 1;
    const std::uint32_t valid = AgcDriver::Pm4::RewindValid;
    FrameCaptureWriter writer(directory, 1, previous ? AgcDriver::InFlightCallsVersion - 1 : AgcDriver::CallsVersion);
    writer.WriteDelta(1, 0, address(control), std::as_bytes(std::span(&valid, 1)));
    writer.Finish("{}");
    const FrameCaptureReader reader(directory);
    check((reader.Version() < AgcDriver::InFlightCallsVersion) == previous, "the synthetic capture is not of the calls format this mode replays");
    Options options;
    options.paceTimeout = std::chrono::milliseconds(previous ? 5000 : 300);
    options.busyTimeout = std::chrono::milliseconds(5000);
    Replay replay{options, reader};
    replay.recording = true;
    collectGroups(replay);
    auto output = std::make_shared<ReplayOutput>(replay.frames);
    AgcDriverRegisterVideoOutput_nid_postfix(7, output);
    AgcDriverSubmitGraphics_nid_postfix(commands, static_cast<std::uint32_t>(rewound.size()));
    const auto parked = [&] {
        std::array<AgcDriver::AwaitedWait, 64> awaited{};
        const auto count = AgcDriverAwaitedWaits_nid_postfix(awaited.data(), awaited.size());
        return std::any_of(awaited.begin(), awaited.begin() + static_cast<std::ptrdiff_t>(count), [&](const AgcDriver::AwaitedWait& slot) { return slot.address == address(control); });
    };
    const auto deadline = Clock::now() + std::chrono::seconds(10);
    while (!parked()) {
        check(Clock::now() < deadline, "the submission did not park at its unreleased REWIND");
        std::this_thread::yield();
    }
    const auto [accepted, completed] = progress();
    check(accepted == completed + 1, "the REWIND submission was not the only one in flight");
    auto reservation = reservationCall(AgcDriver::EopFlipTokenBase);
    reservation.ordinal = 1;
    reservation.thread = 1;
    reservation.accepted = accepted;
    reservation.completed = accepted;
    auto delta = memoryCall(1, 1);
    delta.ordinal = 2;
    delta.flags = previous ? 0u : AgcDriver::InFlightDeltaFlag;
    delta.accepted = accepted;
    delta.completed = completed;
    const std::array calls{reservation, delta};
    replay.runWindow(calls);
    check(replay.lateReleases.load() == 0 && replay.syntheticReleases.load() == 0, "the breaker released the REWIND with a recorded value or one derived from the reference");
    if (previous) {
        check(replay.earlyReleases.load() == 1 && replay.paceTimeouts.load() == 0, "the breaker of the replay of a capture of the previous calls format did not apply the later memory delta early to release the REWIND before pacing gave up");
    } else {
        check(replay.earlyReleases.load() == 0, "the breaker released the REWIND early with the bytes of a memory delta read in flight, before that delta's record");
        check(replay.paceTimeouts.load() == 1, "the replay did not give up pacing while its queue waited on the REWIND");
    }
    check(std::atomic_ref<std::uint32_t>(*label).load() == 1 && replay.unfinished == 0, "the REWIND did not run its tail once its memory delta was restored");
    AgcDriverUnregisterVideoOutput_nid_postfix(7, output);
}

void testSerialDiverged(const std::filesystem::path& directory) {
    constexpr std::uint64_t Base = 40;
    const char* dump = std::getenv("APS5_DUMP_SUBMITS");
    check(std::getenv("APS5_CPU_FLIP_IN_ORDER") == nullptr && std::getenv("APS5_SYNC_FLIP") == nullptr && (dump == nullptr || *dump == '\0'), "the environment orders CPU flips or captures submissions, so the replay's CPU flip would take a serial");
    auto* bytes = allocate(FrameCaptureWriter::BlockBytes);
    auto* commands = reinterpret_cast<std::uint32_t*>(bytes);
    const std::array<std::uint32_t, 2> nop{0xc0001000u, 0u};
    std::copy(nop.begin(), nop.end(), commands);
    const std::uint32_t stored = 7;
    FrameCaptureWriter writer(directory);
    writer.WriteDelta(1, 0, address(bytes + 8192), std::as_bytes(std::span(&stored, 1)));
    FrameCall start;
    start.kind = FrameCallKind::FrameStart;
    start.accepted = Base;
    start.completed = Base;
    writer.WriteCall(start);
    writer.Finish("{}");
    const FrameCaptureReader reader(directory);
    Options options;
    options.release = false;
    options.busyTimeout = std::chrono::milliseconds(3000);
    Replay replay{options, reader};
    replay.recording = true;
    collectGroups(replay);
    auto output = std::make_shared<ReplayOutput>(replay.frames);
    AgcDriverRegisterVideoOutput_nid_postfix(7, output);
    const auto submit = [&](std::uint32_t queue, std::vector<std::uint64_t> taken) {
        FrameCall call;
        call.kind = FrameCallKind::Submit;
        call.ordinal = 1;
        call.thread = 1;
        call.queue = queue;
        call.address = address(commands);
        call.size = nop.size();
        call.accepted = Base;
        call.completed = Base;
        call.values = std::move(taken);
        return call;
    };
    const auto suspend = [&](std::vector<std::uint64_t> taken) {
        FrameCall call;
        call.kind = FrameCallKind::Suspend;
        call.ordinal = 1;
        call.thread = 1;
        call.accepted = Base;
        call.completed = Base;
        call.values = std::move(taken);
        return call;
    };
    const auto delta = [&](std::uint64_t ordinal, std::uint64_t accepted, std::uint64_t listed) {
        auto call = memoryCall(1, 1);
        call.ordinal = ordinal;
        call.flags = AgcDriver::InFlightDeltaFlag;
        call.accepted = accepted;
        call.completed = Base;
        call.values = {1, listed};
        return call;
    };
    const auto refusal = [&](std::span<const FrameCall> calls) {
        std::string error;
        try {
            replay.runWindow(calls);
        } catch (const std::runtime_error& failure) {
            error = failure.what();
        }
        AgcDriverWaitIdle_nid_postfix();
        return error;
    };
    const std::array mapped{submit(0, {Base + 1}), delta(2, Base + 1, Base + 1)};
    const auto replayed = refusal(mapped);
    check(replayed.empty(), ("the replay refused a submission recorded as taking the serial after the frame start: " + replayed).c_str());
    if (!APS5_ENABLE_TIMING_LOG) {
        FrameCall flip;
        flip.kind = FrameCallKind::Flip;
        flip.ordinal = 2;
        flip.thread = 1;
        flip.cpuFlip = true;
        flip.accepted = Base + 1;
        flip.completed = Base;
        flip.values = {7, static_cast<std::uint64_t>(-1), 1, 0, Base + 3};
        const std::array flipped{submit(0x20, {Base + 1}), flip, delta(3, Base + 3, Base + 1)};
        const auto skipped = refusal(flipped);
        check(skipped.find("took no replay serial") != std::string::npos, ("the replay ran a CPU flip recorded as taking the third serial after the frame start, after a submission that took the first, and did not refuse the delta read in flight behind it: " + skipped).c_str());
    }
    const std::array diverged{submit(0, {Base + 2}), delta(2, Base + 2, Base + 2)};
    const auto unmapped = refusal(diverged);
    check(unmapped.find("took replay serial") != std::string::npos, ("the replay ran a submission recorded as taking the second serial after the frame start, which no recorded call before it took, and did not refuse the delta read in flight behind it: " + unmapped).c_str());
    const std::array unrecorded{submit(0, {}), delta(2, Base + 1, Base + 1)};
    const auto missing = refusal(unrecorded);
    check(missing.find("has no recorded serial") != std::string::npos, ("the replay ran a submission that recorded no serial before a delta read in flight: " + missing).c_str());
    const std::array unlined{delta(1, Base + 1, Base + 1)};
    const auto offset = refusal(unlined);
    check(offset.find("but this replay had accepted") != std::string::npos, ("the replay restored a delta read in flight at a serial that no call before it in the window took: " + offset).c_str());
    const std::array listedBefore{submit(0, {Base + 1}), delta(2, Base + 1, Base)};
    const auto unlisted = refusal(listedBefore);
    check(unlisted.find("no call since the frame start recorded that serial") != std::string::npos, ("the replay restored a delta read in flight that lists a serial no call since the frame start took: " + unlisted).c_str());
    FrameCall other;
    other.kind = FrameCallKind::DisplayBuffer;
    other.ordinal = 2;
    other.thread = 2;
    const std::array threaded{submit(0, {Base + 1}), other, delta(3, Base + 1, Base + 1)};
    const auto unordered = refusal(threaded);
    check(unordered.find("after a second thread made calls") != std::string::npos, ("the replay restored a delta read in flight after a second thread's call, which it cannot order: " + unordered).c_str());
    const std::array divergedSuspend{suspend({Base + 2}), delta(2, Base + 2, Base + 2)};
    const auto unmappedSuspend = refusal(divergedSuspend);
    check(unmappedSuspend.find("took replay serial") != std::string::npos, ("the replay ran a suspend point recorded as taking the second serial after the frame start, which no recorded call before it took, and did not refuse the delta read in flight behind it: " + unmappedSuspend).c_str());
    const auto refusedBeforeRestore = [&](const std::string& name, std::span<const FrameCall> calls) {
        const auto capture = directory / name;
        {
            FrameCaptureWriter captureWriter(capture);
            captureWriter.WriteCall(start);
            for (const auto& call : calls) captureWriter.WriteCall(call);
            captureWriter.Finish("{}");
        }
        std::string error;
        try {
            Options paced;
            paced.directory = capture;
            run(paced);
        } catch (const std::runtime_error& failure) {
            error = failure.what();
        }
        return error;
    };
    const auto early = refusedBeforeRestore("unrecorded", unrecorded);
    check(early.find("has no recorded serial") != std::string::npos, ("the replay did not refuse before its restore a capture whose submission before a delta read in flight recorded no serial: " + early).c_str());
    const std::array unrecordedSuspend{suspend({}), delta(2, Base + 1, Base + 1)};
    const auto earlySuspend = refusedBeforeRestore("unsuspended", unrecordedSuspend);
    check(earlySuspend.find("has no recorded serial") != std::string::npos, ("the replay did not refuse before its restore a capture whose suspend point before a delta read in flight recorded no serial: " + earlySuspend).c_str());
    AgcDriverUnregisterVideoOutput_nid_postfix(7, output);
}

class PresentingFlip final : public AgcDriver::IFlipRequest {
public:
    PresentingFlip(Frames& frames, const std::uint32_t* restored, std::atomic<std::uint32_t>& seen) : frames(frames), restored(restored), seen(seen) {}
    void GpuReady(const std::shared_ptr<AgcDriver::FrameTiming>&) override {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        seen.store(*static_cast<const volatile std::uint32_t*>(restored));
        frames.MarkReady();
    }
    void Fail(std::exception_ptr error) noexcept override { frames.Failed(error); }

private:
    Frames& frames;
    const std::uint32_t* restored;
    std::atomic<std::uint32_t>& seen;
};

class PresentingOutput final : public AgcDriver::IVideoOutput {
public:
    PresentingOutput(Frames& frames, const std::uint32_t* restored) : frames(frames), restored(restored) {}
    std::shared_ptr<AgcDriver::IFlipRequest> Reserve(const AgcDriver::FlipInfo&) override { return std::make_shared<PresentingFlip>(frames, restored, seen); }
    void Fail(std::exception_ptr error) noexcept override { frames.Failed(error); }
    std::atomic<std::uint32_t> seen{~0u};

private:
    Frames& frames;
    const std::uint32_t* restored;
};

void testSerialFlipInOrder(const std::filesystem::path& directory) {
    constexpr std::uint64_t Base = 40;
    check(std::getenv("APS5_CPU_FLIP_IN_ORDER") != nullptr, "serial_flip_in_order needs APS5_CPU_FLIP_IN_ORDER");
    auto* bytes = allocate(FrameCaptureWriter::BlockBytes);
    auto* commands = reinterpret_cast<std::uint32_t*>(bytes);
    auto* restored = reinterpret_cast<std::uint32_t*>(bytes + 8192);
    *restored = 0;
    const std::array<std::uint32_t, 2> nop{0xc0001000u, 0u};
    std::copy(nop.begin(), nop.end(), commands);
    const std::uint32_t stored = 7;
    FrameCaptureWriter writer(directory);
    writer.WriteDelta(1, 0, address(restored), std::as_bytes(std::span(&stored, 1)));
    FrameCall start;
    start.kind = FrameCallKind::FrameStart;
    start.accepted = Base;
    start.completed = Base;
    writer.WriteCall(start);
    writer.Finish("{}");
    const FrameCaptureReader reader(directory);
    Options options;
    options.release = false;
    options.busyTimeout = std::chrono::milliseconds(3000);
    Replay replay{options, reader};
    replay.recording = true;
    collectGroups(replay);
    auto output = std::make_shared<PresentingOutput>(replay.frames, restored);
    AgcDriverRegisterVideoOutput_nid_postfix(7, output);
    FrameCall compute;
    compute.kind = FrameCallKind::Submit;
    compute.ordinal = 1;
    compute.thread = 1;
    compute.queue = 0x20;
    compute.address = address(commands);
    compute.size = nop.size();
    compute.accepted = Base;
    compute.completed = Base;
    compute.values = {Base + 1};
    FrameCall flip;
    flip.kind = FrameCallKind::Flip;
    flip.ordinal = 2;
    flip.thread = 1;
    flip.cpuFlip = true;
    flip.accepted = Base + 1;
    flip.completed = Base;
    flip.values = {7, static_cast<std::uint64_t>(-1), 1, 0, 0};
    auto delta = memoryCall(1, 1);
    delta.ordinal = 3;
    delta.flags = AgcDriver::InFlightDeltaFlag;
    delta.accepted = Base + 1;
    delta.completed = Base;
    delta.values = {1, Base + 1};
    const std::array calls{compute, flip, delta};
    std::string refusal;
    try {
        replay.runWindow(calls);
    } catch (const std::runtime_error& error) {
        refusal = error.what();
    }
    AgcDriverWaitIdle_nid_postfix();
    AgcDriverUnregisterVideoOutput_nid_postfix(7, output);
    check(refusal.empty(), ("the replay refused a CPU flip that took a serial in the replay and none in the capture, although the delta read in flight behind it lists only the compute submission before the flip: " + refusal).c_str());
    check(*restored == stored, "the replay did not restore the delta read in flight behind a CPU flip that took a serial only in the replay");
    check(replay.frames.Times().size() == 1 && output->seen.load() == 0, ("the replay restored the delta read in flight behind a CPU flip that took a serial only in the replay before that flip presented, while the capture's flip presented before its next call (restored word at the present: " + std::to_string(output->seen.load()) + ")").c_str());
}

void testFlipSerialNeutral(const std::filesystem::path& directory) {
    constexpr std::uint64_t Base = 40;
    if (APS5_ENABLE_TIMING_LOG) return;
    const char* dump = std::getenv("APS5_DUMP_SUBMITS");
    check(std::getenv("APS5_CPU_FLIP_IN_ORDER") == nullptr && std::getenv("APS5_SYNC_FLIP") == nullptr && (dump == nullptr || *dump == '\0'), "the environment orders CPU flips or captures submissions, so the replay's CPU flip would take a serial");
    auto* bytes = allocate(FrameCaptureWriter::BlockBytes);
    auto* commands = reinterpret_cast<std::uint32_t*>(bytes);
    auto* label = reinterpret_cast<std::uint32_t*>(bytes + 4096);
    auto* restored = reinterpret_cast<std::uint32_t*>(bytes + 8192);
    auto* waiting = reinterpret_cast<std::uint32_t*>(bytes + 12288);
    *label = 0;
    *restored = 0;
    const std::array<std::uint32_t, 2> nop{0xc0001000u, 0u};
    std::copy(nop.begin(), nop.end(), commands);
    const std::array<std::uint32_t, 7> wait{0xc0053c00u, 0x13u, static_cast<std::uint32_t>(address(label)), static_cast<std::uint32_t>(address(label) >> 32u), 1u, 0xffffffffu, 4u};
    std::copy(wait.begin(), wait.end(), waiting);
    const std::uint32_t stored = 7;
    FrameCaptureWriter writer(directory);
    writer.WriteDelta(1, 0, address(restored), std::as_bytes(std::span(&stored, 1)));
    FrameCall start;
    start.kind = FrameCallKind::FrameStart;
    start.accepted = Base;
    start.completed = Base;
    writer.WriteCall(start);
    writer.Finish("{}");
    const FrameCaptureReader reader(directory);
    Options options;
    options.release = false;
    options.paceTimeout = std::chrono::milliseconds(300);
    options.busyTimeout = std::chrono::milliseconds(5000);
    Replay replay{options, reader};
    replay.recording = true;
    collectGroups(replay);
    auto output = std::make_shared<ReplayOutput>(replay.frames);
    AgcDriverRegisterVideoOutput_nid_postfix(7, output);
    const auto submit = [&](std::uint64_t ordinal, std::uint32_t queue, const std::uint32_t* words, std::size_t size, std::uint64_t taken) {
        FrameCall call;
        call.kind = FrameCallKind::Submit;
        call.ordinal = ordinal;
        call.thread = 1;
        call.queue = queue;
        call.address = address(words);
        call.size = size;
        call.accepted = taken - 1;
        call.completed = taken - 1;
        call.values = {taken};
        return call;
    };
    FrameCall flip;
    flip.kind = FrameCallKind::Flip;
    flip.ordinal = 2;
    flip.thread = 1;
    flip.cpuFlip = true;
    flip.accepted = Base + 1;
    flip.completed = Base + 1;
    flip.values = {7, static_cast<std::uint64_t>(-1), 1, 0, Base + 2};
    const auto delta = [&](std::vector<std::uint64_t> listed) {
        auto call = memoryCall(1, 1);
        call.ordinal = 4;
        call.flags = AgcDriver::InFlightDeltaFlag;
        call.accepted = Base + 3;
        call.completed = Base;
        call.values = {1};
        call.values.insert(call.values.end(), listed.begin(), listed.end());
        return call;
    };
    const auto refusal = [&](std::span<const FrameCall> calls) {
        std::string error;
        try {
            replay.runWindow(calls);
        } catch (const std::runtime_error& failure) {
            error = failure.what();
        }
        return error;
    };
    const std::array presented{submit(1, 0x20, commands, nop.size(), Base + 1), flip, submit(3, 0, commands, nop.size(), Base + 3), delta({Base + 1, Base + 2, Base + 3})};
    const auto replayed = refusal(presented);
    AgcDriverWaitIdle_nid_postfix();
    check(replayed.empty(), ("the replay refused a CPU flip that took a serial in the capture and none in the replay, so the serials after it could not be mapped: " + replayed).c_str());
    check(*restored == stored && replay.frames.Times().size() == 1, "the replay did not present the CPU flip that took no serial and restore the delta read in flight behind it");
    *restored = 0;
    const auto accepted = progress().first;
    const std::array parked{submit(1, 0x20, commands, nop.size(), Base + 1), flip, submit(3, 0, waiting, wait.size(), Base + 3), delta({Base + 2, Base + 3})};
    const auto gated = refusal(parked);
    std::atomic_ref<std::uint32_t>(*label).store(1, std::memory_order_release);
    AgcDriverWaitIdle_nid_postfix();
    const auto expected = "frame replay: memory delta at call 4 waits for submission " + std::to_string(Base + 3) + " (replay serial " + std::to_string(accepted + 2) + "), which had started when the capture read the delta, but no queue runs";
    check(gated.starts_with(expected), ("the delta read in flight behind a CPU flip that took a serial only in the capture did not skip the flip's serial and wait for the submission after it at the replay serial that submission took (" + expected + "): " + gated).c_str());
    check(*restored == 0, "the replay restored a delta read in flight although the submission it waits for never completed");
    auto* computeLabel = reinterpret_cast<std::uint32_t*>(bytes + 16384);
    auto* computeWaiting = reinterpret_cast<std::uint32_t*>(bytes + 20480);
    *computeLabel = 0;
    const std::array<std::uint32_t, 7> computeWait{0xc0053c00u, 0x13u, static_cast<std::uint32_t>(address(computeLabel)), static_cast<std::uint32_t>(address(computeLabel) >> 32u), 1u, 0xffffffffu, 4u};
    std::copy(computeWait.begin(), computeWait.end(), computeWaiting);
    auto flipBehind = flip;
    flipBehind.completed = Base;
    auto graphics = submit(3, 0, commands, nop.size(), Base + 3);
    graphics.completed = Base;
    const auto before = progress().first;
    const std::array earlier{submit(1, 0x20, computeWaiting, computeWait.size(), Base + 1), flipBehind, graphics, delta({Base + 1})};
    const auto gatedEarlier = refusal(earlier);
    std::atomic_ref<std::uint32_t>(*computeLabel).store(1, std::memory_order_release);
    AgcDriverWaitIdle_nid_postfix();
    AgcDriverUnregisterVideoOutput_nid_postfix(7, output);
    const auto expectedEarlier = "frame replay: memory delta at call 4 waits for submission " + std::to_string(Base + 1) + " (replay serial " + std::to_string(before + 1) + "), which had started when the capture read the delta, but no queue runs";
    check(gatedEarlier.starts_with(expectedEarlier), ("the delta read in flight behind a CPU flip that took a serial only in the capture did not wait for the compute submission before the flip at the replay serial it took (" + expectedEarlier + "): " + gatedEarlier).c_str());
    check(*restored == 0, "the replay restored a delta read in flight while the compute submission before the CPU flip, which it waits for, was parked");
}

void testUnverifiedDeltas(const std::filesystem::path& directory) {
    auto* bytes = allocate(FrameCaptureWriter::BlockBytes);
    const std::uint32_t stored = 7;
    std::uintptr_t base = 0;
    std::size_t arenaSize = 0;
    GuestArena::GuestArenaRange_nid_postfix(&base, &arenaSize);
    const auto arena = "{\"arena_base\":" + std::to_string(base);
    const auto refusal = [&](const std::string& name, bool inFlight, const std::optional<std::string>& header, std::optional<std::uint32_t> version = std::nullopt) {
        const auto capture = directory / name;
        {
            FrameCaptureWriter writer(capture, 1, version.value_or(AgcDriver::CallsVersion));
            writer.WriteRange(address(bytes), FrameCaptureWriter::BlockBytes, FrameRangeKind::Unwatched, ReadWrite, FrameRangeAccess::Readable | FrameRangeAccess::Writable);
            writer.WriteDelta(1, 1, address(bytes), std::as_bytes(std::span(&stored, 1)));
            auto call = memoryCall(1, 1);
            call.ordinal = 1;
            call.accepted = 2;
            call.completed = inFlight ? 1 : 2;
            if (inFlight) {
                call.flags = AgcDriver::InFlightDeltaFlag;
                call.values.push_back(2);
            }
            writer.WriteCall(call);
            if (header) writer.Finish(*header);
        }
        const FrameCaptureReader reader(capture);
        try {
            restoreMemory(reader);
        } catch (const std::exception& error) {
            return std::string(error.what());
        }
        return std::string();
    };
    const auto refused = [&](const std::string& name, const std::optional<std::string>& header, const char* reason) {
        const auto message = refusal(name, true, header);
        check(!message.empty(), ("the replay accepted capture " + name + ", whose memory delta was read with submissions in flight and never verified").c_str());
        check(message.find(reason) != std::string::npos, ("the replay refused capture " + name + " without '" + reason + "': " + message).c_str());
    };
    refused("missing", std::nullopt, "capture.json is missing");
    refused("bare", arena + "}", "capture.json has no stale_delta");
    refused("unrecorded", arena + ",\"stale_delta\":0,\"warnings\":[]}", "capture.json has no unverified_deltas");
    refused("unparsed", arena + ",\"stale_delta\":0,\"unverified_deltas\":null,\"warnings\":[]}", "unverified_deltas is not a number");
    refused("unverified", arena + ",\"stale_delta\":0,\"unverified_deltas\":1,\"warnings\":[]}", "capture.json has unverified_deltas 1");
    refused("stale", arena + ",\"stale_delta\":1,\"unverified_deltas\":0,\"warnings\":[\"stopped: memory delta 1 was read while submission 2, in flight at its read, stored into its page 0x10000\"]}", "(stale_delta 1)");
    const auto verified = refusal("verified", true, arena + ",\"stale_delta\":0,\"unverified_deltas\":0,\"warnings\":[\"a \\\"stopped: quoted\\\" warning \\\\ \\u0009\",\"4096 bytes under multisampled targets whose samples are only on the host are zero in the capture, so replay starts those targets without their earlier contents\"]}");
    check(verified.empty(), ("the replay refused a capture whose memory delta read in flight was verified: " + verified).c_str());
    const auto stopped = refusal("stopped", true, arena + ",\"stale_delta\":0,\"unverified_deltas\":0,\"warnings\":[\"stopped: less than 4 GiB free on the capture volume\"]}");
    check(stopped.empty(), ("the replay refused a capture that stopped early, although the check after its stop verified its memory delta read in flight: " + stopped).c_str());
    const auto held = refusal("held", false, arena + ",\"unverified_deltas\":1,\"warnings\":[\"stopped: less than 4 GiB free on the capture volume\"]}");
    check(held.empty(), ("the replay refused a capture without memory deltas read in flight: " + held).c_str());
    const auto unheaded = refusal("unheaded", false, std::nullopt);
    check(unheaded.find("capture.json is missing") != std::string::npos, ("the replay did not refuse capture unheaded, which has no memory delta read in flight but no capture.json either, so its last records may name memory that was never written: " + unheaded).c_str());
    const auto unheadedPrevious = refusal("unheaded_previous", false, std::nullopt, AgcDriver::InFlightCallsVersion - 1);
    check(unheadedPrevious.empty(), ("the replay refused capture unheaded_previous, a capture of the previous calls format without capture.json, which that format never needed: " + unheadedPrevious).c_str());
}

void testShaderCode(const std::filesystem::path& directory, bool host, std::string_view mode = {}) {
    const bool prologue = mode.starts_with("shader_prologue_");
    const bool readOnly = mode.ends_with("readonly");
    const bool hostHeader = readOnly || mode.ends_with("header");
    std::byte* hostMemory = nullptr;
    auto* bytes = allocate(1u << 20);
    auto* shader = reinterpret_cast<Shader*>(bytes);
    auto* code = bytes + (1u << 19) + 256;
    constexpr std::size_t CodeBytes = 3 * FrameCaptureWriter::BlockBytes;
#if defined(_WIN32) || defined(__linux__)
    if (host) {
#ifdef _WIN32
        auto* memory = static_cast<std::byte*>(VirtualAlloc(nullptr, CodeBytes + 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
#else
        void* mapped = mmap(nullptr, CodeBytes + 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        auto* memory = mapped != MAP_FAILED ? static_cast<std::byte*>(mapped) : nullptr;
#endif
        check(memory != nullptr, "host shader allocation failed");
        if (!prologue) {
            GuestAllocations::Mutation mutation;
            mutation.Add(memory, CodeBytes + 4096, true, true, true);
        }
        hostMemory = memory;
        if (hostHeader) shader = reinterpret_cast<Shader*>(memory);
        code = memory + 256;
    }
#endif
    *shader = {};
    shader->file_header = 0x34333231u;
    shader->version = 0x18u;
    shader->header_size = sizeof(Shader);
    shader->shader_size = CodeBytes;
    shader->code = code;
    auto* recorder = AgcDriver::FrameRecorder::Active();
    check(recorder != nullptr, "frame recorder is disabled");
    if (!prologue) recorder->FrameBoundary();
    std::vector<std::byte> expected(CodeBytes);
    for (std::size_t i = 0; i < expected.size(); ++i) expected[i] = static_cast<std::byte>(i * 31 + 17);
    std::memcpy(code, expected.data(), expected.size());
    std::vector<std::byte> expectedHeader(sizeof(Shader));
    std::memcpy(expectedHeader.data(), shader, expectedHeader.size());
    if (readOnly) {
#ifdef _WIN32
        DWORD previous = 0;
        check(VirtualProtect(hostMemory, CodeBytes + 4096, PAGE_READONLY, &previous) != 0, "cannot protect host shader pages");
#elif defined(__linux__)
        check(mprotect(hostMemory, CodeBytes + 4096, PROT_READ) == 0, "cannot protect host shader pages");
#endif
    }
    recorder->RegisterShader(shader);
    recorder->FrameBoundary();
    if (prologue) recorder->FrameBoundary();
    const FrameCaptureReader reader(directory);
    checkRanToEnd(reader);
    std::vector<std::byte> captured(CodeBytes);
    std::vector<std::byte> capturedHeader(sizeof(Shader));
    const auto applyBytes = [&](const FrameMemoryChunk& chunk, std::uint64_t target, std::span<std::byte> bytes) {
        const auto begin = std::max(target, chunk.address);
        const auto end = std::min(target + bytes.size(), chunk.address + chunk.bytes);
        if (begin >= end) return;
        const auto data = reader.Read(chunk);
        std::memcpy(bytes.data() + begin - target, data.data() + begin - chunk.address, end - begin);
    };
    const auto apply = [&](const FrameMemoryChunk& chunk) {
        applyBytes(chunk, address(code), captured);
        if (hostHeader) applyBytes(chunk, address(shader), capturedHeader);
    };
    for (const auto& chunk : reader.Chunks()) if (chunk.kind == FrameMemoryKind::Snapshot) apply(chunk);
    bool registered = false;
    for (const auto& call : reader.Calls()) {
        if (call.kind == FrameCallKind::RegisterShader) {
            registered = true;
            break;
        }
        if (call.kind != FrameCallKind::Memory) continue;
        for (const auto& chunk : reader.Chunks()) {
            if (chunk.kind == FrameMemoryKind::Delta && chunk.group == call.values.at(0)) apply(chunk);
        }
    }
    if (hostHeader && captured == expected) check(capturedHeader == expectedHeader, "the host shader header was not captured before registration");
    if (readOnly && captured == expected) {
        const bool protectedHeader = std::ranges::any_of(reader.Chunks(), [&](const auto& chunk) {
            return chunk.kind == FrameMemoryKind::Range && chunk.address <= address(shader) &&
                address(shader) - chunk.address < chunk.bytes && (chunk.access & AgcDriver::FrameRangeAccess::Writable) == 0;
        });
        check(protectedHeader, "the read-only host shader was recorded with write access");
    }
    check(registered && captured == expected, "the complete separate shader code was not captured before registration");
    CapturedMemory memory(reader);
    std::map<std::uint64_t, std::vector<const FrameMemoryChunk*>> groups;
    for (const auto& chunk : reader.Chunks()) {
        if (chunk.kind == FrameMemoryKind::Delta) groups[chunk.group].push_back(&chunk);
    }
    std::optional<std::vector<std::byte>> listed;
    for (const auto& call : reader.Calls()) {
        if (call.kind == FrameCallKind::RegisterShader) {
            listed = memory.Read(address(code), CodeBytes);
            break;
        }
        if (call.kind != FrameCallKind::Memory) continue;
        if (const auto found = groups.find(call.values.at(0)); found != groups.end()) memory.Apply(found->second);
    }
    check(listed && *listed == expected, "the offline shader listing read other code than the capture holds at the registration");
}

void testDeltaPaths(const std::filesystem::path& directory, std::string_view mode) {
    constexpr std::size_t Block = FrameCaptureWriter::BlockBytes;
    constexpr std::size_t PieceBytes = Block + Block / 2;
    const bool host = mode != "shader_unaligned_restored";
    const bool rewritten = mode == "shader_host_rewritten";
    const std::size_t codeBytes = host ? 3 * Block : Block;
    Shader* shader = nullptr;
    std::byte* code = nullptr;
    if (host) {
        shader = reinterpret_cast<Shader*>(allocate(1u << 20));
        std::byte* memory = nullptr;
#ifdef _WIN32
        memory = static_cast<std::byte*>(VirtualAlloc(nullptr, codeBytes + 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
#elif defined(__linux__)
        void* mapped = mmap(nullptr, codeBytes + 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        memory = mapped != MAP_FAILED ? static_cast<std::byte*>(mapped) : nullptr;
#endif
        check(memory != nullptr, "host shader allocation failed");
        GuestAllocations::Mutation mutation;
        mutation.Add(memory, codeBytes + 4096, true, true, true);
        code = memory + 256;
    } else {
        auto* block = static_cast<std::byte*>(GuestArena::GuestArenaAllocate_nid_postfix(2 * Block, Block));
        check(block != nullptr, "guest reservation failed");
        GuestArena::GuestArenaRelease_nid_postfix(block, 2 * Block);
        auto* piece = block + Block / 2;
        GuestArena::GuestArenaCommit_nid_postfix(piece, PieceBytes, 0x04, PieceBytes);
#ifdef _WIN32
        AgcDriver::GuestMemory::Unwatch(address(piece), PieceBytes);
#endif
        check(!AgcDriver::GuestMemory::Watched(address(piece), PieceBytes), "the unaligned shader range is write-watched");
        GuestAllocations::Mutation mutation;
        mutation.Add(piece, PieceBytes, true, true, false);
        shader = reinterpret_cast<Shader*>(piece);
        code = piece + 256;
    }
    *shader = {};
    shader->file_header = 0x34333231u;
    shader->version = 0x18u;
    shader->header_size = sizeof(Shader);
    shader->shader_size = codeBytes;
    shader->code = code;
    auto* recorder = AgcDriver::FrameRecorder::Active();
    check(recorder != nullptr, "frame recorder is disabled");
    recorder->FrameBoundary();
    const auto store = [&](unsigned seed) {
        std::vector<std::byte> bytes(codeBytes);
        for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<std::byte>(i * 31 + seed);
        std::memcpy(code, bytes.data(), bytes.size());
        return bytes;
    };
    const auto checked = rewritten ? FrameCallKind::RegisterShader : FrameCallKind::Flip;
    const auto call = [&](FrameCallKind kind) {
        if (kind == FrameCallKind::Flip) recorder->Flip(AgcDriver::FlipInfo{}, {});
        else recorder->RegisterShader(shader);
    };
    const auto expected = store(17);
    call(checked);
    store(29);
    call(rewritten ? FrameCallKind::Flip : FrameCallKind::RegisterShader);
    store(17);
    call(checked);
    recorder->FrameBoundary();
    const FrameCaptureReader reader(directory);
    CapturedMemory memory(reader);
    std::map<std::uint64_t, std::vector<const FrameMemoryChunk*>> groups;
    for (const auto& chunk : reader.Chunks()) {
        if (chunk.kind == FrameMemoryKind::Delta) groups[chunk.group].push_back(&chunk);
    }
    std::size_t seen = 0;
    std::optional<std::vector<std::byte>> replayed;
    for (const auto& captured : reader.Calls()) {
        if (captured.kind == checked && ++seen == 2) {
            replayed = memory.Read(address(code), codeBytes);
            break;
        }
        if (captured.kind != FrameCallKind::Memory) continue;
        if (const auto found = groups.find(captured.values.at(0)); found != groups.end()) memory.Apply(found->second);
    }
    check(seen == 2, "the capture lost a call of the test");
    check(replayed && *replayed == expected, rewritten ? "the second registration replays the host code a flip's delta wrote, not the code the title registered again" :
        host ? "after the second flip the replay keeps the host code a shader delta wrote, not the code the title restored" :
        "after the second flip the replay keeps the code a shader delta wrote into an unaligned unwatched range, not the code the title restored");
}

bool insideRanges(const FrameCaptureReader& reader, const FrameMemoryChunk& chunk) {
    std::map<std::uint64_t, std::uint64_t> ranges;
    for (const auto& range : reader.Chunks()) {
        if (range.kind == FrameMemoryKind::Range) ranges[range.address] = std::max(ranges[range.address], range.address + range.bytes);
    }
    auto cursor = chunk.address;
    for (const auto& [begin, end] : ranges) {
        if (begin <= cursor && cursor < end) cursor = end;
    }
    return cursor >= chunk.address + chunk.bytes;
}

std::atomic<std::uint32_t*> pendingResult{nullptr};

void storePendingResult(std::uint64_t begin, std::size_t bytes) {
    auto* const result = pendingResult.load();
    if (result == nullptr || begin >= address(result) + sizeof(std::uint32_t) || address(result) >= begin + bytes) return;
    *result = 1;
    pendingResult = nullptr;
}

std::atomic<std::uint64_t> collectOnRead{0};
std::atomic<std::byte*> collectedPage{nullptr};

void storeAndCollect(std::uint64_t begin, std::size_t bytes) {
    const auto trigger = collectOnRead.load();
    if (trigger == 0 || trigger < begin || trigger >= begin + bytes) return;
    collectOnRead = 0;
    auto* const page = collectedPage.load();
    std::fill_n(page, 4096, std::byte{0x6b});
    AgcDriver::GuestMemory::CollectWritesUncached(address(page), 4096);
}

std::uint64_t groupBeforeSuspend(const FrameCaptureReader& reader, std::size_t index) {
    const FrameCall* previous = nullptr;
    for (const auto& call : reader.Calls()) {
        if (call.kind == FrameCallKind::Suspend) {
            if (index == 0) return previous != nullptr && previous->kind == FrameCallKind::Memory ? previous->values.at(0) : 0;
            --index;
        }
        previous = &call;
    }
    return 0;
}

std::vector<std::uint64_t> deltaGroupsHolding(const FrameCaptureReader& reader, const std::byte* page) {
    std::vector<std::uint64_t> groups;
    for (const auto& chunk : reader.Chunks()) {
        if (chunk.kind != FrameMemoryKind::Delta || chunk.address >= address(page) + 4096 || address(page) >= chunk.address + chunk.bytes) continue;
        if (std::find(groups.begin(), groups.end(), chunk.group) == groups.end()) groups.push_back(chunk.group);
    }
    return groups;
}

void expectGroups(const FrameCaptureReader& reader, std::string& failures, const std::byte* page, const std::vector<std::uint64_t>& expected, const char* name) {
    const auto groups = deltaGroupsHolding(reader, page);
    if (groups == expected) return;
    failures += std::string(failures.empty() ? "" : "; ") + name + (groups.empty() ? " is in no memory delta" : " is in memory delta groups");
    for (const auto held : groups) failures += " " + std::to_string(held);
    failures += ", not only in group";
    for (const auto group : expected) failures += " " + std::to_string(group);
}

void testPagesCollectedOnce(const std::filesystem::path& directory) {
    constexpr std::size_t Size = 1u << 20;
    constexpr std::size_t Page = 4096;
    auto* bytes = allocate(Size);
    std::fill_n(bytes, Size, std::byte{0});
    auto* x = bytes + 2 * FrameCaptureWriter::BlockBytes;
    auto* y = bytes + 4 * FrameCaptureWriter::BlockBytes;
    auto* shader = reinterpret_cast<Shader*>(x + 2 * Page);
    *shader = {};
    shader->file_header = 0x34333231u;
    shader->version = 0x18u;
    shader->header_size = sizeof(Shader);
    auto* recorder = AgcDriver::FrameRecorder::Active();
    check(recorder != nullptr, "frame recorder is disabled");
    recorder->FrameBoundary();
    std::fill_n(x, Page, std::byte{0x31});
    AgcDriverSuspendPoint_nid_postfix();
    std::fill_n(x + FrameCaptureWriter::BlockBytes, Page, std::byte{0x4d});
    recorder->RegisterShader(shader);
    recorder->RegisterShader(shader);
    auto* late = allocate(Size);
    std::fill_n(late, Size, std::byte{0});
    auto* lateSource = late + 2 * FrameCaptureWriter::BlockBytes;
    std::fill_n(lateSource, Page, std::byte{0x57});
    std::fill_n(y, Page, std::byte{0x65});
    AgcDriverSuspendPoint_nid_postfix();
    AgcDriverSuspendPoint_nid_postfix();
    recorder->FrameBoundary();
    const FrameCaptureReader reader(directory);
    checkRanToEnd(reader);
    const auto first = groupBeforeSuspend(reader, 0);
    const auto second = groupBeforeSuspend(reader, 1);
    check(first != 0 && second != 0, "the CPU writes before the first two suspend points are not in memory deltas");
    std::string failures;
    expectGroups(reader, failures, x, {first}, "the page written before the first suspend point");
    expectGroups(reader, failures, y, {second}, "the page written before the second suspend point");
    expectGroups(reader, failures, lateSource, {second}, "the written page of the range committed during the capture");
    const auto& calls = reader.Calls();
    const auto registration = std::find_if(calls.begin(), calls.end(), [](const FrameCall& call) { return call.kind == FrameCallKind::RegisterShader; });
    if (registration == calls.end()) {
        failures += std::string(failures.empty() ? "" : "; ") + "the capture holds no registration of the shader";
    } else {
        const auto holding = deltaGroupsHolding(reader, x + FrameCaptureWriter::BlockBytes);
        if (registration == calls.begin() || (registration - 1)->kind != FrameCallKind::Memory || std::find(holding.begin(), holding.end(), (registration - 1)->values.at(0)) == holding.end()) failures += std::string(failures.empty() ? "" : "; ") + "the first registration of the shader has no memory delta holding the page written into the shader's next 64 KiB block before it";
        if (registration + 1 == calls.end() || (registration + 1)->kind != FrameCallKind::RegisterShader) failures += std::string(failures.empty() ? "" : "; ") + "the second registration of the shader collected its blocks again";
    }
    if (!failures.empty()) throw std::runtime_error(failures);
}

void testPieceSince(const std::filesystem::path& directory) {
    constexpr std::size_t Size = 1u << 20;
    constexpr std::size_t Page = 4096;
    constexpr std::size_t Block = FrameCaptureWriter::BlockBytes;
    auto* bytes = allocate(Size);
    std::fill_n(bytes, Size, std::byte{0});
    auto* stored = bytes;
    auto* partA = bytes + Block;
    auto* partB = bytes + Block + 4 * Page;
    auto* whole = bytes + 2 * Block;
    auto* recorder = AgcDriver::FrameRecorder::Active();
    check(recorder != nullptr, "frame recorder is disabled");
    recorder->FrameBoundary();
    {
        const AgcDriver::GuestMemory::StoreSerialScope serial(AgcDriver::GuestMemory::TitleCallStoreSerial);
        const std::vector<std::byte> driver(Page, std::byte{0x66});
        AgcDriver::GuestMemory::StoreOwnBytes(address(stored), driver);
    }
    std::fill_n(stored, Page, std::byte{0x77});
    std::fill_n(partA, Page, std::byte{0x11});
    std::fill_n(whole, Block, std::byte{0x22});
    AgcDriverSuspendPoint_nid_postfix();
    std::fill_n(partB, Page, std::byte{0x33});
    AgcDriverSuspendPoint_nid_postfix();
    auto* late = allocate(Size);
    std::fill_n(late, Size, std::byte{0});
    auto* lateC = late + Block;
    auto* lateD = late + Block + 4 * Page;
    auto* lateE = late + 3 * Block;
    std::fill_n(lateC, Page, std::byte{0x44});
    struct FlushHookReset {
        ~FlushHookReset() {
            AgcDriver::GuestMemory::SetFlushHook(nullptr);
            collectOnRead = 0;
        }
    } reset;
    collectedPage = lateE;
    collectOnRead = address(late + 4 * Block);
    AgcDriver::GuestMemory::SetFlushHook(&storeAndCollect);
    AgcDriverSuspendPoint_nid_postfix();
    AgcDriver::GuestMemory::SetFlushHook(nullptr);
    const bool hooked = collectOnRead.load() == 0;
    std::fill_n(lateD, Page, std::byte{0x55});
    AgcDriverSuspendPoint_nid_postfix();
    AgcDriverSuspendPoint_nid_postfix();
    recorder->FrameBoundary();
    const FrameCaptureReader reader(directory);
    checkRanToEnd(reader);
    std::array<std::uint64_t, 4> groups{};
    for (std::size_t index = 0; index < groups.size(); ++index) groups[index] = groupBeforeSuspend(reader, index);
    check(std::all_of(groups.begin(), groups.end(), [](std::uint64_t group) { return group != 0; }), "the CPU writes before the first four suspend points are not in memory deltas");
    check(hooked, "the delta that added the range committed during the capture did not read its fifth 64 KiB piece through the flush hook");
    std::string failures;
    expectGroups(reader, failures, stored, {groups[0], groups[1]}, "the page the driver stored into and the CPU then overwrote before the first suspend point, in the first 64 KiB piece of the allocation");
    expectGroups(reader, failures, partA, {groups[0]}, "the first written page of a 64 KiB piece written in part before the first suspend point");
    expectGroups(reader, failures, partB, {groups[1]}, "the page written later in the same piece before the second suspend point");
    expectGroups(reader, failures, whole, {groups[0]}, "the 64 KiB piece written whole before the first suspend point");
    expectGroups(reader, failures, whole + Block - Page, {groups[0]}, "the last page of the 64 KiB piece written whole before the first suspend point");
    expectGroups(reader, failures, lateC, {groups[2]}, "the page of a range committed during the capture written before the delta that added it");
    expectGroups(reader, failures, lateD, {groups[2], groups[3]}, "the page of that range written after the delta that added it, which writes every block of the range it adds");
    expectGroups(reader, failures, late + 5 * Block, {groups[2]}, "an all-zero 64 KiB piece of the range committed during the capture");
    const std::string lateEName = "the page of that range the CPU wrote, and another walk collected, while the delta that added the range read a later 64 KiB piece";
    std::uint64_t lateEGroup = 0;
    std::byte lateEByte{0};
    std::optional<std::byte> lateEAdded;
    for (const auto& chunk : reader.Chunks()) {
        if (chunk.kind != FrameMemoryKind::Delta || chunk.address > address(lateE) || address(lateE) + Page > chunk.address + chunk.bytes) continue;
        const auto value = reader.Read(chunk)[address(lateE) - chunk.address];
        if (chunk.group == groups[2]) lateEAdded = value;
        if (chunk.group < lateEGroup) continue;
        lateEGroup = chunk.group;
        lateEByte = value;
    }
    if (lateEAdded && *lateEAdded != std::byte{0}) failures += std::string(failures.empty() ? "" : "; ") + lateEName + " already has byte " + std::to_string(static_cast<int>(*lateEAdded)) + " in the delta that added the range (group " + std::to_string(groups[2]) + "), so the flush hook stored before that delta read the page";
    if (lateEByte != std::byte{0x6b}) failures += std::string(failures.empty() ? "" : "; ") + lateEName + (lateEGroup == 0 ? std::string(" is in no memory delta") : " has byte " + std::to_string(static_cast<int>(lateEByte)) + " in the last memory delta holding it (group " + std::to_string(lateEGroup) + "), not the 107 the CPU stored");
    else if (lateEGroup != groups[3]) failures += std::string(failures.empty() ? "" : "; ") + lateEName + " is last in memory delta group " + std::to_string(lateEGroup) + ", not in group " + std::to_string(groups[3]) + ", the delta after the one that added the range";
    if (!failures.empty()) throw std::runtime_error(failures);
}

void testDriverStoreAfterShaderDelta(const std::filesystem::path& directory) {
    constexpr std::size_t Size = 1u << 20;
    constexpr std::size_t Page = 4096;
    constexpr std::size_t Block = FrameCaptureWriter::BlockBytes;
    auto* bytes = allocate(Size);
    std::fill_n(bytes, Size, std::byte{0});
    auto* stored = bytes + Block;
    auto* other = bytes + 2 * Block;
    auto* shader = reinterpret_cast<Shader*>(bytes + 4 * Block);
    *shader = {};
    shader->file_header = 0x34333231u;
    shader->version = 0x18u;
    shader->header_size = sizeof(Shader);
    auto* recorder = AgcDriver::FrameRecorder::Active();
    check(recorder != nullptr, "frame recorder is disabled");
    recorder->FrameBoundary();
    {
        const AgcDriver::GuestMemory::StoreSerialScope serial(AgcDriver::GuestMemory::TitleCallStoreSerial);
        const std::vector<std::byte> driver(Page, std::byte{0x66});
        AgcDriver::GuestMemory::StoreOwnBytes(address(stored), driver);
    }
    std::fill_n(stored, Page, std::byte{0x77});
    recorder->RegisterShader(shader);
    AgcDriverSuspendPoint_nid_postfix();
    std::fill_n(other, Page, std::byte{0x88});
    AgcDriverSuspendPoint_nid_postfix();
    recorder->FrameBoundary();
    const FrameCaptureReader reader(directory);
    checkRanToEnd(reader);
    const auto first = groupBeforeSuspend(reader, 0);
    const auto second = groupBeforeSuspend(reader, 1);
    check(first != 0 && second != 0, "the CPU writes before the first two suspend points are not in memory deltas");
    std::string failures;
    expectGroups(reader, failures, stored, {first, second}, "the page the driver stored into and the CPU then overwrote before a shader registration and the first suspend point");
    if (!failures.empty()) throw std::runtime_error(failures);
}

void testPendingResultRead(const std::filesystem::path& directory) {
    constexpr std::size_t Size = 1u << 20;
    constexpr std::size_t Page = 4096;
    auto* bytes = allocate(Size);
    std::fill_n(bytes, Size, std::byte{0});
    auto* page = bytes + 2 * FrameCaptureWriter::BlockBytes;
    auto* result = reinterpret_cast<std::uint32_t*>(page + Page - sizeof(std::uint32_t));
    auto* recorder = AgcDriver::FrameRecorder::Active();
    check(recorder != nullptr, "frame recorder is disabled");
    recorder->FrameBoundary();
    struct FlushHookReset {
        ~FlushHookReset() {
            AgcDriver::GuestMemory::SetFlushHook(nullptr);
            pendingResult = nullptr;
        }
    } reset;
    AgcDriver::GuestMemory::SetFlushHook(&storePendingResult);
    std::fill_n(page, Page - sizeof(std::uint32_t), std::byte{0x3c});
    pendingResult = result;
    AgcDriverSuspendPoint_nid_postfix();
    auto* late = allocate(Size);
    std::fill_n(late, Size, std::byte{0});
    auto* latePage = late + 2 * FrameCaptureWriter::BlockBytes;
    auto* lateResult = reinterpret_cast<std::uint32_t*>(latePage + Page - sizeof(std::uint32_t));
    std::fill_n(latePage, Page - sizeof(std::uint32_t), std::byte{0x3c});
    pendingResult = lateResult;
    AgcDriverSuspendPoint_nid_postfix();
    recorder->FrameBoundary();
    AgcDriver::GuestMemory::SetFlushHook(nullptr);
    const FrameCaptureReader reader(directory);
    checkRanToEnd(reader);
    const auto first = groupBeforeSuspend(reader, 0);
    const auto second = groupBeforeSuspend(reader, 1);
    check(first != 0 && second != 0, "the CPU writes before the suspend points are not in memory deltas");
    std::string failures;
    const auto expectStored = [&](const std::uint32_t* word, std::uint64_t group, const char* name) {
        std::optional<std::uint32_t> held;
        for (const auto& chunk : reader.Chunks()) {
            if (chunk.kind != FrameMemoryKind::Delta || chunk.group != group || chunk.address > address(word) || address(word) + sizeof(std::uint32_t) > chunk.address + chunk.bytes) continue;
            const auto data = reader.Read(chunk);
            std::uint32_t value = 0;
            std::memcpy(&value, data.data() + (address(word) - chunk.address), sizeof(value));
            held = value;
        }
        if (held == 1u) return;
        failures += std::string(failures.empty() ? "" : "; ") + name + (held ? " read the page before the result pending over it was stored: it holds " + std::to_string(*held) + ", not 1" : std::string(" does not hold the page the CPU wrote"));
    };
    expectStored(result, first, "the memory delta before the first suspend point");
    expectStored(lateResult, second, "the memory delta that added the range committed during the capture");
    if (!failures.empty()) throw std::runtime_error(failures);
}

void testPartlyCoveredBlock(const std::filesystem::path& directory) {
    constexpr std::size_t Page = 4096;
    auto* bytes = allocate(4 * Page);
    std::fill_n(bytes, 4 * Page, std::byte{0x2b});
    auto* shader = reinterpret_cast<Shader*>(bytes + 2 * Page);
    *shader = {};
    shader->file_header = 0x34333231u;
    shader->version = 0x18u;
    shader->header_size = sizeof(Shader);
    auto* recorder = AgcDriver::FrameRecorder::Active();
    check(recorder != nullptr, "frame recorder is disabled");
    recorder->FrameBoundary();
    std::fill_n(bytes + Page, Page, std::byte{0x3c});
    recorder->RegisterShader(shader);
    recorder->FrameBoundary();
    const FrameCaptureReader reader(directory);
    checkRanToEnd(reader);
    const auto& calls = reader.Calls();
    const auto registration = std::find_if(calls.begin(), calls.end(), [](const FrameCall& call) { return call.kind == FrameCallKind::RegisterShader; });
    check(registration != calls.end() && registration != calls.begin() && (registration - 1)->kind == FrameCallKind::Memory, "the shader registration has no memory delta before it");
    const auto written = deltaGroupsHolding(reader, bytes + Page);
    check(std::find(written.begin(), written.end(), (registration - 1)->values.at(0)) != written.end(), "the page written during the capture, in the shader block the captured ranges cover only in part, is not in the shader registration's memory delta");
    const auto groups = deltaGroupsHolding(reader, bytes);
    if (groups.empty()) return;
    std::string failure = "the page written before the capture, in a shader block the captured ranges cover only in part, is in memory delta groups";
    for (const auto held : groups) failure += " " + std::to_string(held);
    throw std::runtime_error(failure);
}

void testLateClear(const std::filesystem::path& directory) {
    constexpr std::size_t Size = 1u << 20;
    constexpr std::size_t Page = 4096;
    auto* recorder = AgcDriver::FrameRecorder::Active();
    check(recorder != nullptr, "frame recorder is disabled");
    recorder->FrameBoundary();
    auto* late = allocate(Size);
    std::fill_n(late, Size, std::byte{0});
    auto* x = late + 2 * FrameCaptureWriter::BlockBytes;
    auto* shader = reinterpret_cast<Shader*>(x + 2 * Page);
    *shader = {};
    shader->file_header = 0x34333231u;
    shader->version = 0x18u;
    shader->header_size = sizeof(Shader);
    auto* neighbor = x + FrameCaptureWriter::BlockBytes;
    std::fill_n(neighbor, Page, std::byte{0x5a});
    recorder->RegisterShader(shader);
    std::fill_n(neighbor, Page, std::byte{0});
    AgcDriverSuspendPoint_nid_postfix();
    AgcDriverSuspendPoint_nid_postfix();
    recorder->FrameBoundary();
    const FrameCaptureReader reader(directory);
    checkRanToEnd(reader);
    std::uint64_t holding = 0;
    std::uint64_t lastGroup = 0;
    std::byte last{0};
    for (const auto& chunk : reader.Chunks()) {
        if (chunk.kind != FrameMemoryKind::Delta || chunk.address > address(neighbor) || address(neighbor) + Page > chunk.address + chunk.bytes) continue;
        ++holding;
        if (chunk.group < lastGroup) continue;
        lastGroup = chunk.group;
        last = reader.Read(chunk)[address(neighbor) - chunk.address];
    }
    check(holding != 0, "the neighbour of the registered shader is in no memory delta");
    if (last != std::byte{0}) throw std::runtime_error("the last memory delta holding the neighbour of the registered shader (group " + std::to_string(lastGroup) + " of " + std::to_string(holding) + " holding it) has byte " + std::to_string(static_cast<int>(last)) + ", not the 0 the CPU stored after the registration");
}

void testShaderBlockHole(const std::filesystem::path& directory) {
    constexpr std::size_t Block = FrameCaptureWriter::BlockBytes;
    constexpr std::size_t Page = 4096;
    auto* block = static_cast<std::byte*>(GuestArena::GuestArenaAllocate_nid_postfix(2 * Block, Block));
    check(block != nullptr, "guest reservation failed");
    GuestArena::GuestArenaRelease_nid_postfix(block, 2 * Block);
    auto* tail = block + 8 * Page;
    for (const auto& [piece, bytes] : {std::pair{block, 7 * Page}, std::pair{tail, Block - 8 * Page}}) {
        GuestArena::GuestArenaCommit_nid_postfix(piece, bytes, 0x04, bytes);
#ifdef _WIN32
        AgcDriver::GuestMemory::Unwatch(address(piece), bytes);
#endif
        GuestAllocations::Mutation mutation;
        mutation.Add(piece, bytes, true, true, false);
    }
    auto* shader = reinterpret_cast<Shader*>(tail + 0x2b0);
    *shader = {};
    shader->file_header = 0x34333231u;
    shader->version = 0x18u;
    shader->header_size = sizeof(Shader);
    auto* recorder = AgcDriver::FrameRecorder::Active();
    check(recorder != nullptr, "frame recorder is disabled");
    recorder->FrameBoundary();
    recorder->RegisterShader(shader);
    recorder->FrameBoundary();
    const FrameCaptureReader reader(directory);
    checkRanToEnd(reader);
    bool shaderDelta = false;
    for (const auto& chunk : reader.Chunks()) {
        if (chunk.kind != FrameMemoryKind::Delta) continue;
        check(insideRanges(reader, chunk), "a memory delta of the shader's block reaches a page that no range of the capture holds");
        shaderDelta = shaderDelta || (chunk.address <= address(shader) && address(shader) + sizeof(Shader) <= chunk.address + chunk.bytes);
    }
    check(shaderDelta, "the capture holds no memory delta with the registered shader");
}

void testDeltaOutsideRanges(const std::filesystem::path& directory) {
    constexpr std::size_t Block = FrameCaptureWriter::BlockBytes;
    constexpr std::size_t Page = 4096;
    auto* block = static_cast<std::byte*>(GuestArena::GuestArenaAllocate_nid_postfix(Block, Block));
    check(block != nullptr, "guest reservation failed");
    GuestArena::GuestArenaRelease_nid_postfix(block, Block);
    std::uintptr_t base = 0;
    std::size_t arenaSize = 0;
    GuestArena::GuestArenaRange_nid_postfix(&base, &arenaSize);
    const std::vector<std::byte> delta(Block, std::byte{0x5a});
    FrameCaptureWriter writer(directory);
    writer.WriteRange(address(block), 7 * Page, FrameRangeKind::Section, ReadWrite, FrameRangeAccess::Readable | FrameRangeAccess::Writable);
    writer.WriteRange(address(block) + 8 * Page, Block - 8 * Page, FrameRangeKind::Section, ReadWrite, FrameRangeAccess::Readable | FrameRangeAccess::Writable);
    writer.WriteDelta(2, 0, address(block), delta);
    writer.WriteCall(memoryCall(2, 1));
    writer.Finish("{\"arena_base\":" + std::to_string(base) + "}");
    const FrameCaptureReader reader(directory);
    std::string message;
    try {
        restoreMemory(reader);
    } catch (const std::exception& error) {
        message = error.what();
    }
    char hole[48];
    std::snprintf(hole, sizeof(hole), "reaches 0x%llx", static_cast<unsigned long long>(address(block) + 7 * Page));
    check(message.find(hole) != std::string::npos, ("the replay accepted a memory delta across a page that no range of the capture maps: " + message).c_str());
}

void writeInspectionCapture(const std::filesystem::path& directory) {
    constexpr std::uint64_t Commands = 0x1000000001ull;
    constexpr std::uint64_t Header = 0x2000000000ull;
    constexpr std::uint64_t Code = 0x2000010000ull;
    const std::array<std::uint32_t, 27> commands{
        0x00010000u, 0xc0053c00u, 0xc0064900u, 0x80000000u, 0xffff1000u,
        0xc0053c00u, 0x13u, 0x12345678u, 0x9abcdef0u, 0x10203040u, 0xf0f0f0f0u, 4u,
        0xc0064900u, 1u, 2u, 3u, 4u, 5u, 6u, 7u,
        0xc0033700u, 0x500u, 0xdeadbeefu, 0x12u, 0x1337u,
        0x00030000u, 0xc0053c00u};
    std::array<std::uint8_t, 16> code{0, 1, 2, 3, 4, 5, 6, 7, 255, 128, 13, 10, 39, 36, 38, 0};
    Shader shader{};
    shader.code = reinterpret_cast<const void*>(static_cast<std::uintptr_t>(Code));
    shader.shader_size = code.size();
    FrameCaptureWriter writer(directory);
    writer.WriteRange(Commands, 6, FrameRangeKind::Host, ReadWrite, FrameRangeAccess::Readable);
    writer.WriteRange(Commands + 6, sizeof(commands) - 6, FrameRangeKind::Host, ReadWrite, FrameRangeAccess::Readable);
    writer.WriteRange(Header, sizeof(shader), FrameRangeKind::Host, ReadWrite, FrameRangeAccess::Readable);
    writer.WriteRange(Code, code.size(), FrameRangeKind::Host, ReadWrite, FrameRangeAccess::Readable);
    writer.WriteSnapshot(Header, std::as_bytes(std::span(&shader, 1)));
    writer.WriteSnapshot(Code, std::as_bytes(std::span(code)));
    writer.WriteDelta(1, 0, Commands, std::as_bytes(std::span(commands)));
    const std::uint32_t reference = 0x55667788u;
    writer.WriteDelta(2, 0, Commands + 9 * sizeof(std::uint32_t), std::as_bytes(std::span(&reference, 1)));
    code[0] = 0x42;
    writer.WriteDelta(3, 0, Code, std::as_bytes(std::span(code)));
    FrameCall call;
    const auto write = [&](FrameCallKind kind, std::uint64_t location, std::uint64_t size, std::vector<std::uint64_t> values = {}) {
        call.kind = kind;
        ++call.ordinal;
        call.address = location;
        call.size = size;
        call.values = std::move(values);
        call.thread = 7;
        call.queue = 0x42;
        call.accepted = 12;
        call.completed = 9;
        writer.WriteCall(call);
    };
    write(FrameCallKind::Submit, Commands, commands.size());
    write(FrameCallKind::Memory, 0, 0, {1});
    write(FrameCallKind::RegisterShader, Header, 0);
    write(FrameCallKind::FrameStart, 0, 0);
    write(FrameCallKind::Submit, Commands, commands.size());
    write(FrameCallKind::Memory, 0, 0, {2});
    write(FrameCallKind::Submit, Commands, commands.size());
    write(FrameCallKind::RegisterShader, Header, 0);
    write(FrameCallKind::Memory, 0, 0, {3});
    write(FrameCallKind::RegisterShader, Header, 0);
    write(FrameCallKind::Flip, 0, 0);
    write(FrameCallKind::ReserveEopFlip, 0, 0);
    write(FrameCallKind::Submit, 0x3000000000ull, 1);
    write(FrameCallKind::Submit, Commands, std::numeric_limits<std::uint64_t>::max());
    write(FrameCallKind::Submit, std::numeric_limits<std::uint64_t>::max() - 1, 1);
    write(FrameCallKind::Submit, Commands, 0);
    writer.Finish("{}");
}

std::uint32_t low(const void* pointer) {
    return static_cast<std::uint32_t>(address(pointer));
}

std::uint32_t high(const void* pointer) {
    return static_cast<std::uint32_t>(address(pointer) >> 32u);
}

bool copySourcePending(std::uint64_t source, std::size_t bytes) {
    using AgcDriver::Graphics::Recorder;
    constexpr auto kind = static_cast<std::size_t>(Recorder::ReadKind::CopySource);
    const auto before = Recorder::ReadCounts().hits[kind];
    std::lock_guard gpuLock(AgcDriver::GuestMemory::GpuMutex());
    const auto* const recorder = Recorder::Active();
    return recorder != nullptr && recorder->PendingReadOverlaps(source, bytes) && Recorder::ReadCounts().hits[kind] > before;
}

bool readInOpenBatch(std::uint64_t range, std::size_t bytes) {
    std::lock_guard gpuLock(AgcDriver::GuestMemory::GpuMutex());
    const auto* const recorder = AgcDriver::Graphics::Recorder::Active();
    const auto reading = recorder != nullptr ? recorder->DescribePendingRead(range, bytes) : std::nullopt;
    return reading.has_value() && reading->open;
}

bool readStillPending(std::uint64_t range, std::size_t bytes) {
    std::lock_guard gpuLock(AgcDriver::GuestMemory::GpuMutex());
    auto* const recorder = AgcDriver::Graphics::Recorder::Active();
    const auto reading = recorder != nullptr ? recorder->DescribePendingRead(range, bytes) : std::nullopt;
    return reading.has_value() && !reading->signaled && (reading->open || !recorder->WaitSerialFor(reading->serial, 0));
}

void openDevice() {
    try {
        AgcDriverWaitForPipelineJournal_nid_postfix();
    } catch (const std::exception& error) {
        if (std::getenv("ANYPS5_REQUIRE_VULKAN") != nullptr) throw;
        throw Inconclusive(std::string("no usable Vulkan device: ") + error.what());
    }
}

void testGpuGate(const std::filesystem::path& directory, bool inFlight) {
    constexpr std::size_t BulkBytes = std::size_t{48} << 20u;
    constexpr std::size_t CopyBytes = 128u * 1024u;
    constexpr std::uint32_t DmaHeader = 0xc0055000u;
    constexpr std::uint32_t DmaMemoryToMemory = 0x80000000u;
    const std::size_t bulks = inFlight ? 0 : 4;
    openDevice();
    auto* bulk = bulks == 0 ? nullptr : allocate(BulkBytes * (bulks + 1));
    auto* source = allocate(CopyBytes);
    auto* target = allocate(CopyBytes);
    auto* words = reinterpret_cast<std::uint32_t*>(allocate(65536, false));
    if (bulk != nullptr) std::fill_n(bulk, BulkBytes, std::byte{0x33});
    std::fill_n(source, CopyBytes, std::byte{0x11});
    std::fill_n(target, CopyBytes, std::byte{0});
    std::size_t used = 0;
    const auto dma = [&](const std::byte* from, const std::byte* to, std::size_t bytes) {
        const std::array<std::uint32_t, 7> packet{DmaHeader, DmaMemoryToMemory, low(from), high(from), low(to), high(to), static_cast<std::uint32_t>(bytes)};
        std::copy(packet.begin(), packet.end(), words + used);
        used += packet.size();
    };
    for (std::size_t copy = 1; copy <= bulks; ++copy) dma(bulk, bulk + copy * BulkBytes, BulkBytes);
    dma(source, target, CopyBytes);
    const std::vector<std::byte> rewritten(CopyBytes, std::byte{0x22});
    FrameCaptureWriter writer(directory);
    writer.WriteDelta(1, 0, address(source), rewritten);
    writer.Finish("{}");
    const FrameCaptureReader reader(directory);
    Options options;
    options.release = false;
    if (inFlight) options.busyTimeout = std::chrono::milliseconds(20000);
    Replay replay{options, reader};
    replay.recording = true;
    collectGroups(replay);
    FrameCall submit;
    submit.kind = FrameCallKind::Submit;
    submit.thread = 1;
    submit.address = address(words);
    submit.size = used;
    replay.execute(submit);
    check(AgcDriverHoldSettled_nid_postfix(10000), "the copies did not complete on the CPU");
    const auto [accepted, completed] = progress();
    const bool pending = copySourcePending(address(source), CopyBytes);
    const bool open = !inFlight || readInOpenBatch(address(source), CopyBytes);
    AgcDriverReleaseSettled_nid_postfix();
    if (!pending || !open) {
        AgcDriverWaitIdle_nid_postfix();
        throw Inconclusive(pending ? "inconclusive: the GPU copy that reads the restored range was not in the open batch after its submission completed on the CPU" : "inconclusive: no GPU copy read the restored range in place after its submission completed on the CPU");
    }
    if (inFlight) {
        check(accepted != 0 && completed == accepted, "a submission was still in flight after the copy's submission completed on the CPU");
        auto call = memoryCall(1, 1);
        call.ordinal = 1;
        call.flags = AgcDriver::InFlightDeltaFlag;
        call.accepted = accepted;
        call.completed = accepted - 1;
        call.values = {1, accepted};
        replay.runWindow(std::span(&call, 1));
    } else {
        replay.execute(memoryCall(1, 1));
    }
    check(!readStillPending(address(source), CopyBytes), inFlight ? "a delta read in flight was restored while a GPU batch that reads its range had not finished" : "the held delta was restored while a GPU batch that reads its range had not finished");
    AgcDriverWaitIdle_nid_postfix();
    check(std::equal(rewritten.begin(), rewritten.end(), source), inFlight ? "the delta read in flight was not restored" : "the held delta was not restored");
    check(std::all_of(target, target + CopyBytes, [](std::byte value) { return value == std::byte{0x11}; }), inFlight ? "the delta read in flight was restored before the GPU copy that reads its range ran" : "the held delta was restored before the GPU copy that reads its range ran");
}

void testGpuGateParked(const std::filesystem::path& directory) {
    constexpr std::size_t CopyBytes = 128u * 1024u;
    openDevice();
    auto* source = allocate(CopyBytes);
    auto* target = allocate(CopyBytes);
    auto* words = reinterpret_cast<std::uint32_t*>(allocate(65536, false));
    auto* gate = words + 64;
    std::fill_n(source, CopyBytes, std::byte{0x11});
    std::fill_n(target, CopyBytes, std::byte{0});
    *gate = 0;
    const std::array<std::uint32_t, 14> packets{0xc0055000u, 0x80000000u, low(source), high(source), low(target), high(target), static_cast<std::uint32_t>(CopyBytes), 0xc0053c00u, 0x13u, low(gate), high(gate), 1u, 0xffffffffu, 4u};
    std::copy(packets.begin(), packets.end(), words);
    const std::vector<std::byte> rewritten(CopyBytes, std::byte{0x22});
    const std::uint32_t ready = 1;
    FrameCaptureWriter writer(directory);
    writer.WriteDelta(1, 0, address(source), rewritten);
    writer.WriteDelta(1, 0, address(gate), std::as_bytes(std::span(&ready, 1)));
    writer.Finish("{}");
    const FrameCaptureReader reader(directory);
    Options options;
    options.release = false;
    options.strict = true;
    options.busyTimeout = std::chrono::milliseconds(10000);
    Replay replay{options, reader};
    replay.recording = true;
    collectGroups(replay);
    FrameCall submit;
    submit.kind = FrameCallKind::Submit;
    submit.thread = 1;
    submit.address = address(words);
    submit.size = packets.size();
    replay.execute(submit);
    check(AgcDriverHoldSettled_nid_postfix(10000), "the submission did not settle on its title wait");
    const auto [accepted, completed] = progress();
    std::array<AgcDriver::AwaitedWait, 64> awaited{};
    const auto count = AgcDriverAwaitedWaits_nid_postfix(awaited.data(), awaited.size());
    const bool parked = std::any_of(awaited.begin(), awaited.begin() + static_cast<std::ptrdiff_t>(count), [&](const AgcDriver::AwaitedWait& slot) { return slot.address == address(gate); });
    const bool pending = copySourcePending(address(source), CopyBytes);
    const bool open = readInOpenBatch(address(source), CopyBytes);
    AgcDriverReleaseSettled_nid_postfix();
    if (accepted <= completed || !parked || !pending || !open) {
        std::atomic_ref<std::uint32_t>(*gate).store(1, std::memory_order_release);
        AgcDriverWaitIdle_nid_postfix();
        check(accepted > completed, "the submission completed before the hold settled, so the hold did not settle on its title wait");
        check(parked, "the settled submission was not parked on its title wait");
        throw Inconclusive(pending ? "inconclusive: the GPU copy that reads the restored range was not in the open batch while its submission waited on the title" : "inconclusive: no GPU copy read the restored range in place while its submission waited on the title");
    }
    replay.execute(memoryCall(1, 1));
    check(!readStillPending(address(source), CopyBytes), "the held delta was restored while its submission waited on the title and a GPU batch that reads its range had not finished");
    AgcDriverWaitIdle_nid_postfix();
    check(std::equal(rewritten.begin(), rewritten.end(), source), "the held delta was not restored");
    check(std::all_of(target, target + CopyBytes, [](std::byte value) { return value == std::byte{0x11}; }), "the held delta was restored before the GPU copy that reads its range ran");
}

void testCompletedGate(const std::filesystem::path& directory) {
    constexpr std::uint32_t ComputeQueue = 0x20;
    constexpr std::size_t CopyBytes = 16;
    auto* bytes = allocate(FrameCaptureWriter::BlockBytes);
    auto* gate = reinterpret_cast<std::uint32_t*>(bytes);
    auto* compute = reinterpret_cast<std::uint32_t*>(bytes + 4096);
    auto* nop = reinterpret_cast<std::uint32_t*>(bytes + 4352);
    auto* source = bytes + 8192;
    auto* target = bytes + 8256;
    *gate = 0;
    std::fill_n(source, CopyBytes, std::byte{0x11});
    std::fill_n(target, CopyBytes, std::byte{0});
    const std::array<std::uint32_t, 14> gated{0xc0053c00u, 0x13u, low(gate), high(gate), 1u, 0xffffffffu, 4u, 0xc0055000u, 0x80000000u, low(source), high(source), low(target), high(target), static_cast<std::uint32_t>(CopyBytes)};
    std::copy(gated.begin(), gated.end(), compute);
    const std::array<std::uint32_t, 2> empty{0xc0001000u, 0u};
    std::copy(empty.begin(), empty.end(), nop);
    const std::vector<std::byte> rewritten(CopyBytes, std::byte{0x22});
    FrameCaptureWriter writer(directory);
    writer.WriteDelta(1, 0, address(source), rewritten);
    writer.Finish("{}");
    const FrameCaptureReader reader(directory);
    Options options;
    options.release = false;
    options.busyTimeout = std::chrono::milliseconds(20000);
    Replay replay{options, reader};
    replay.recording = true;
    collectGroups(replay);
    const auto before = progress().first;
    const Packet packet{compute, static_cast<std::uint32_t>(gated.size()), 0, {}};
    check(sceAgcDriverSubmitAcb(ComputeQueue, &packet) == 0, "the compute submission failed");
    const auto parked = [&] {
        std::array<AgcDriver::AwaitedWait, 64> awaited{};
        const auto count = AgcDriverAwaitedWaits_nid_postfix(awaited.data(), awaited.size());
        return std::any_of(awaited.begin(), awaited.begin() + static_cast<std::ptrdiff_t>(count), [&](const AgcDriver::AwaitedWait& slot) { return slot.address == address(gate); });
    };
    const auto deadline = Clock::now() + std::chrono::seconds(10);
    while (!parked()) {
        check(Clock::now() < deadline, "the compute submission did not wait on its unwritten gate");
        std::this_thread::yield();
    }
    AgcDriverSubmitGraphics_nid_postfix(nop, static_cast<std::uint32_t>(empty.size()));
    while (!AgcDriverSerialDone_nid_postfix(before + 2)) {
        check(Clock::now() < deadline, "the graphics submission did not complete while the compute submission before it waited on its gate");
        std::this_thread::yield();
    }
    check(progress() == std::pair{before + 2, before} && !AgcDriverSerialDone_nid_postfix(before + 1), "the graphics submission did not complete ahead of the compute submission that waits on its gate");
    std::uint32_t sourceAtGate = 0;
    std::thread title([&] {
        const auto limit = Clock::now() + std::chrono::seconds(1);
        do {
            sourceAtGate = std::atomic_ref<std::uint32_t>(*reinterpret_cast<std::uint32_t*>(source)).load(std::memory_order_acquire);
            std::this_thread::yield();
        } while (sourceAtGate != 0x22222222u && Clock::now() < limit);
        std::atomic_ref<std::uint32_t>(*gate).store(1, std::memory_order_release);
    });
    auto call = memoryCall(1, 1);
    call.ordinal = 1;
    call.flags = AgcDriver::InFlightDeltaFlag;
    call.accepted = 2;
    call.completed = 1;
    call.values = {1, 2};
    std::string failure;
    try {
        replay.runWindow(std::span(&call, 1));
    } catch (const std::exception& error) {
        failure = error.what();
    }
    title.join();
    check(failure.empty(), failure.c_str());
    AgcDriverWaitIdle_nid_postfix();
    check(sourceAtGate == 0x11111111u, "the in-flight delta was restored before a submission the capture had completed ran");
    check(std::all_of(target, target + CopyBytes, [](std::byte value) { return value == std::byte{0x11}; }), "the copy of a submission the capture had completed read the bytes of a delta read in flight after it");
    check(std::equal(rewritten.begin(), rewritten.end(), source), "the delta read in flight was not restored");
}

void testCompletedGateFlip(const std::filesystem::path& directory) {
    constexpr std::uint64_t Base = 40;
    constexpr std::uint32_t ComputeQueue = 0x20;
    constexpr std::size_t CopyBytes = 16;
    if (APS5_ENABLE_TIMING_LOG) return;
    const char* dump = std::getenv("APS5_DUMP_SUBMITS");
    check(std::getenv("APS5_CPU_FLIP_IN_ORDER") == nullptr && std::getenv("APS5_SYNC_FLIP") == nullptr && (dump == nullptr || *dump == '\0'), "the environment orders CPU flips or captures submissions, so the replay's CPU flip would take a serial");
    auto* bytes = allocate(FrameCaptureWriter::BlockBytes);
    auto* gate = reinterpret_cast<std::uint32_t*>(bytes);
    auto* compute = reinterpret_cast<std::uint32_t*>(bytes + 4096);
    auto* nop = reinterpret_cast<std::uint32_t*>(bytes + 4352);
    auto* source = bytes + 8192;
    auto* target = bytes + 8256;
    *gate = 0;
    std::fill_n(source, CopyBytes, std::byte{0x11});
    std::fill_n(target, CopyBytes, std::byte{0});
    const std::array<std::uint32_t, 14> gated{0xc0053c00u, 0x13u, low(gate), high(gate), 1u, 0xffffffffu, 4u, 0xc0055000u, 0x80000000u, low(source), high(source), low(target), high(target), static_cast<std::uint32_t>(CopyBytes)};
    std::copy(gated.begin(), gated.end(), compute);
    const std::array<std::uint32_t, 2> empty{0xc0001000u, 0u};
    std::copy(empty.begin(), empty.end(), nop);
    const std::vector<std::byte> rewritten(CopyBytes, std::byte{0x22});
    FrameCaptureWriter writer(directory);
    writer.WriteDelta(1, 0, address(source), rewritten);
    FrameCall start;
    start.kind = FrameCallKind::FrameStart;
    start.accepted = Base;
    start.completed = Base;
    writer.WriteCall(start);
    writer.Finish("{}");
    const FrameCaptureReader reader(directory);
    Options options;
    options.release = false;
    options.busyTimeout = std::chrono::milliseconds(20000);
    Replay replay{options, reader};
    replay.recording = true;
    collectGroups(replay);
    auto output = std::make_shared<ReplayOutput>(replay.frames);
    AgcDriverRegisterVideoOutput_nid_postfix(7, output);
    FrameCall parked;
    parked.kind = FrameCallKind::Submit;
    parked.ordinal = 1;
    parked.thread = 1;
    parked.queue = ComputeQueue;
    parked.address = address(compute);
    parked.size = gated.size();
    parked.accepted = Base;
    parked.completed = Base;
    parked.values = {Base + 1};
    FrameCall flip;
    flip.kind = FrameCallKind::Flip;
    flip.ordinal = 2;
    flip.thread = 1;
    flip.cpuFlip = true;
    flip.accepted = Base + 1;
    flip.completed = Base;
    flip.values = {7, static_cast<std::uint64_t>(-1), 1, 0, Base + 2};
    FrameCall graphics;
    graphics.kind = FrameCallKind::Submit;
    graphics.ordinal = 3;
    graphics.thread = 1;
    graphics.address = address(nop);
    graphics.size = empty.size();
    graphics.accepted = Base + 2;
    graphics.completed = Base;
    graphics.values = {Base + 3};
    auto delta = memoryCall(1, 1);
    delta.ordinal = 4;
    delta.flags = AgcDriver::InFlightDeltaFlag;
    delta.accepted = Base + 3;
    delta.completed = Base + 1;
    delta.values = {1, Base + 2, Base + 3};
    std::uint32_t sourceAtGate = 0;
    std::thread title([&] {
        const auto limit = Clock::now() + std::chrono::seconds(1);
        do {
            sourceAtGate = std::atomic_ref<std::uint32_t>(*reinterpret_cast<std::uint32_t*>(source)).load(std::memory_order_acquire);
            std::this_thread::yield();
        } while (sourceAtGate != 0x22222222u && Clock::now() < limit);
        std::atomic_ref<std::uint32_t>(*gate).store(1, std::memory_order_release);
    });
    const std::array calls{parked, flip, graphics, delta};
    std::string failure;
    try {
        replay.runWindow(calls);
    } catch (const std::exception& error) {
        failure = error.what();
    }
    title.join();
    AgcDriverWaitIdle_nid_postfix();
    AgcDriverUnregisterVideoOutput_nid_postfix(7, output);
    check(failure.empty(), failure.c_str());
    check(replay.frames.Times().size() == 1, "the replay did not present the CPU flip that took a serial only in the capture");
    check(sourceAtGate == 0x11111111u, "the in-flight delta was restored before a submission the capture had completed ran, when a CPU flip after that submission took a serial only in the capture");
    check(std::all_of(target, target + CopyBytes, [](std::byte value) { return value == std::byte{0x11}; }), "the copy of a submission the capture had completed before a CPU flip that took a serial only in the capture read the bytes of a delta read in flight after it");
    check(std::equal(rewritten.begin(), rewritten.end(), source), "the delta read in flight was not restored");
}

void waitDone(std::uint64_t serial, const char* reason) {
    const auto deadline = Clock::now() + std::chrono::seconds(10);
    while (!AgcDriverSerialDone_nid_postfix(serial)) {
        check(Clock::now() < deadline, reason);
        std::this_thread::yield();
    }
}

bool parkedOn(const void* dword) {
    std::array<AgcDriver::AwaitedWait, 64> awaited{};
    const auto count = AgcDriverAwaitedWaits_nid_postfix(awaited.data(), awaited.size());
    return std::any_of(awaited.begin(), awaited.begin() + static_cast<std::ptrdiff_t>(count), [&](const AgcDriver::AwaitedWait& slot) { return slot.address == address(dword); });
}

struct GpuEpochTracking {
    GpuEpochTracking() {
        AgcDriverTrackGpuEpochs_nid_postfix(true);
    }
    ~GpuEpochTracking() {
        AgcDriverTrackGpuEpochs_nid_postfix(false);
    }
    GpuEpochTracking(const GpuEpochTracking&) = delete;
    GpuEpochTracking& operator=(const GpuEpochTracking&) = delete;
};

class BatchHold {
public:
    explicit BatchHold(VkDevice device) : device(device) {
#ifdef _WIN32
        constexpr const char* libraryName = "vulkan-1.dll";
#elif defined(__APPLE__)
        constexpr const char* libraryName = "libvulkan.1.dylib";
#else
        constexpr const char* libraryName = "libvulkan.so.1";
#endif
        library.reset(SDL_LoadObject(libraryName));
        check(library != nullptr, "cannot load Vulkan to hold a recorder batch");
        resolve = reinterpret_cast<PFN_vkGetDeviceProcAddr>(SDL_LoadFunction(library.get(), "vkGetDeviceProcAddr"));
        check(resolve != nullptr, "cannot resolve vkGetDeviceProcAddr to hold a recorder batch");
        const auto createSemaphore = function<PFN_vkCreateSemaphore>("vkCreateSemaphore");
        destroySemaphore = function<PFN_vkDestroySemaphore>("vkDestroySemaphore");
        signalSemaphore = reinterpret_cast<PFN_vkSignalSemaphore>(resolve(device, "vkSignalSemaphoreKHR"));
        if (signalSemaphore == nullptr) signalSemaphore = function<PFN_vkSignalSemaphore>("vkSignalSemaphore");
        VkSemaphoreTypeCreateInfo type{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
        type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        VkSemaphoreCreateInfo info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &type};
        check(createSemaphore(device, &info, nullptr, &semaphore) == VK_SUCCESS, "cannot create the semaphore that holds a recorder batch");
    }

    ~BatchHold() {
        Release();
        try {
            AgcDriverWaitIdle_nid_postfix();
        } catch (...) {
        }
        destroySemaphore(device, semaphore, nullptr);
    }

    BatchHold(const BatchHold&) = delete;
    BatchHold& operator=(const BatchHold&) = delete;

    void SubmitHeld(AgcDriver::Graphics::Recorder& recorder) {
        recorder.HoldNextSubmit(semaphore, 1);
        recorder.Submit();
    }

    void Release() {
        if (released.exchange(true)) return;
        VkSemaphoreSignalInfo info{VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO};
        info.semaphore = semaphore;
        info.value = 1;
        releaseResult = signalSemaphore(device, &info);
    }

    VkResult ReleaseResult() const {
        return releaseResult;
    }

private:
    template <typename TFunction>
    TFunction function(const char* name) const {
        const auto function = reinterpret_cast<TFunction>(resolve(device, name));
        check(function != nullptr, "cannot resolve a Vulkan function to hold a recorder batch");
        return function;
    }

    VkDevice device;
    std::unique_ptr<void, decltype(&SDL_UnloadObject)> library{nullptr, &SDL_UnloadObject};
    PFN_vkGetDeviceProcAddr resolve = nullptr;
    PFN_vkDestroySemaphore destroySemaphore = nullptr;
    PFN_vkSignalSemaphore signalSemaphore = nullptr;
    VkSemaphore semaphore = VK_NULL_HANDLE;
    std::atomic<bool> released{false};
    VkResult releaseResult = VK_NOT_READY;
};

VkDevice recorderDevice() {
    std::lock_guard gpuLock(AgcDriver::GuestMemory::GpuMutex());
    const auto* const recorder = AgcDriver::Graphics::Recorder::Active();
    check(recorder != nullptr, "the device has no active recorder");
    return recorder->Device();
}

void testGpuGateQueues() {
    constexpr std::uint32_t ComputeQueue = 0x20;
    constexpr std::size_t CopyBytes = 128u * 1024u;
    openDevice();
    const GpuEpochTracking tracking;
    auto* source = allocate(CopyBytes);
    auto* target = allocate(CopyBytes);
    auto* words = reinterpret_cast<std::uint32_t*>(allocate(65536, false));
    auto* nop = words + 32;
    auto* gate = words + 64;
    std::fill_n(source, CopyBytes, std::byte{0x11});
    std::fill_n(target, CopyBytes, std::byte{0});
    *gate = 0;
    struct OpenGate {
        std::uint32_t* gate;
        ~OpenGate() {
            std::atomic_ref<std::uint32_t>(*gate).store(1, std::memory_order_release);
        }
    } openGate{gate};
    const std::array<std::uint32_t, 14> gated{0xc0053c00u, 0x13u, low(gate), high(gate), 1u, 0xffffffffu, 4u, 0xc0055000u, 0x80000000u, low(source), high(source), low(target), high(target), static_cast<std::uint32_t>(CopyBytes)};
    std::copy(gated.begin(), gated.end(), words);
    const std::array<std::uint32_t, 2> empty{0xc0001000u, 0u};
    std::copy(empty.begin(), empty.end(), nop);
    const auto before = progress().first;
    const Packet packet{words, static_cast<std::uint32_t>(gated.size()), 0, {}};
    check(sceAgcDriverSubmitAcb(ComputeQueue, &packet) == 0, "the compute submission failed");
    const auto parkDeadline = Clock::now() + std::chrono::seconds(10);
    while (!parkedOn(gate)) {
        check(Clock::now() < parkDeadline, "the compute submission did not wait on its unwritten gate");
        std::this_thread::yield();
    }
    AgcDriverSubmitGraphics_nid_postfix(nop, static_cast<std::uint32_t>(empty.size()));
    waitDone(before + 2, "the graphics submission did not complete while the compute submission before it waited on its gate");
    check(!AgcDriverSerialDone_nid_postfix(before + 1), "the graphics submission did not complete ahead of the compute submission that waits on its gate");
    check(AgcDriverWaitRecordedOnGpuFor_nid_postfix(10000), "the GPU work recorded before the gate opened did not finish");
    std::atomic_ref<std::uint32_t>(*gate).store(1, std::memory_order_release);
    const auto runDeadline = Clock::now() + std::chrono::seconds(10);
    while (progress() != std::pair{before + 2, before + 2}) {
        check(Clock::now() < runDeadline, "the compute submission did not complete after its gate opened");
        std::this_thread::yield();
    }
    const bool pending = copySourcePending(address(source), CopyBytes);
    const bool open = readInOpenBatch(address(source), CopyBytes);
    if (!pending || !open) {
        AgcDriverWaitIdle_nid_postfix();
        throw Inconclusive(pending ? "inconclusive: the compute copy that reads its source was not in the open batch after its submission completed on the CPU" : "inconclusive: no GPU copy read the compute submission's source in place after it completed on the CPU");
    }
    AgcDriver::GpuSerialsWait outcome;
    check(AgcDriverWaitSerialsOnGpuFor_nid_postfix(before + 2, nullptr, 0, 10000, &outcome), "the GPU work of the completed submissions did not finish");
    check(!readStillPending(address(source), CopyBytes), "a GPU wait through a submission returned before the GPU ran an earlier submission that completed after it on another queue");
    std::fill_n(source, CopyBytes, std::byte{0x22});
    AgcDriverWaitIdle_nid_postfix();
    check(std::all_of(target, target + CopyBytes, [](std::byte value) { return value == std::byte{0x11}; }), "the compute copy read its source after a GPU wait through its submission returned");
}

void testGpuGateDepth(const std::filesystem::path& directory) {
    constexpr std::size_t CopyBytes = 128u * 1024u;
    openDevice();
    auto* bytes = allocate(CopyBytes * 4);
    auto* first = bytes;
    auto* firstTarget = bytes + CopyBytes;
    auto* second = bytes + CopyBytes * 2;
    auto* secondTarget = bytes + CopyBytes * 3;
    auto* words = reinterpret_cast<std::uint32_t*>(allocate(65536, false));
    std::fill_n(first, CopyBytes, std::byte{0x11});
    std::fill_n(firstTarget, CopyBytes, std::byte{0});
    std::fill_n(second, CopyBytes, std::byte{0x44});
    std::fill_n(secondTarget, CopyBytes, std::byte{0});
    const std::array<std::uint32_t, 7> firstCopy{0xc0055000u, 0x80000000u, low(first), high(first), low(firstTarget), high(firstTarget), static_cast<std::uint32_t>(CopyBytes)};
    const std::array<std::uint32_t, 7> secondCopy{0xc0055000u, 0x80000000u, low(second), high(second), low(secondTarget), high(secondTarget), static_cast<std::uint32_t>(CopyBytes)};
    std::copy(firstCopy.begin(), firstCopy.end(), words);
    std::copy(secondCopy.begin(), secondCopy.end(), words + 16);
    const std::vector<std::byte> rewritten(CopyBytes, std::byte{0x22});
    FrameCaptureWriter writer(directory);
    writer.WriteDelta(1, 0, address(first), rewritten);
    writer.Finish("{}");
    const FrameCaptureReader reader(directory);
    Options options;
    options.release = false;
    options.busyTimeout = std::chrono::milliseconds(20000);
    Replay replay{options, reader};
    replay.recording = true;
    collectGroups(replay);
    BatchHold hold(recorderDevice());
    const auto before = progress().first;
    AgcDriverSubmitGraphics_nid_postfix(words, static_cast<std::uint32_t>(firstCopy.size()));
    waitDone(before + 1, "the first copy's submission did not complete on the CPU");
    const bool firstPending = copySourcePending(address(first), CopyBytes);
    if (!firstPending || !readInOpenBatch(address(first), CopyBytes)) {
        AgcDriverWaitIdle_nid_postfix();
        throw Inconclusive(firstPending ? "inconclusive: the first copy was not in the open batch after its submission completed on the CPU" : "inconclusive: no GPU copy read the restored range in place after its submission completed on the CPU");
    }
    {
        std::lock_guard gpuLock(AgcDriver::GuestMemory::GpuMutex());
        auto* const recorder = AgcDriver::Graphics::Recorder::Active();
        check(recorder != nullptr, "the device has no active recorder");
        hold.SubmitHeld(*recorder);
    }
    AgcDriverSubmitGraphics_nid_postfix(words + 16, static_cast<std::uint32_t>(secondCopy.size()));
    waitDone(before + 2, "the second copy's submission did not complete on the CPU while the first copy's batch was held");
    check(readStillPending(address(first), CopyBytes) && !readInOpenBatch(address(first), CopyBytes), "the first copy's batch was not held on the GPU after its submit");
    const bool secondPending = copySourcePending(address(second), CopyBytes);
    if (!secondPending || !readInOpenBatch(address(second), CopyBytes)) throw Inconclusive(secondPending ? "inconclusive: the second copy was not in the open batch after its submission completed on the CPU" : "inconclusive: no GPU copy read the second source in place after its submission completed on the CPU");
    std::uint32_t firstAtRelease = 0;
    std::thread title([&] {
        const auto limit = Clock::now() + std::chrono::seconds(1);
        do {
            firstAtRelease = std::atomic_ref<std::uint32_t>(*reinterpret_cast<std::uint32_t*>(first)).load(std::memory_order_acquire);
            std::this_thread::yield();
        } while (firstAtRelease != 0x22222222u && Clock::now() < limit);
        hold.Release();
    });
    auto call = memoryCall(1, 1);
    call.ordinal = 1;
    call.flags = AgcDriver::InFlightDeltaFlag;
    call.accepted = 2;
    call.completed = 1;
    call.values = {1};
    std::string failure;
    try {
        replay.runWindow(std::span(&call, 1));
    } catch (const std::exception& error) {
        failure = error.what();
    }
    title.join();
    const bool drained = !readInOpenBatch(address(second), CopyBytes);
    AgcDriverWaitIdle_nid_postfix();
    check(failure.empty(), failure.c_str());
    check(hold.ReleaseResult() == VK_SUCCESS, "cannot signal the semaphore that holds the first copy's batch");
    check(firstAtRelease == 0x11111111u, "a delta read in flight was restored before the GPU ran the batch of a submission the capture had completed");
    check(!drained, "a delta read in flight drained the GPU work of a submission it does not wait for");
    check(std::equal(rewritten.begin(), rewritten.end(), first), "the delta read in flight was not restored");
    check(std::all_of(firstTarget, firstTarget + CopyBytes, [](std::byte value) { return value == std::byte{0x11}; }), "the copy of a submission the capture had completed read the bytes of a delta read in flight after it");
    check(std::all_of(secondTarget, secondTarget + CopyBytes, [](std::byte value) { return value == std::byte{0x44}; }), "the copy of the submission after the delta's read did not run");
}

bool gpuEpochsTracked() {
    try {
        return AgcDriverWaitSerialsOnGpuFor_nid_postfix(0, nullptr, 0, 0, nullptr);
    } catch (const std::runtime_error& error) {
        if (std::string_view(error.what()).find("needs AgcDriverTrackGpuEpochs") == std::string_view::npos) throw;
        return false;
    }
}

void testGpuEpochsUntracked(const std::filesystem::path& directory) {
    FrameCaptureWriter writer(directory);
    writer.Finish("{}");
    const FrameCaptureReader reader(directory);
    check(!gpuEpochsTracked(), "the GPU batches of completed submissions were known with no frame replay tracking them");
    Options unpaced;
    unpaced.pace = false;
    {
        Replay replay{unpaced, reader};
        check(!gpuEpochsTracked(), "an unpaced frame replay tracked the GPU batches of completed submissions");
    }
    Options paced;
    {
        Replay replay{paced, reader};
        check(gpuEpochsTracked(), "a paced frame replay did not track the GPU batches of completed submissions");
        {
            Replay second{paced, reader};
        }
        check(gpuEpochsTracked(), "a second paced frame replay stopped the tracking when it ended while the first one was alive");
    }
    check(!gpuEpochsTracked(), "the tracking of the GPU batches of completed submissions outlived the frame replay that asked for it");
}

void testGpuEpochsFlip(const std::filesystem::path& directory) {
    constexpr std::uint64_t Base = 40;
    constexpr std::uint32_t ComputeQueue = 0x20;
    if (APS5_ENABLE_TIMING_LOG) return;
    const char* dump = std::getenv("APS5_DUMP_SUBMITS");
    check(std::getenv("APS5_CPU_FLIP_IN_ORDER") == nullptr && std::getenv("APS5_SYNC_FLIP") == nullptr && (dump == nullptr || *dump == '\0'), "the environment orders CPU flips or captures submissions, so the replay's CPU flip would take a serial");
    auto* bytes = allocate(FrameCaptureWriter::BlockBytes);
    auto* commands = reinterpret_cast<std::uint32_t*>(bytes);
    auto* label = reinterpret_cast<std::uint32_t*>(bytes + 4096);
    auto* restored = reinterpret_cast<std::uint32_t*>(bytes + 8192);
    auto* waiting = reinterpret_cast<std::uint32_t*>(bytes + 12288);
    *label = 0;
    *restored = 0;
    const std::array<std::uint32_t, 2> nop{0xc0001000u, 0u};
    std::copy(nop.begin(), nop.end(), commands);
    const std::array<std::uint32_t, 7> wait{0xc0053c00u, 0x13u, low(label), high(label), 1u, 0xffffffffu, 4u};
    std::copy(wait.begin(), wait.end(), waiting);
    const std::uint32_t stored = 7;
    FrameCaptureWriter writer(directory);
    writer.WriteDelta(1, 0, address(restored), std::as_bytes(std::span(&stored, 1)));
    FrameCall start;
    start.kind = FrameCallKind::FrameStart;
    start.accepted = Base;
    start.completed = Base;
    writer.WriteCall(start);
    writer.Finish("{}");
    const FrameCaptureReader reader(directory);
    Options options;
    options.release = false;
    options.busyTimeout = std::chrono::milliseconds(5000);
    Replay replay{options, reader};
    replay.recording = true;
    collectGroups(replay);
    auto output = std::make_shared<ReplayOutput>(replay.frames);
    AgcDriverRegisterVideoOutput_nid_postfix(7, output);
    const auto submit = [&](std::uint64_t ordinal, const std::uint32_t* words, std::size_t size, std::uint64_t taken) {
        FrameCall call;
        call.kind = FrameCallKind::Submit;
        call.ordinal = ordinal;
        call.thread = 1;
        call.queue = ComputeQueue;
        call.address = address(words);
        call.size = size;
        call.accepted = taken - 1;
        call.completed = taken - 1;
        call.values = {taken};
        return call;
    };
    FrameCall flip;
    flip.kind = FrameCallKind::Flip;
    flip.ordinal = 3;
    flip.thread = 1;
    flip.cpuFlip = true;
    flip.accepted = Base + 2;
    flip.completed = Base + 1;
    flip.values = {7, static_cast<std::uint64_t>(-1), 1, 0, Base + 3};
    auto delta = memoryCall(1, 1);
    delta.ordinal = 4;
    delta.flags = AgcDriver::InFlightDeltaFlag;
    delta.accepted = Base + 3;
    delta.completed = Base + 1;
    delta.values = {1, Base + 3};
    std::uint32_t restoredAtRelease = 0;
    std::thread title([&] {
        const auto limit = Clock::now() + std::chrono::seconds(1);
        do {
            restoredAtRelease = std::atomic_ref<std::uint32_t>(*restored).load(std::memory_order_acquire);
            std::this_thread::yield();
        } while (restoredAtRelease != stored && Clock::now() < limit);
        std::atomic_ref<std::uint32_t>(*label).store(1, std::memory_order_release);
    });
    const std::array calls{submit(1, commands, nop.size(), Base + 1), submit(2, waiting, wait.size(), Base + 2), flip, delta};
    std::string failure;
    try {
        replay.runWindow(calls);
    } catch (const std::exception& error) {
        failure = error.what();
    }
    title.join();
    AgcDriverWaitIdle_nid_postfix();
    AgcDriverUnregisterVideoOutput_nid_postfix(7, output);
    check(failure.empty(), ("a delta read in flight that lists the serial of a CPU flip that took one only in the capture made the GPU wait ask for a submission the flip did not take: " + failure).c_str());
    check(replay.frames.Times().size() == 1, "the replay did not present the CPU flip that took a serial only in the capture");
    check(restoredAtRelease == stored && *restored == stored, "the delta read in flight that lists the serial of a CPU flip that took one only in the capture was not restored while the submission before the flip, which it does not wait for, was parked");
}

struct PendingResults {
    std::uint64_t address = 0;
    std::size_t bytes = 0;
    std::byte value{};
    std::atomic<bool> pending{false};
    AgcDriver::GuestMemory::ReadSite site{};
};

PendingResults& pendingResults() {
    static PendingResults results;
    return results;
}

void storePendingResults(std::uint64_t access, std::size_t bytes) {
    auto& results = pendingResults();
    if (access >= results.address + results.bytes || results.address >= access + bytes || !results.pending.exchange(false)) return;
    results.site = AgcDriver::GuestMemory::CurrentReadSite();
    std::fill_n(reinterpret_cast<std::byte*>(static_cast<std::uintptr_t>(results.address)), results.bytes, results.value);
}

void testRestorePendingStore(const std::filesystem::path& directory) {
    constexpr std::size_t Page = 4096;
    auto* bytes = allocate(FrameCaptureWriter::BlockBytes);
    auto* page = bytes + Page;
    std::fill_n(bytes, FrameCaptureWriter::BlockBytes, std::byte{0x11});
    const std::vector<std::byte> rewritten(Page, std::byte{0x22});
    FrameCaptureWriter writer(directory);
    writer.WriteDelta(1, 0, address(page), rewritten);
    writer.Finish("{}");
    const FrameCaptureReader reader(directory);
    Options options;
    options.release = false;
    Replay replay{options, reader};
    replay.recording = true;
    collectGroups(replay);
    auto& results = pendingResults();
    results.address = address(page);
    results.bytes = Page;
    results.value = std::byte{0x5a};
    results.pending.store(true);
    struct FlushHookReset {
        ~FlushHookReset() { AgcDriver::GuestMemory::SetFlushHook(nullptr); }
    } reset;
    AgcDriver::GuestMemory::SetFlushHook(&storePendingResults);
    replay.execute(memoryCall(1, 1));
    AgcDriver::GuestMemory::FlushGpuWrites(address(page), Page);
    check(!results.pending.load(), "the results pending over the restored page were never stored");
    check(std::equal(rewritten.begin(), rewritten.end(), page), "results pending over a held restore landed after it");
    check(results.site == AgcDriver::GuestMemory::ReadSite::Store, "the restore flushed the pending results outside a store");
}

void testColdPipelines() {
    const auto* inherited = std::getenv("APS5_HEADLESS_PIPELINE_CACHE");
    check(inherited != nullptr && std::string_view(inherited) == "1", "pipeline cache setting was not inherited");
    Options options;
    options.coldPipelines = true;
    configureEnvironment(options);
    check(std::getenv("APS5_HEADLESS_PIPELINE_CACHE") == nullptr, "cold replay kept the inherited pipeline cache setting");
    check(std::getenv("APS5_FRAME_CAPTURE") == nullptr, "replay kept the capture setting");
    options.coldPipelines = false;
    configureEnvironment(options);
    const auto* warm = std::getenv("APS5_HEADLESS_PIPELINE_CACHE");
    check(warm != nullptr && std::string_view(warm) == "1", "warm replay did not enable the pipeline cache");
#ifdef _WIN32
    _putenv_s("APS5_HEADLESS_PIPELINE_CACHE", "0");
#else
    setenv("APS5_HEADLESS_PIPELINE_CACHE", "0", 1);
#endif
    options.coldPipelines = true;
    configureEnvironment(options);
    check(std::getenv("APS5_HEADLESS_PIPELINE_CACHE") == nullptr, "cold replay kept a present zero pipeline cache setting");
}

constexpr std::uint64_t DepthBase = 40;
constexpr std::array<std::uint32_t, 2> NopPacket{0xc0001000u, 0u};

DepthProfile depthProfile(std::initializer_list<std::pair<std::uint64_t, std::uint64_t>> lines) {
    DepthProfile profile;
    profile.path = "pacing-profile.txt";
    for (const auto& [bound, observed] : lines) {
        profile.names.push_back(profile.names.empty() ? std::string("suspend") : "s" + std::to_string(profile.names.size()));
        profile.bounds.push_back(bound);
        profile.observed.push_back(observed);
    }
    return profile;
}

FrameCall depthFrameStart() {
    FrameCall start;
    start.kind = FrameCallKind::FrameStart;
    start.accepted = DepthBase;
    start.completed = DepthBase;
    return start;
}

FrameCall pacedCall(FrameCallKind kind, std::uint64_t ordinal, std::uint64_t taken, const std::uint32_t* words = nullptr, std::size_t size = 0) {
    FrameCall call;
    call.kind = kind;
    call.ordinal = ordinal;
    call.thread = 1;
    call.address = address(words);
    call.size = size;
    call.accepted = taken - 1;
    call.completed = taken - 1;
    call.values = {taken};
    return call;
}

FrameCall depthRecord(std::uint64_t ordinal, std::uint64_t group) {
    auto call = memoryCall(group, 1);
    call.ordinal = ordinal;
    return call;
}

std::string windowFailure(Replay& replay, std::span<const FrameCall> calls) {
    std::string failure;
    try {
        replay.runWindow(calls);
    } catch (const std::runtime_error& error) {
        failure = error.what();
    }
    AgcDriverWaitIdle_nid_postfix();
    return failure;
}

void testPacingProfile(const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);
    const auto profileAt = [&](const std::string& name) { return directory / (name + ".txt"); };
    const auto written = [&](const std::string& name, std::string_view text) {
        const auto path = profileAt(name);
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        file.close();
        check(!file.fail(), "cannot write a depth profile of the test");
        return path;
    };
    const auto refusal = [](const std::filesystem::path& path) {
        try {
            readDepthProfile(path);
        } catch (const std::runtime_error& error) {
            return std::string(error.what());
        }
        return std::string();
    };
    const auto refused = [&](const std::string& name, std::string_view text, const std::string& expected, const std::string& accepted) {
        const auto path = written(name, text);
        const auto message = refusal(path);
        if (message.empty()) throw std::runtime_error(accepted + " (" + path.string() + ")");
        check(message == expected, ("the depth profile " + path.string() + " was refused with '" + message + "', not '" + expected + "'").c_str());
    };
    const auto line = [&](const std::string& name, int number) { return "frame replay: line " + std::to_string(number) + " of " + profileAt(name).string(); };
    const auto missing = profileAt("missing");
    std::filesystem::remove(missing);
    check(refusal(missing) == "frame replay: cannot open " + missing.string(), ("a missing depth profile was not refused as unopened: " + refusal(missing)).c_str());
    struct Text {
        std::string name;
        std::string_view text;
        int line = 0;
    };
    const std::vector<Text> unmarked{{"magic", "aps5-depth-profile 2\nsuspend 4 6\ns1 5 5\n"}, {"empty", ""}, {"unterminated_magic", "aps5-depth-profile 1"}};
    for (const auto& entry : unmarked) refused(entry.name, entry.text, "frame replay: " + profileAt(entry.name).string() + " does not start with the line 'aps5-depth-profile 1'", "a depth profile without the line 'aps5-depth-profile 1' was accepted");
    const std::vector<Text> malformed{
        {"short", "aps5-depth-profile 1\nsuspend 4\ns1 5 5\n", 2},
        {"long", "aps5-depth-profile 1\nsuspend 4 6\ns1 5 5 5\n", 3},
        {"carriage", "aps5-depth-profile 1\nsuspend 4 6\r\ns1 5 5\n", 2},
        {"unterminated", "aps5-depth-profile 1\nsuspend 4 6\ns1 5 5", 3},
        {"negative", "aps5-depth-profile 1\nsuspend 4 6\ns1 -5 5\n", 3},
        {"blank", "aps5-depth-profile 1\nsuspend 4 6\n\ns1 5 5\n", 3},
        {"spaced", "aps5-depth-profile 1\nsuspend  4 6\ns1 5 5\n", 2},
        {"trailing", "aps5-depth-profile 1\nsuspend 4 6 \ns1 5 5\n", 2},
        {"unnamed", "aps5-depth-profile 1\n 4 6\ns1 5 5\n", 2},
        {"overflow", "aps5-depth-profile 1\nsuspend 4 18446744073709551616\ns1 5 5\n", 2},
        {"hexadecimal", "aps5-depth-profile 1\nsuspend 0x4 6\ns1 5 5\n", 2},
    };
    for (const auto& entry : malformed) refused(entry.name, entry.text, line(entry.name, entry.line) + " is not '<position> <bound> <observed>'", "a depth profile with a malformed line was accepted");
    refused("first", "aps5-depth-profile 1\ns1 5 5\n", line("first", 2) + " names s1 where its frame needs suspend", "a depth profile without its suspend line was accepted");
    refused("skipped", "aps5-depth-profile 1\nsuspend 4 6\ns2 6 6\n", line("skipped", 3) + " names s2 where its frame needs s1", "a depth profile that skips s1 was accepted");
    refused("above", "aps5-depth-profile 1\nsuspend 7 6\ns1 5 5\n", line("above", 2) + " bounds suspend at 7, above the 6 it observed", "a bound above the observed depth was accepted");
    const std::vector<Text> frameless{{"frameless", "aps5-depth-profile 1\nsuspend 4 6\n"}, {"bare", "aps5-depth-profile 1\n"}};
    for (const auto& entry : frameless) refused(entry.name, entry.text, "frame replay: " + profileAt(entry.name).string() + " lists no submission after its suspend point", "a depth profile without a submission was accepted");
    const auto casino = written("casino", "aps5-depth-profile 1\nsuspend 4 6\ns1 5 5\ns2 6 6\ns3 7 7\ns4 8 8\ns5 9 9\ns6 6 7\n");
    const auto loaded = readDepthProfile(casino);
    check(loaded.path == casino && loaded.names == std::vector<std::string>{"suspend", "s1", "s2", "s3", "s4", "s5", "s6"} && loaded.bounds == std::vector<std::uint64_t>{4, 5, 6, 7, 8, 9, 6} &&
              loaded.observed == std::vector<std::uint64_t>{6, 5, 6, 7, 8, 9, 7},
          "the casino depth profile did not load as written");
    const auto usageCode = [&](std::vector<std::string> arguments) {
        arguments.insert(arguments.begin(), {"agc_frame_replay", (directory / "no-capture").string()});
        std::vector<char*> pointers;
        for (auto& argument : arguments) pointers.push_back(argument.data());
        pointers.push_back(nullptr);
        return frameReplayToolMain(static_cast<int>(arguments.size()), pointers.data());
    };
    const auto profile = casino.string();
    const std::vector<std::pair<std::string, std::vector<std::string>>> usages{
        {"a repeated --depth-profile", {"--depth-profile", profile, "--depth-profile", profile}},
        {"a repeated --depth-cap", {"--depth-profile", profile, "--depth-cap", "4", "--depth-cap", "4"}},
        {"--depth-cap without --depth-profile", {"--depth-cap", "4"}},
        {"--depth-probe without --depth-profile", {"--depth-probe", "16"}},
        {"--depth-cap with --depth-probe", {"--depth-profile", profile, "--depth-cap", "4", "--depth-probe", "16"}},
        {"--depth-profile with --no-pace", {"--depth-profile", profile, "--no-pace"}},
        {"--depth-profile with --shaders", {"--depth-profile", profile, "--shaders"}},
        {"--depth-profile with --shader-code", {"--depth-profile", profile, "--shader-code", (directory / "code").string()}},
        {"--depth-profile with --submits", {"--depth-profile", profile, "--submits", "1"}},
        {"a --depth-cap that is not a number", {"--depth-profile", profile, "--depth-cap", "4x"}},
        {"an empty --depth-profile", {"--depth-profile", ""}},
    };
    for (const auto& [what, arguments] : usages) check(usageCode(arguments) == 2, ("the replay did not return its usage for " + what).c_str());
    auto* block = allocate(FrameCaptureWriter::BlockBytes);
    auto* sentinel = reinterpret_cast<std::uint32_t*>(block);
    constexpr std::uint32_t Marked = 0x5a5a5a5au;
    std::vector<std::byte> snapshot(FrameCaptureWriter::BlockBytes);
    std::memcpy(snapshot.data(), &Marked, sizeof(Marked));
    std::uintptr_t base = 0;
    std::size_t arenaSize = 0;
    GuestArena::GuestArenaRange_nid_postfix(&base, &arenaSize);
    const auto capture = [&](const std::string& name, bool started) {
        const auto path = directory / name;
        FrameCaptureWriter writer(path);
        writer.WriteRange(address(block), FrameCaptureWriter::BlockBytes, FrameRangeKind::Watched, ReadWrite, FrameRangeAccess::Readable | FrameRangeAccess::Writable | FrameRangeAccess::Gpu);
        writer.WriteSnapshot(address(block), snapshot);
        if (started) writer.WriteCall(depthFrameStart());
        writer.Finish("{\"arena_base\":" + std::to_string(base) + "}");
        return path;
    };
    const auto runFailure = [&](const Options& options) {
        *sentinel = 0;
        try {
            run(options);
        } catch (const std::runtime_error& error) {
            return std::string(error.what());
        }
        return std::string();
    };
    Options released;
    released.directory = capture("released", true);
    released.depth = loaded;
    const auto releaseFailure = runFailure(released);
    check(releaseFailure == "frame replay: --depth-profile needs --no-release: a replay at depth never writes recorded or synthesized values to release a wait", ("the replay did not refuse --depth-profile with the breaker on: " + releaseFailure).c_str());
    check(*sentinel == 0, "the replay restored the capture's snapshot before it refused --depth-profile with the breaker on");
    Options unstarted;
    unstarted.directory = capture("unstarted", false);
    unstarted.release = false;
    unstarted.depth = loaded;
    const auto startFailure = runFailure(unstarted);
    check(startFailure == DepthWithoutFrameStart, ("the replay did not refuse --depth-profile on a capture without a frame start: " + startFailure).c_str());
    check(*sentinel == 0, "the replay restored the capture's snapshot before it refused a capture without a frame start");
    restoreMemory(FrameCaptureReader(released.directory));
    check(*sentinel == Marked, "the test capture does not restore its sentinel word, so its refusals could not show that nothing was restored");
}

void testPacingTemplate(const std::filesystem::path& directory) {
    auto* bytes = allocate(FrameCaptureWriter::BlockBytes);
    auto* nop = reinterpret_cast<std::uint32_t*>(bytes);
    auto* first = reinterpret_cast<std::uint32_t*>(bytes + 8192);
    auto* last = reinterpret_cast<std::uint32_t*>(bytes + 8196);
    std::copy(NopPacket.begin(), NopPacket.end(), nop);
    *first = 0;
    *last = 0;
    const std::uint32_t firstValue = 0x11u;
    const std::uint32_t lastValue = 0x22u;
    FrameCaptureWriter writer(directory);
    writer.WriteDelta(1, 0, address(first), std::as_bytes(std::span(&firstValue, 1)));
    writer.WriteDelta(2, 0, address(last), std::as_bytes(std::span(&lastValue, 1)));
    writer.WriteCall(depthFrameStart());
    writer.Finish("{}");
    const FrameCaptureReader reader(directory);
    Options options;
    options.release = false;
    options.depth = depthProfile({{1, 1}, {1, 1}, {1, 1}});
    Replay replay{options, reader};
    replay.recording = true;
    collectGroups(replay);
    constexpr auto Base = DepthBase;
    const auto suspend = [](std::uint64_t ordinal, std::uint64_t taken) { return pacedCall(FrameCallKind::Suspend, ordinal, taken); };
    const auto submit = [&](std::uint64_t ordinal, std::uint64_t taken) { return pacedCall(FrameCallKind::Submit, ordinal, taken, nop, NopPacket.size()); };
    auto threaded = depthRecord(4, 1);
    threaded.thread = 2;
    auto flip = pacedCall(FrameCallKind::Flip, 3, Base + 3);
    flip.values = {7, static_cast<std::uint64_t>(-1), 1, 0, Base + 3};
    auto reservation = reservationCall(AgcDriver::EopFlipTokenBase);
    reservation.ordinal = 2;
    reservation.thread = 1;
    auto compute = submit(2, Base + 2);
    compute.queue = 0x20;
    auto unrecorded = submit(2, Base + 2);
    unrecorded.values.clear();
    auto untaken = suspend(1, Base + 1);
    untaken.values = {0};
    const auto profile = options.depth->path.string();
    const auto counted = " queue 0 submissions where " + profile + " lists s1-s2";
    struct Case {
        std::string what;
        std::vector<FrameCall> calls;
        std::string expected;
    };
    const std::vector<Case> cases{
        {"a second thread's call", {suspend(1, Base + 1), submit(2, Base + 2), submit(3, Base + 3), threaded}, "frame replay: --depth-profile replays one calling thread, but call 1 is on thread 1 and call 4 on thread 2"},
        {"a flip", {suspend(1, Base + 1), submit(2, Base + 2), flip}, "frame replay: --depth-profile does not order flips or EOP flip reservations inside the captured frames, and call 3 is a flip"},
        {"an EOP flip reservation", {suspend(1, Base + 1), reservation}, "frame replay: --depth-profile does not order flips or EOP flip reservations inside the captured frames, and call 2 is a reserve-eop-flip"},
        {"a submission to queue 0x20", {suspend(1, Base + 1), compute}, "frame replay: call 2 submits to queue 0x20, and --depth-profile replays queue 0 submissions only"},
        {"a submission before the first suspend point", {submit(1, Base + 1), suspend(2, Base + 2), submit(3, Base + 3), submit(4, Base + 4)}, "frame replay: call 1 (submit) comes before the first suspend point of the captured frames, so it has no position in " + profile},
        {"a frame with one submission", {suspend(1, Base + 1), submit(2, Base + 2), suspend(3, Base + 3), submit(4, Base + 4), submit(5, Base + 5)}, "frame replay: frame 1 of the captured frames (from call 1) has 1" + counted},
        {"a frame with three submissions", {suspend(1, Base + 1), submit(2, Base + 2), submit(3, Base + 3), submit(4, Base + 4)}, "frame replay: frame 1 of the captured frames (from call 1) has 3" + counted},
        {"a short last frame", {suspend(1, Base + 1), submit(2, Base + 2), submit(3, Base + 3), suspend(4, Base + 4), submit(5, Base + 5)}, "frame replay: frame 2 of the captured frames (from call 4) has 1" + counted},
        {"frames without a suspend point", {depthRecord(1, 1)}, "frame replay: the captured frames have no suspend point, so the frame of " + profile + " never starts"},
        {"a submission without its serial", {suspend(1, Base + 1), unrecorded}, "frame replay: call 2 (submit) has no recorded serial, so --depth-profile cannot map the floors and bounds that wait for it"},
        {"a suspend point that took no serial", {untaken, submit(2, Base + 2), submit(3, Base + 3)}, "frame replay: call 1 (suspend) has no recorded serial, so --depth-profile cannot map the floors and bounds that wait for it"},
    };
    const auto refusedBeforeIssue = [&](Replay& target, const std::string& what, std::span<const FrameCall> calls, const std::string& expected) {
        const auto before = progress();
        const auto failure = windowFailure(target, calls);
        if (failure.empty()) throw std::runtime_error("a frame that differs from the profile replayed: " + what);
        check(failure == expected, ("the replay refused " + what + " with '" + failure + "', not '" + expected + "'").c_str());
        check(progress() == before, ("the replay issued calls of " + what + " before it refused them").c_str());
    };
    for (const auto& entry : cases) refusedBeforeIssue(replay, entry.what, entry.calls, entry.expected);
    const auto bare = directory / "bare";
    {
        FrameCaptureWriter bareWriter(bare);
        bareWriter.Finish("{}");
    }
    const FrameCaptureReader bareReader(bare);
    Replay unstarted{options, bareReader};
    unstarted.recording = true;
    const std::array frameOnly{suspend(1, Base + 1), submit(2, Base + 2), submit(3, Base + 3)};
    refusedBeforeIssue(unstarted, "a window of a capture without a frame start", frameOnly, std::string(DepthWithoutFrameStart));
    const std::array frames{suspend(1, Base + 1), depthRecord(2, 1), submit(3, Base + 2), submit(4, Base + 3), suspend(5, Base + 4), submit(6, Base + 5), submit(7, Base + 6), depthRecord(8, 2)};
    const auto failure = windowFailure(replay, frames);
    check(failure.empty(), ("two frames that follow the profile did not replay: " + failure).c_str());
    check(replay.unfinished == 0 && *first == firstValue && *last == lastValue, "the two frames that follow the profile did not finish with both memory deltas restored");
    check(replay.plan && replay.plan->frames == 2, "the replay did not count the two captured frames");
    check(replay.plan->next[1] == 2 && replay.plan->position[2] == 1, "the memory delta between the first suspend point and its first submission did not take position s1");
    check(replay.plan->next[7] == DepthPlan::Tail, "the memory delta after the last submission did not take the tail");
    check(replay.replaySerials.size() == 6, ("the replay mapped " + std::to_string(replay.replaySerials.size()) + " of the six calls that took a serial").c_str());
}

void testPacingSerials(const std::filesystem::path& directory) {
    auto* bytes = allocate(FrameCaptureWriter::BlockBytes);
    auto* nop = reinterpret_cast<std::uint32_t*>(bytes);
    std::copy(NopPacket.begin(), NopPacket.end(), nop);
    FrameCaptureWriter writer(directory);
    writer.WriteCall(depthFrameStart());
    writer.Finish("{}");
    const FrameCaptureReader reader(directory);
    Options options;
    options.release = false;
    options.depth = depthProfile({{1, 1}, {1, 1}});
    Replay replay{options, reader};
    replay.recording = true;
    constexpr auto Base = DepthBase;
    const auto suspend = [](std::uint64_t ordinal, std::uint64_t taken) { return pacedCall(FrameCallKind::Suspend, ordinal, taken); };
    const auto submit = [&](std::uint64_t ordinal, std::uint64_t taken) { return pacedCall(FrameCallKind::Submit, ordinal, taken, nop, NopPacket.size()); };
    const std::array diverged{suspend(1, Base + 1), submit(2, Base + 3)};
    const auto skipped = windowFailure(replay, diverged);
    const auto expected = "frame replay: call 2 (submit) took replay serial " + std::to_string(replay.replayBase + 2) + " where the capture's serial " + std::to_string(Base + 3) + " maps to " + std::to_string(replay.replayBase + 3) + std::string(DepthUnmapped);
    check(!skipped.empty(), "a submission recorded as taking the third serial after the frame start, after a suspend point that took the first, replayed at depth");
    check(skipped == expected, ("the replay at depth did not refuse the submission that took another serial than its recorded one maps to with '" + expected + "': " + skipped).c_str());
    const std::array divergedSuspend{suspend(1, Base + 2), submit(2, Base + 3)};
    const auto early = windowFailure(replay, divergedSuspend);
    const auto expectedEarly = "frame replay: call 1 (suspend) took replay serial " + std::to_string(replay.replayBase + 1) + " where the capture's serial " + std::to_string(Base + 2) + " maps to " + std::to_string(replay.replayBase + 2) + std::string(DepthUnmapped);
    check(early == expectedEarly, ("the replay at depth did not refuse the suspend point that took another serial than its recorded one maps to with '" + expectedEarly + "': " + early).c_str());
    const std::array matched{suspend(1, Base + 1), submit(2, Base + 2)};
    const auto replayed = windowFailure(replay, matched);
    check(replayed.empty(), ("the replay at depth refused a suspend point and a submission that took the serials they recorded: " + replayed).c_str());
    const std::map<std::uint64_t, std::optional<std::uint64_t>> mapped{{Base + 1, replay.replayBase + 1}, {Base + 2, replay.replayBase + 2}};
    check(replay.unfinished == 0 && replay.replaySerials == mapped, "the replay at depth did not map the suspend point and the submission to the replay serials they took");
}

class Gate {
public:
    Gate(std::uint32_t* dword, std::function<bool()> arm, std::function<bool()> release)
        : thread([this, dword, arm = std::move(arm), release = std::move(release)] {
              const auto armBy = Clock::now() + std::chrono::seconds(5);
              while (!arm()) {
                  if (Clock::now() >= armBy) {
                      std::atomic_ref<std::uint32_t>(*dword).store(1, std::memory_order_release);
                      return;
                  }
                  std::this_thread::sleep_for(std::chrono::microseconds(50));
              }
              armed.store(true);
              const auto openAt = Clock::now() + std::chrono::milliseconds(300);
              while (!release() && Clock::now() < openAt) std::this_thread::sleep_for(std::chrono::microseconds(50));
              std::atomic_ref<std::uint32_t>(*dword).store(1, std::memory_order_release);
          }) {}
    Gate(const Gate&) = delete;
    Gate& operator=(const Gate&) = delete;
    ~Gate() {
        if (thread.joinable()) thread.join();
    }

    bool Armed() {
        if (thread.joinable()) thread.join();
        return armed.load();
    }

private:
    std::atomic<bool> armed{false};
    std::thread thread;
};

std::array<std::uint32_t, 7> waitPacket(const std::uint32_t* label) {
    return {0xc0053c00u, 0x13u, static_cast<std::uint32_t>(address(label)), static_cast<std::uint32_t>(address(label) >> 32u), 1u, 0xffffffffu, 4u};
}

void testPacingBound(const std::filesystem::path& directory) {
    auto* bytes = allocate(FrameCaptureWriter::BlockBytes);
    auto* gate = reinterpret_cast<std::uint32_t*>(bytes);
    auto* waiting = reinterpret_cast<std::uint32_t*>(bytes + 4096);
    auto* nop = reinterpret_cast<std::uint32_t*>(bytes + 8192);
    auto* cappedGate = reinterpret_cast<std::uint32_t*>(bytes + 12288);
    auto* cappedWaiting = reinterpret_cast<std::uint32_t*>(bytes + 16384);
    auto* probedGate = reinterpret_cast<std::uint32_t*>(bytes + 20480);
    auto* probedWaiting = reinterpret_cast<std::uint32_t*>(bytes + 24576);
    *gate = 0;
    *cappedGate = 0;
    *probedGate = 0;
    const auto wait = waitPacket(gate);
    const auto cappedWait = waitPacket(cappedGate);
    const auto probedWait = waitPacket(probedGate);
    std::copy(wait.begin(), wait.end(), waiting);
    std::copy(cappedWait.begin(), cappedWait.end(), cappedWaiting);
    std::copy(probedWait.begin(), probedWait.end(), probedWaiting);
    std::copy(NopPacket.begin(), NopPacket.end(), nop);
    FrameCaptureWriter writer(directory);
    writer.WriteCall(depthFrameStart());
    writer.Finish("{}");
    const FrameCaptureReader reader(directory);
    Options options;
    options.release = false;
    options.paceTimeout = std::chrono::seconds(2);
    options.depth = depthProfile({{1, 1}, {3, 3}});
    Replay replay{options, reader};
    replay.recording = true;
    constexpr auto Base = DepthBase;
    const auto suspend = [](std::uint64_t ordinal, std::uint64_t taken) { return pacedCall(FrameCallKind::Suspend, ordinal, taken); };
    const auto submit = [&](std::uint64_t ordinal, std::uint64_t taken) { return pacedCall(FrameCallKind::Submit, ordinal, taken, nop, NopPacket.size()); };
    const std::array calls{suspend(1, Base + 1), pacedCall(FrameCallKind::Submit, 2, Base + 2, waiting, wait.size()), suspend(3, Base + 3), submit(4, Base + 4), suspend(5, Base + 5), submit(6, Base + 6)};
    const auto behindGate = [](Replay& target, std::span<const FrameCall> window, std::uint32_t* label, std::uint64_t armAt) {
        if (progress().first == 0) Replay::primeAcceptedSerial();
        const auto base = progress().first;
        check(std::atomic_ref<std::uint32_t>(*label).load() == 0, "the label the first submission waits on was already written");
        Gate opener(label, [base, armAt] { return progress().first - base >= armAt; }, [] { return false; });
        auto failure = windowFailure(target, window);
        const bool armed = opener.Armed();
        AgcDriverWaitIdle_nid_postfix();
        return std::pair{failure, armed};
    };
    const auto [failure, armed] = behindGate(replay, calls, gate, 4);
    check(failure.empty(), ("the replay of frames at the profile's bounds failed: " + failure).c_str());
    check(armed, "the replay did not run ahead of a parked submission");
    check(replay.paceTimeouts.load() == 0, ("the replay gave up pacing behind the parked submission instead of issuing within the profile's bounds (pace_timeouts " + std::to_string(replay.paceTimeouts.load()) + ")").c_str());
    check(replay.census.size() == 2 && replay.census[0].issue.size() == 2 && replay.census[1].issue.size() == 2, "the replay did not sample the suspend point and the submission of frames 2 and 3, and only those");
    for (std::size_t position = 0; position < replay.census.size(); ++position) {
        const auto& issue = replay.census[position].issue;
        const auto deepest = *std::max_element(issue.begin(), issue.end());
        const auto bound = options.depth->bounds[position];
        check(deepest <= bound, ("issue depth " + std::to_string(deepest) + " above the bound " + std::to_string(bound) + " at " + (position == 0 ? std::string("the suspend") : options.depth->names[position])).c_str());
    }
    check(replay.census[0].issue[0] == 1 && replay.census[1].issue[0] == 2, ("frame 2 started its suspend point at depth " + std::to_string(replay.census[0].issue[0]) + " and its submission at depth " + std::to_string(replay.census[1].issue[0]) + ", not 1 and 2 behind the parked submission").c_str());
    check(replay.paceWaitedSeconds() >= 0.1, ("the wait of frame 3's suspend point for the parked submission was not counted: pacing waited " + std::to_string(replay.paceWaitedSeconds()) + " s").c_str());
    check(replay.unfinished == 0, "the replay of frames at the profile's bounds did not finish");
    auto cappedOptions = options;
    cappedOptions.depthCap = 0;
    Replay capped{cappedOptions, reader};
    capped.recording = true;
    auto entered = suspend(3, Base + 3);
    entered.completed = Base + 1;
    const std::array cappedCalls{suspend(1, Base + 1), pacedCall(FrameCallKind::Submit, 2, Base + 2, cappedWaiting, cappedWait.size()), entered, submit(4, Base + 4)};
    const auto [cappedFailure, cappedArmed] = behindGate(capped, cappedCalls, cappedGate, 3);
    check(cappedFailure.empty(), ("the replay at --depth-cap 0 waited for the parked submission at a suspend point the capture entered with that submission in flight: " + cappedFailure).c_str());
    check(cappedArmed, "the replay at --depth-cap 0 did not start the suspend point the capture entered with one submission in flight while that submission was parked");
    check(capped.census.size() == 2 && capped.census[0].issue == std::vector<std::uint64_t>{1} && capped.census[1].issue == std::vector<std::uint64_t>{0}, "at --depth-cap 0, frame 2 did not start its suspend point at the capture's depth 1 and its submission at depth 0");
    check(capped.unfinished == 0, "the replay at --depth-cap 0 did not finish");
    auto probedOptions = options;
    probedOptions.depth = depthProfile({{3, 3}, {3, 3}});
    probedOptions.depthProbe = 1;
    Replay probed{probedOptions, reader};
    probed.recording = true;
    const std::array probedCalls{suspend(1, Base + 1), pacedCall(FrameCallKind::Submit, 2, Base + 2, probedWaiting, probedWait.size()), suspend(3, Base + 3), submit(4, Base + 4), suspend(5, Base + 5), submit(6, Base + 6)};
    const auto [probedFailure, probedArmed] = behindGate(probed, probedCalls, probedGate, 3);
    check(probedFailure.empty(), ("the replay at --depth-probe 1 failed: " + probedFailure).c_str());
    check(probedArmed, "the replay at --depth-probe 1 did not run ahead of a parked submission");
    check(probed.census.size() == 2 && probed.census[0].issue.size() == 2 && probed.census[1].issue.size() == 2, "the replay at --depth-probe 1 did not sample the suspend point and the submission of frames 2 and 3, and only those");
    for (std::size_t position = 0; position < probed.census.size(); ++position) {
        const auto& issue = probed.census[position].issue;
        const auto deepest = *std::max_element(issue.begin(), issue.end());
        check(deepest <= 1, ("issue depth " + std::to_string(deepest) + " above the probe 1 at " + (position == 0 ? std::string("the suspend") : probedOptions.depth->names[position])).c_str());
    }
    check(probed.unfinished == 0, "the replay at --depth-probe 1 did not finish");
}

void testPacingStall(const std::filesystem::path& directory) {
    openDevice();
    constexpr auto Base = DepthBase;
    constexpr std::size_t BusyPackets = 20000;
    constexpr std::size_t BusyLabels = 512u * 1024u;
    constexpr std::size_t LabelWords = 5;
    auto* bytes = allocate(FrameCaptureWriter::BlockBytes);
    auto* gate = reinterpret_cast<std::uint32_t*>(bytes);
    auto* waiting = reinterpret_cast<std::uint32_t*>(bytes + 4096);
    auto* nop = reinterpret_cast<std::uint32_t*>(bytes + 8192);
    *gate = 0;
    const auto wait = waitPacket(gate);
    std::copy(wait.begin(), wait.end(), waiting);
    std::copy(NopPacket.begin(), NopPacket.end(), nop);
    auto* busy = allocate(std::size_t{1} << 20u);
    auto* busyWords = reinterpret_cast<std::uint32_t*>(busy);
    auto* labels = reinterpret_cast<std::uint32_t*>(busy + BusyLabels);
    std::fill_n(labels, BusyPackets, 0u);
    for (std::size_t index = 0; index < BusyPackets; ++index) {
        const std::array<std::uint32_t, LabelWords> label{0xc0033700u, 0x00100500u, static_cast<std::uint32_t>(address(labels + index)), static_cast<std::uint32_t>(address(labels + index) >> 32u), 1u};
        std::copy(label.begin(), label.end(), busyWords + index * LabelWords);
    }
    auto* const lastLabel = labels + BusyPackets - 1;
    const std::uint32_t restoredLabel = 2;
    FrameCaptureWriter writer(directory);
    writer.WriteDelta(1, 0, address(lastLabel), std::as_bytes(std::span(&restoredLabel, 1)));
    writer.WriteCall(depthFrameStart());
    writer.Finish("{}");
    const FrameCaptureReader reader(directory);
    const auto suspend = [](std::uint64_t ordinal, std::uint64_t taken) { return pacedCall(FrameCallKind::Suspend, ordinal, taken); };
    const auto submit = [&](std::uint64_t ordinal, std::uint64_t taken) { return pacedCall(FrameCallKind::Submit, ordinal, taken, nop, NopPacket.size()); };
    Options capOptions;
    capOptions.release = false;
    capOptions.paceTimeout = std::chrono::milliseconds(300);
    capOptions.drainTimeout = std::chrono::milliseconds(300);
    capOptions.busyTimeout = std::chrono::milliseconds(5000);
    capOptions.depth = depthProfile({{2, 2}, {3, 3}});
    capOptions.depthCap = 0;
    Replay capped{capOptions, reader};
    capped.recording = true;
    const std::array parkedFrames{suspend(1, Base + 1), pacedCall(FrameCallKind::Submit, 2, Base + 2, waiting, wait.size()), suspend(3, Base + 3), submit(4, Base + 4)};
    std::string stall;
    try {
        capped.runWindow(parkedFrames);
    } catch (const std::runtime_error& error) {
        stall = error.what();
    }
    std::atomic_ref<std::uint32_t>(*gate).store(1, std::memory_order_release);
    AgcDriverWaitIdle_nid_postfix();
    char blocked[64];
    std::snprintf(blocked, sizeof(blocked), "; blocked on 0x%llx (", static_cast<unsigned long long>(address(gate)));
    const auto expected = "frame replay: call 3 (suspend, position suspend) may start with at most 0 submissions in flight, but no queue runs and the serials stopped at " + std::to_string(capped.replayBase + 1) + "/" + std::to_string(capped.replayBase + 2) + blocked;
    check(!stall.empty(), ("the replay at --depth-cap 0 went on past a submission parked on title memory instead of failing (pace_timeouts " + std::to_string(capped.paceTimeouts.load()) + ")").c_str());
    check(stall.starts_with(expected), ("the stall at --depth-cap 0 did not fail with '" + expected + "': " + stall).c_str());
    check(capped.paceTimeouts.load() == 0, "the replay at depth counted a pacing give-up");
    Options settleOptions;
    settleOptions.release = false;
    settleOptions.drainTimeout = std::chrono::milliseconds(300);
    settleOptions.busyTimeout = std::chrono::milliseconds(1);
    settleOptions.depth = depthProfile({{2, 2}, {2, 2}});
    Replay settling{settleOptions, reader};
    settling.recording = true;
    collectGroups(settling);
    check(std::atomic_ref<std::uint32_t>(*lastLabel).load() == 0, "the last label of the busy submission was already written");
    const std::array busyFrames{suspend(1, Base + 1), pacedCall(FrameCallKind::Submit, 2, Base + 2, busyWords, BusyPackets * LabelWords), depthRecord(3, 1), suspend(4, Base + 3), submit(5, Base + 4)};
    const auto unsettled = windowFailure(settling, busyFrames);
    const auto last = std::atomic_ref<std::uint32_t>(*lastLabel).load(std::memory_order_acquire);
    if (unsettled.empty()) {
        if (last == restoredLabel) throw Inconclusive("the busy submission had finished before the replay reached the memory delta after it");
        throw std::runtime_error("the replay at depth restored a memory delta while the busy submission before it still ran, without settling");
    }
    check(unsettled == "frame replay: memory delta at call 3 is restored with nothing running or every busy queue parked on title memory, but the replay did not settle within 1 ms", ("the replay at depth did not fail at the memory delta it could not settle: " + unsettled).c_str());
    check(last == 1, "the busy submission did not store its last label, or the replay restored the memory delta over it");
}

void testPacingFailure(const std::filesystem::path& directory) {
    auto* bytes = allocate(FrameCaptureWriter::BlockBytes);
    auto* gate = reinterpret_cast<std::uint32_t*>(bytes);
    auto* waiting = reinterpret_cast<std::uint32_t*>(bytes + 4096);
    auto* nop = reinterpret_cast<std::uint32_t*>(bytes + 8192);
    *gate = 0;
    const auto wait = waitPacket(gate);
    std::copy(wait.begin(), wait.end(), waiting);
    std::copy(NopPacket.begin(), NopPacket.end(), nop);
    FrameCaptureWriter writer(directory);
    writer.WriteCall(depthFrameStart());
    writer.Finish("{}");
    const FrameCaptureReader reader(directory);
    Options options;
    options.release = false;
    options.paceTimeout = std::chrono::seconds(2);
    options.depth = depthProfile({{2, 2}, {3, 3}});
    options.depthCap = 0;
    Replay replay{options, reader};
    replay.recording = true;
    constexpr auto Base = DepthBase;
    const std::array calls{pacedCall(FrameCallKind::Suspend, 1, Base + 1), pacedCall(FrameCallKind::Submit, 2, Base + 2, waiting, wait.size()), pacedCall(FrameCallKind::Suspend, 3, Base + 3), pacedCall(FrameCallKind::Submit, 4, Base + 4, nop, NopPacket.size())};
    if (progress().first == 0) Replay::primeAcceptedSerial();
    const auto base = progress().first;
    const auto parked = [&] {
        std::array<AgcDriver::AwaitedWait, 64> awaited{};
        const auto count = AgcDriverAwaitedWaits_nid_postfix(awaited.data(), awaited.size());
        return std::any_of(awaited.begin(), awaited.begin() + static_cast<std::ptrdiff_t>(count), [&](const AgcDriver::AwaitedWait& slot) { return slot.address == address(gate); });
    };
    std::atomic<bool> reported{false};
    std::thread failing([&] {
        const auto deadline = Clock::now() + std::chrono::seconds(5);
        while (progress().first - base < 2 || !parked()) {
            if (Clock::now() >= deadline) return;
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        AgcDriverReportFailure_nid_postfix(std::make_exception_ptr(std::runtime_error("depth driver failure")));
        reported.store(true);
    });
    std::string failure;
    try {
        replay.runWindow(calls);
    } catch (const std::exception& error) {
        failure = error.what();
    }
    failing.join();
    if (!reported.load()) {
        std::atomic_ref<std::uint32_t>(*gate).store(1, std::memory_order_release);
        throw std::runtime_error("the submission before the second suspend point did not park on its unwritten label: " + failure);
    }
    check(failure == "depth driver failure", ("a driver failure while a call waited at depth did not end the replay with the driver's error: " + failure).c_str());
    bool rethrown = false;
    try { AgcDriverShutdown_nid_postfix(); }
    catch (const std::runtime_error& error) { rethrown = std::string_view(error.what()) == "depth driver failure"; }
    check(rethrown, "the injected driver failure was not propagated at shutdown");
}

#ifdef __linux__
std::optional<std::pair<std::uint64_t, std::byte>> lastRecorded(const FrameCaptureReader& reader, const std::byte* at) {
    std::optional<std::pair<std::uint64_t, std::byte>> last;
    for (const auto& chunk : reader.Chunks()) {
        if ((chunk.kind != FrameMemoryKind::Delta && chunk.kind != FrameMemoryKind::Snapshot) || chunk.address > address(at) || address(at) >= chunk.address + chunk.bytes) continue;
        const std::uint64_t group = chunk.kind == FrameMemoryKind::Delta ? chunk.group : 0;
        if (last && group < last->first) continue;
        last = std::pair{group, reader.Read(chunk)[address(at) - chunk.address]};
    }
    return last;
}

std::string describeRecorded(const std::optional<std::pair<std::uint64_t, std::byte>>& last) {
    if (!last) return "in no memory delta and not in the snapshot";
    const auto where = last->first == 0 ? std::string("last recorded by the snapshot") : "last recorded in memory delta group " + std::to_string(last->first);
    return where + " with byte " + std::to_string(static_cast<int>(last->second));
}

void testFreshPage(const std::filesystem::path& directory) {
    constexpr std::size_t Size = 1u << 20;
    constexpr std::size_t Page = 4096;
    auto* bytes = allocate(Size);
    std::fill_n(bytes, Size, std::byte{0});
    auto* block = bytes + 2 * FrameCaptureWriter::BlockBytes;
    auto* remapped = block + Page;
    std::fill_n(remapped, Page, std::byte{0x52});
    auto* recorder = AgcDriver::FrameRecorder::Active();
    check(recorder != nullptr, "frame recorder is disabled");
    recorder->FrameBoundary();
    check(mmap(remapped, Page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) == remapped, "the guest page could not be mapped again");
    GuestWriteWatch::GuestWriteWatchRegister_nid_postfix(remapped, Page);
    check(AgcDriver::GuestMemory::MarkWritten(address(block + 8 * Page), 4) != 0, "the GPU write in the block of the page mapped again was not stamped");
    AgcDriverSuspendPoint_nid_postfix();
    recorder->FrameBoundary();
    const FrameCaptureReader reader(directory);
    check(reader.Header().find("stopped") == std::string::npos, ("the capture stopped: " + reader.Header()).c_str());
    const auto last = lastRecorded(reader, remapped);
    if (last && last->first != 0 && last->second == std::byte{0}) return;
    throw std::runtime_error("the page mapped again during the capture is " + describeRecorded(last) + ", not the 0 of its new mapping");
}
#endif

}

int main(int argc, char** argv) {
    try {
        check(argc == 3, "expected a test name and capture directory");
        const std::string mode = argv[1];
        const std::filesystem::path directory = argv[2];
        if (mode == "inspect_capture") {
            writeInspectionCapture(directory);
            return 0;
        }
        if (mode == "order") testMemoryOrder(directory);
        else if (mode == "zero") testReusedMemory(directory);
        else if (mode.starts_with("shader_prologue_")) testShaderCode(directory, true, mode);
        else if (mode == "shared_page") testSharedPage(directory);
        else if (mode == "host_neighbors") testHostNeighbors(directory);
        else if (mode == "unwatched") testUnwatched();
#ifdef __linux__
        else if (mode == "fresh_page") testFreshPage(directory);
#endif
        else if (mode == "tokens") testEopTokens();
        else if (mode == "reservations" || mode == "reservation_failure") testRepeatedReservations(directory, mode == "reservation_failure");
        else if (mode == "blocking") testBlockingSubmission(directory);
        else if (mode == "gate_settled") testGateSettled(directory);
        else if (mode == "completed_gate") testCompletedGate(directory);
        else if (mode == "completed_gate_flip") testCompletedGateFlip(directory);
        else if (mode == "depth_submits") testDepthAfterSubmits(directory);
        else if (mode == "depth_passes") testDepthOfTimedPasses(directory);
        else if (mode == "gate_failure") testGateFailure(directory);
        else if (mode == "in_flight_breaker" || mode == "previous_breaker") testRewindBreaker(directory, mode == "previous_breaker");
        else if (mode == "unverified") testUnverifiedDeltas(directory);
        else if (mode == "serial_diverged") testSerialDiverged(directory);
        else if (mode == "serial_flip_in_order") testSerialFlipInOrder(directory);
        else if (mode == "flip_serial_neutral") testFlipSerialNeutral(directory);
        else if (mode == "environment") testColdPipelines();
        else if (mode == "shader" || mode == "shader_host") testShaderCode(directory, mode == "shader_host");
        else if (mode == "shader_host_rewritten" || mode == "shader_host_restored" || mode == "shader_unaligned_restored") testDeltaPaths(directory, mode);
        else if (mode == "shader_hole") testShaderBlockHole(directory);
        else if (mode == "delta_hole") testDeltaOutsideRanges(directory);
        else if (mode == "gpu_gate" || mode == "gpu_gate_in_flight") testGpuGate(directory, mode == "gpu_gate_in_flight");
        else if (mode == "gpu_gate_parked") testGpuGateParked(directory);
        else if (mode == "gpu_gate_queues") testGpuGateQueues();
        else if (mode == "gpu_gate_depth") testGpuGateDepth(directory);
        else if (mode == "gpu_epochs_untracked") testGpuEpochsUntracked(directory);
        else if (mode == "gpu_epochs_flip") testGpuEpochsFlip(directory);
        else if (mode == "restore_pending_store") testRestorePendingStore(directory);
        else if (mode == "pacing_profile") testPacingProfile(directory);
        else if (mode == "pacing_template") testPacingTemplate(directory);
        else if (mode == "pacing_serials") testPacingSerials(directory);
        else if (mode == "pacing_bound") testPacingBound(directory);
        else if (mode == "pacing_stall") testPacingStall(directory);
        else if (mode == "pacing_failure") testPacingFailure(directory);
        else if (mode == "once") testPagesCollectedOnce(directory);
        else if (mode == "piece_since") testPieceSince(directory);
        else if (mode == "driver_store_shader") testDriverStoreAfterShaderDelta(directory);
        else if (mode == "pending_result") testPendingResultRead(directory);
        else if (mode == "partly_covered") testPartlyCoveredBlock(directory);
        else if (mode == "late_clear") testLateClear(directory);
        else throw std::runtime_error("unknown test");
        if (mode != "reservation_failure" && mode != "gate_failure" && mode != "pacing_failure") AgcDriverShutdown_nid_postfix();
        std::puts("frame replay state test passed");
        return 0;
    } catch (const Inconclusive& error) {
        std::printf("skipped, %s\n", error.what());
        try {
            AgcDriverShutdown_nid_postfix();
        } catch (const std::exception& shutdown) {
            std::fprintf(stderr, "FAILED shutdown: %s\n", shutdown.what());
            return 1;
        }
        return 77;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        try { AgcDriverShutdown_nid_postfix(); } catch (...) {}
        return 1;
    }
}
