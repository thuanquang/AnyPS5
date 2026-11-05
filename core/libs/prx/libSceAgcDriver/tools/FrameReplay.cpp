#include "prx/libSceAgcDriver/State/include/Configuration.hpp"
#include "prx/libSceAgcDriver/Execution/include/DisplayBuffer.hpp"
#include "prx/libSceAgcDriver/Execution/include/Driver.hpp"
#include "prx/libSceAgcDriver/Execution/include/FrameCapture.hpp"
#include "prx/libSceAgcDriver/Execution/include/FrameDumpIo.hpp"
#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "prx/libSceAgcDriver/Execution/include/Pm4.hpp"
#include "prx/libSceAgcDriver/Execution/include/VideoOutput.hpp"
#include "prx/libSceAgcDriver/Submit/include/Acb.hpp"
#include "prx/libSceAgcDriver/Submit/include/Dcb.hpp"
#include "prx/libc/include/GuestAllocations.hpp"
#include "prx/libc/include/GuestArena.hpp"
#include "SceTypes.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif
#ifdef __linux__
#include "prx/libc/include/specifics/linux/HostMapping.hpp"
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace {

using AgcDriver::FrameCall;
using AgcDriver::FrameCallKind;
using AgcDriver::FrameCaptureReader;
using AgcDriver::FrameFloorFlag;
using AgcDriver::FrameMemoryChunk;
using AgcDriver::FrameMemoryKind;
using AgcDriver::FrameRangeAccess;
using AgcDriver::FrameRangeKind;
using Clock = std::chrono::steady_clock;

struct Dump {
    std::uint64_t address = 0;
    std::uint64_t bytes = 0;
    std::filesystem::path path;
};

struct DepthProfile {
    std::filesystem::path path;
    std::vector<std::string> names;
    std::vector<std::uint64_t> bounds, observed;
};

struct Options {
    std::filesystem::path directory;
    std::vector<Dump> dumps;
    std::filesystem::path frames;
    bool pace = true;
    bool strict = false;
    std::chrono::milliseconds paceTimeout{10000};
    std::chrono::milliseconds releaseAfter{3};
    std::chrono::milliseconds drainTimeout{10000};
    std::chrono::milliseconds busyTimeout{600000};
    bool release = true;
    bool synthesize = true;
    std::size_t repeat = 1;
    bool shaders = false;
    std::filesystem::path shaderCode;
    std::size_t submits = 0;
    bool traceSerials = false;
    bool coldPipelines = false;
    std::filesystem::path flipHashes;
    std::filesystem::path expectedFlipHashes;
    std::filesystem::path depthFile;
    std::optional<DepthProfile> depth;
    std::optional<std::uint64_t> depthCap, depthProbe;
    std::vector<std::string> depthHold;
};

std::uint64_t contentHash(const std::byte* bytes, std::size_t size);

struct FlipRange {
    std::uint64_t address = 0;
    std::size_t bytes = 0;
    std::size_t alignment = 1;
    bool keys = false;
};

struct FlipHash {
    std::uint64_t flip = 0;
    std::uint64_t address = 0;
    std::uint64_t hash = 0;
};

thread_local bool issuing = false;

std::string addressText(std::uint64_t value) {
    char text[24];
    std::snprintf(text, sizeof(text), "0x%llx", static_cast<unsigned long long>(value));
    return text;
}

std::string hashText(std::uint64_t value) {
    char text[24];
    std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(value));
    return text;
}

class Frames {
public:
    void MarkReady() {
        std::lock_guard lock(mutex);
        ready.push_back(Clock::now());
    }

    void Failed(std::exception_ptr error) {
        std::lock_guard lock(mutex);
        if (!failure) failure = error;
    }

    std::vector<Clock::time_point> Times() const {
        std::lock_guard lock(mutex);
        return ready;
    }

    std::exception_ptr Failure() const {
        std::lock_guard lock(mutex);
        return failure;
    }

    void HashFlips(std::vector<FlipRange> ranges, std::vector<FlipHash> expected, std::string source) {
        std::lock_guard lock(mutex);
        flipRanges = std::move(ranges);
        expectedHashes = std::move(expected);
        expectedSource = std::move(source);
    }

    void StartPass(std::size_t number) {
        std::lock_guard lock(mutex);
        pass = number;
        passFlips = 0;
    }

    void HashFlip() {
        if (flipRanges.empty()) return;
        std::uint64_t flip = 0;
        std::size_t number = 0;
        {
            std::lock_guard lock(mutex);
            if (!pass || failure || (*pass != 0 && expectedHashes.empty())) return;
            flip = ++passFlips;
            number = *pass + 1;
        }
        const auto began = Clock::now();
        try {
            if (issuing) throw std::runtime_error("frame replay: flip " + std::to_string(flip) + " of pass " + std::to_string(number) + " was shown on the replaying thread before the work queued ahead of it ran (a CPU flip from a thread with no graphics submission), so its display buffers cannot be hashed at the flip");
            std::vector<FlipHash> hashes;
            for (const auto& range : flipRanges) {
                AgcDriver::GuestMemory::FlushGpuWrites(range.address, range.bytes);
                const auto* bytes = reinterpret_cast<const std::byte*>(static_cast<std::uintptr_t>(range.address));
                AgcDriver::GuestMemory::CheckRange(bytes, range.bytes, range.alignment);
                hashes.push_back({flip, range.address, contentHash(bytes, range.bytes)});
            }
            std::lock_guard lock(mutex);
            ++hashedFlips;
            hashNs += std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - began).count();
            if (number == 1) written.insert(written.end(), hashes.begin(), hashes.end());
            if (expectedHashes.empty()) return;
            const auto first = (flip - 1) * flipRanges.size();
            if (first >= expectedHashes.size()) throw std::runtime_error("frame replay: flip " + std::to_string(flip) + " of pass " + std::to_string(number) + " is past the " + std::to_string(expectedHashes.size() / flipRanges.size()) + " flips that " + expectedSource + " lists");
            for (std::size_t index = 0; index < hashes.size(); ++index) {
                const auto expected = expectedHashes[first + index].hash;
                if (hashes[index].hash == expected) continue;
                const bool keys = flipRanges[index].keys;
                throw std::runtime_error("frame replay: at flip " + std::to_string(flip) + " of pass " + std::to_string(number) + (keys ? " the DCC keys at " : " the display buffer at ") + addressText(hashes[index].address) + (keys ? " hash to " : " hashes to ") + hashText(hashes[index].hash) + ", but " + expectedSource + " has " + hashText(expected));
            }
        } catch (...) {
            Failed(std::current_exception());
        }
    }

    void EndPass() {
        std::lock_guard lock(mutex);
        if (flipRanges.empty() || expectedHashes.empty() || !pass || failure) return;
        const auto listed = expectedHashes.size() / flipRanges.size();
        if (passFlips != listed) failure = std::make_exception_ptr(std::runtime_error("frame replay: pass " + std::to_string(*pass + 1) + " presented " + std::to_string(passFlips) + " flips, but " + expectedSource + " lists " + std::to_string(listed)));
    }

    std::vector<FlipHash> Written() const {
        std::lock_guard lock(mutex);
        return written;
    }

    std::uint64_t HashedFlips() const {
        std::lock_guard lock(mutex);
        return hashedFlips;
    }

    double HashSeconds() const {
        std::lock_guard lock(mutex);
        return static_cast<double>(hashNs) / 1e9;
    }

private:
    mutable std::mutex mutex;
    std::vector<Clock::time_point> ready;
    std::exception_ptr failure;
    std::vector<FlipRange> flipRanges;
    std::vector<FlipHash> expectedHashes;
    std::string expectedSource;
    std::vector<FlipHash> written;
    std::optional<std::size_t> pass;
    std::uint64_t passFlips = 0;
    std::uint64_t hashedFlips = 0;
    std::int64_t hashNs = 0;
};

class ReplayFlip final : public AgcDriver::IFlipRequest {
public:
    explicit ReplayFlip(Frames& frames) : frames(frames) {}
    void GpuReady(const std::shared_ptr<AgcDriver::FrameTiming>&) override {
        frames.HashFlip();
        frames.MarkReady();
    }
    void Fail(std::exception_ptr error) noexcept override { frames.Failed(error); }

private:
    Frames& frames;
};

class ReplayRenderingWait final : public AgcDriver::IRenderingWait {
public:
    void Wait() override {}
};

constexpr std::uint32_t OverlayOutput = 2;

class ReplayOutput final : public AgcDriver::IVideoOutput {
public:
    explicit ReplayOutput(Frames& frames) : frames(frames) {}
    std::shared_ptr<AgcDriver::IFlipRequest> Reserve(const AgcDriver::FlipInfo& info) override {
        if (info.handle == OverlayOutput) throw std::runtime_error("frame replay: the capture flips video output 2, the overlay bus, which a live run does not present, so the replay cannot count its flips as presents");
        return std::make_shared<ReplayFlip>(frames);
    }
    std::shared_ptr<AgcDriver::IRenderingWait> CaptureRenderingWait(std::uint32_t) override { return std::make_shared<ReplayRenderingWait>(); }
    void Fail(std::exception_ptr error) noexcept override { frames.Failed(error); }

private:
    Frames& frames;
};

std::optional<std::uint64_t> headerValue(const std::string& header, std::string_view key) {
    const auto quoted = "\"" + std::string(key) + "\":";
    const auto at = header.find(quoted);
    if (at == std::string::npos) return std::nullopt;
    const auto* const end = header.data() + header.size();
    std::uint64_t value = 0;
    const auto [last, error] = std::from_chars(header.data() + at + quoted.size(), end, value);
    if (error != std::errc{} || last == end || std::string_view(",} \t\r\n").find(*last) == std::string_view::npos) throw std::runtime_error("frame replay: the capture header's " + std::string(key) + " is not a number");
    return value;
}

std::uint64_t headerNumber(const std::string& header, std::string_view key) {
    const auto value = headerValue(header, key);
    if (!value) throw std::runtime_error("frame replay: the capture header has no " + std::string(key));
    return *value;
}

void requireVerifiedDeltas(const FrameCaptureReader& reader) {
    if (reader.Version() < AgcDriver::InFlightCallsVersion) return;
    const auto& header = reader.Header();
    if (header.empty()) throw std::runtime_error("frame replay: capture.json is missing or empty, so the capture never ended and its last memory deltas may be partial or never verified");
    const auto stale = headerValue(header, "stale_delta");
    if (stale.value_or(0) != 0) throw std::runtime_error("frame replay: the capture stopped because a memory delta was read while submissions that stored into it were in flight (stale_delta " + std::to_string(*stale) + ")");
    const auto inFlight = std::count_if(reader.Calls().begin(), reader.Calls().end(), [](const FrameCall& call) { return call.kind == FrameCallKind::Memory && (call.flags & AgcDriver::InFlightDeltaFlag) != 0; });
    if (inFlight == 0) return;
    const auto refuse = [&](const std::string& reason) { return std::runtime_error("frame replay: " + std::to_string(inFlight) + " memory deltas were read while submissions were in flight, but " + reason + ", so stores those submissions made into them later were never checked"); };
    if (!stale) throw refuse("capture.json has no stale_delta");
    const auto unverified = headerValue(header, "unverified_deltas");
    if (!unverified) throw refuse("capture.json has no unverified_deltas");
    if (*unverified != 0) throw refuse("capture.json has unverified_deltas " + std::to_string(*unverified));
}

void requireMemoryInRanges(const FrameCaptureReader& reader) {
    std::map<std::uint64_t, std::uint64_t> mapped;
    for (const auto& chunk : reader.Chunks()) {
        if (chunk.kind != FrameMemoryKind::Range) continue;
        auto& end = mapped[chunk.address];
        end = std::max(end, chunk.address + chunk.bytes);
    }
    for (const auto& chunk : reader.Chunks()) {
        if (chunk.kind != FrameMemoryKind::Snapshot && chunk.kind != FrameMemoryKind::Delta) continue;
        const auto end = chunk.address + chunk.bytes;
        auto cursor = chunk.address;
        auto range = mapped.upper_bound(cursor);
        if (range != mapped.begin()) --range;
        for (; range != mapped.end() && range->first <= cursor && cursor < end; ++range) cursor = std::max(cursor, range->second);
        if (cursor >= end) continue;
        char where[96];
        std::snprintf(where, sizeof(where), " at 0x%llx (%llu bytes) reaches 0x%llx", static_cast<unsigned long long>(chunk.address), static_cast<unsigned long long>(chunk.bytes), static_cast<unsigned long long>(cursor));
        throw std::runtime_error(std::string("frame replay: the memory ") + (chunk.kind == FrameMemoryKind::Delta ? "delta of group " + std::to_string(chunk.group) : std::string("snapshot")) + where + ", which no range of the capture maps");
    }
}

void requireRecordedSerials(const FrameCaptureReader& reader) {
    if (reader.Version() < AgcDriver::InFlightCallsVersion) return;
    const auto& calls = reader.Calls();
    const auto start = std::find_if(calls.begin(), calls.end(), [](const FrameCall& call) { return call.kind == FrameCallKind::FrameStart; });
    if (start == calls.end()) return;
    auto last = calls.end();
    for (auto call = start; call != calls.end(); ++call) {
        if (call->kind == FrameCallKind::Memory && (call->flags & AgcDriver::InFlightDeltaFlag) != 0) last = call;
    }
    if (last == calls.end()) return;
    for (auto call = start; call != last; ++call) {
        if (call->kind != FrameCallKind::Submit && call->kind != FrameCallKind::Flip && call->kind != FrameCallKind::Suspend) continue;
        if (call->values.size() == (call->kind == FrameCallKind::Flip ? 5u : 1u)) continue;
        throw std::runtime_error("frame replay: call " + std::to_string(call->ordinal) + " (" + std::string(AgcDriver::FrameCallName(call->kind)) + ") has no recorded serial, so the submissions in flight at the memory delta of call " + std::to_string(last->ordinal) + " cannot be mapped to this replay's");
    }
}

std::optional<std::uint64_t> titleNs(const FrameCaptureReader& reader, const FrameCall& call) {
    if (reader.Version() < AgcDriver::ExitCallsVersion || !AgcDriver::FrameTitleCall(call.kind) || call.previousExitNs == 0) return std::nullopt;
    return call.timeNs - call.previousExitNs;
}

constexpr std::string_view DepthProfileMagic = "aps5-depth-profile 1";
constexpr std::string_view DepthWithoutFrameStart = "frame replay: --depth-profile needs a capture with a frame start, so that its gates can map the capture's serials to this replay's";
constexpr std::string_view DepthUnmapped = ", so --depth-profile cannot map the floors and bounds of the memory deltas after it to this replay's submissions";

DepthProfile readDepthProfile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) throw std::runtime_error("frame replay: cannot open " + path.string());
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const auto magic = std::string(DepthProfileMagic) + '\n';
    if (!text.starts_with(magic)) throw std::runtime_error("frame replay: " + path.string() + " does not start with the line '" + std::string(DepthProfileMagic) + "'");
    const auto parse = [](std::string_view line, std::string_view& name, std::uint64_t& bound, std::uint64_t& observed) {
        const auto space = line.find(' ');
        if (space == 0 || space == std::string_view::npos) return false;
        name = line.substr(0, space);
        const auto* cursor = line.data() + space;
        const auto* const end = line.data() + line.size();
        const auto number = [&](std::uint64_t& value) {
            if (cursor == end || *cursor != ' ') return false;
            const auto [last, error] = std::from_chars(cursor + 1, end, value);
            if (error != std::errc{}) return false;
            cursor = last;
            return true;
        };
        return number(bound) && number(observed) && cursor == end;
    };
    DepthProfile profile{path, {}, {}, {}};
    for (std::size_t begin = magic.size(), number = 2; begin < text.size(); ++number) {
        const auto end = text.find('\n', begin);
        const auto where = "frame replay: line " + std::to_string(number) + " of " + path.string();
        std::string_view name;
        std::uint64_t bound = 0;
        std::uint64_t observed = 0;
        if (end == std::string::npos || !parse(std::string_view(text).substr(begin, end - begin), name, bound, observed)) throw std::runtime_error(where + " is not '<position> <bound> <observed>'");
        const auto expected = profile.names.empty() ? std::string("suspend") : "s" + std::to_string(profile.names.size());
        if (name != expected) throw std::runtime_error(where + " names " + std::string(name) + " where its frame needs " + expected);
        if (bound > observed) throw std::runtime_error(where + " bounds " + expected + " at " + std::to_string(bound) + ", above the " + std::to_string(observed) + " it observed");
        profile.names.push_back(expected);
        profile.bounds.push_back(bound);
        profile.observed.push_back(observed);
        begin = end + 1;
    }
    if (profile.names.size() < 2) throw std::runtime_error("frame replay: " + path.string() + " lists no submission after its suspend point");
    return profile;
}

struct DepthPlan {
    std::vector<std::uint32_t> position, frame;
    std::vector<std::size_t> next;
    std::vector<std::uint64_t> ceiling;
    std::size_t frames = 0;
    static constexpr std::uint32_t NoPosition = ~0u;
    static constexpr std::size_t Tail = ~std::size_t{0};
};

DepthPlan planDepth(std::span<const FrameCall> calls, const DepthProfile& profile) {
    const auto submissions = profile.names.size() - 1;
    DepthPlan plan;
    plan.position.assign(calls.size(), DepthPlan::NoPosition);
    plan.frame.assign(calls.size(), 0);
    plan.next.assign(calls.size(), DepthPlan::Tail);
    std::size_t opened = 0;
    std::size_t inFrame = 0;
    const auto unfit = [&](std::size_t count) {
        return std::runtime_error("frame replay: frame " + std::to_string(plan.frames) + " of the captured frames (from call " + std::to_string(calls[opened].ordinal) + ") has " + std::to_string(count) + " queue 0 submissions where " + profile.path.string() + " lists s1-s" + std::to_string(submissions));
    };
    for (std::size_t index = 0; index < calls.size(); ++index) {
        const auto& call = calls[index];
        if (call.thread != calls.front().thread) throw std::runtime_error("frame replay: --depth-profile replays one calling thread, but call " + std::to_string(calls.front().ordinal) + " is on thread " + std::to_string(calls.front().thread) + " and call " + std::to_string(call.ordinal) + " on thread " + std::to_string(call.thread));
        if (call.kind == FrameCallKind::Flip || call.kind == FrameCallKind::ReserveEopFlip) throw std::runtime_error("frame replay: --depth-profile does not order flips or EOP flip reservations inside the captured frames, and call " + std::to_string(call.ordinal) + " is a " + std::string(AgcDriver::FrameCallName(call.kind)));
        plan.frame[index] = static_cast<std::uint32_t>(plan.frames);
        if (call.kind != FrameCallKind::Suspend && call.kind != FrameCallKind::Submit) continue;
        if (call.values.size() != 1 || call.values.back() == 0) throw std::runtime_error("frame replay: call " + std::to_string(call.ordinal) + " (" + std::string(AgcDriver::FrameCallName(call.kind)) + ") has no recorded serial, so --depth-profile cannot map the floors and bounds that wait for it");
        if (call.kind == FrameCallKind::Suspend) {
            if (plan.frames != 0 && inFrame != submissions) throw unfit(inFrame);
            ++plan.frames;
            opened = index;
            inFrame = 0;
            plan.position[index] = 0;
            plan.frame[index] = static_cast<std::uint32_t>(plan.frames);
            continue;
        }
        if (call.queue != 0) throw std::runtime_error("frame replay: call " + std::to_string(call.ordinal) + " submits to queue " + addressText(call.queue) + ", and --depth-profile replays queue 0 submissions only");
        if (plan.frames == 0) throw std::runtime_error("frame replay: call " + std::to_string(call.ordinal) + " (" + std::string(AgcDriver::FrameCallName(call.kind)) + ") comes before the first suspend point of the captured frames, so it has no position in " + profile.path.string());
        if (inFrame == submissions) {
            auto count = inFrame;
            for (auto later = index; later < calls.size() && calls[later].kind != FrameCallKind::Suspend; ++later) count += calls[later].kind == FrameCallKind::Submit && calls[later].queue == 0 ? 1 : 0;
            throw unfit(count);
        }
        plan.position[index] = static_cast<std::uint32_t>(++inFrame);
    }
    if (plan.frames == 0) throw std::runtime_error("frame replay: the captured frames have no suspend point, so the frame of " + profile.path.string() + " never starts");
    if (inFrame != submissions) throw unfit(inFrame);
    for (auto index = calls.size(), paced = DepthPlan::Tail; index > 0; --index) {
        plan.next[index - 1] = paced;
        if (plan.position[index - 1] != DepthPlan::NoPosition) paced = index - 1;
    }
    return plan;
}

enum class DepthCause : std::uint8_t { Forced, Tail, Parked, StoreUnknown, WaitIncomplete, WaitLost, WaitOrder, Releases, Pending, Ahead };

enum class Restore { Held, AtDepth };

constexpr std::size_t HeldCauses = static_cast<std::size_t>(DepthCause::Ahead);
constexpr std::array<std::string_view, HeldCauses> HeldNames{"forced", "tail", "parked", "store_unknown", "wait_incomplete", "wait_lost", "wait_order", "releases", "pending"};
constexpr std::array<std::string_view, HeldCauses> HeldReasons{"held by --depth-hold", "the last delta", "read parked by the capture", "unknown store floor", "wait after an unfinished submission", "wait value lost", "wait order lost", "releases a wait", "GPU results pending"};
constexpr std::array<std::string_view, 8> ReadKindNames{"DispatchElement", "GpuCopy", "AddressBased", "Indirect", "StorageUpload", "CopySource", "DrawInput", "AddressBasedReadOnly"};

std::string readKindName(std::uint32_t kind) {
    return kind < ReadKindNames.size() ? std::string(ReadKindNames[kind]) : "kind " + std::to_string(kind);
}

struct PositionCensus {
    std::vector<std::uint64_t> issue, restore, ceiling, issueBound;
    std::uint64_t byDepth = 0, byStore = 0, byWait = 0, cpuWaits = 0, gpuWaits = 0;
    std::array<std::uint64_t, HeldCauses> held{};
};

struct MemoryFloors {
    std::optional<std::uint64_t> store, wait;
    std::uint32_t flags = 0;
};

MemoryFloors memoryFloors(const FrameCall& call) {
    MemoryFloors floors;
    floors.flags = call.floorFlags;
    if ((call.floorFlags & FrameFloorFlag::StoreUnknown) == 0) floors.store = call.storeFloor;
    if ((call.floorFlags & FrameFloorFlag::WaitUnknown) == 0) floors.wait = call.waitFloor;
    return floors;
}

struct SplitTarget {
    std::uint64_t begin = 0, end = 0, serial = 0;
    bool wait = false;
};

struct SplitAccess {
    std::uint64_t begin = 0, end = 0;
    enum class Kind : std::uint8_t { FullStore, Store, Wait, Read } kind = Kind::Read;
};

struct SplitScan {
    std::vector<SplitTarget> targets;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> acceptReads, indirect;
    std::vector<SplitAccess> accesses;
    bool rewind = false, opaque = false;
};

std::uint64_t packetAddress(std::uint32_t low, std::uint32_t high) {
    return low | (static_cast<std::uint64_t>(high) << 32u);
}

SplitScan scanSubmission(std::span<const std::uint32_t> stream, std::uint64_t address) {
    namespace Pm4 = AgcDriver::Pm4;
    SplitScan scan;
    if (!stream.empty()) scan.acceptReads.emplace_back(address, address + stream.size_bytes());
    std::vector<std::uint64_t> labels;
    std::size_t budget = std::size_t{1} << 26u;
    const auto readAtAccept = [&](std::uint64_t begin, std::uint64_t bytes) {
        if (bytes == 0) return;
        scan.acceptReads.emplace_back(begin, begin + bytes);
        scan.indirect.emplace_back(begin, begin + bytes);
    };
    const auto access = [&](std::uint64_t begin, std::uint64_t bytes, SplitAccess::Kind kind) {
        if (bytes != 0) scan.accesses.push_back(SplitAccess{begin, begin + bytes, kind});
    };
    const auto walk = [&](const auto& self, std::span<const std::uint32_t> words, bool conditional) -> bool {
        if (words.size() > budget) {
            scan.opaque = true;
            return true;
        }
        budget -= words.size();
        std::vector<std::pair<std::size_t, std::size_t>> guarded;
        for (std::size_t cursor = 0; cursor < words.size();) {
            const auto header = words[cursor];
            if (Pm4::FillerPacket(header)) {
                ++cursor;
                continue;
            }
            const auto count = Pm4::PacketWords(header);
            if ((header >> 30u) != 3u || count > words.size() - cursor) {
                scan.opaque = true;
                return true;
            }
            const auto packet = words.subspan(cursor, count);
            const auto opcode = (header >> 8u) & 0xffu;
            const bool inside = conditional || std::any_of(guarded.begin(), guarded.end(), [cursor](const auto& range) { return range.first <= cursor && cursor < range.second; });
            cursor += count;
            if (opcode == 0x59u) {
                scan.rewind = true;
                return true;
            }
            if (opcode == 0x3fu) {
                if (count != 4 && count != 14) {
                    scan.opaque = true;
                    return true;
                }
                const auto target = count == 4 ? packetAddress(packet[1] & ~3u, packet[2] & 0xffffu) : packetAddress(packet[8] & ~3u, packet[9] & 0xffffu);
                const std::size_t targetWords = (count == 4 ? packet[3] : packet[10]) & 0xfffffu;
                if (targetWords != 0) {
                    const auto* const commands = reinterpret_cast<const std::uint32_t*>(static_cast<std::uintptr_t>(target));
                    if (!AgcDriver::GuestMemory::Accessible(commands, targetWords * sizeof(std::uint32_t))) {
                        scan.opaque = true;
                        return true;
                    }
                    readAtAccept(target, targetWords * sizeof(std::uint32_t));
                    if (self(self, std::span(commands, targetWords), inside || count == 14 || Pm4::Predicated(header))) return true;
                }
                if (count == 4 && (packet[3] & (1u << 20u)) != 0) return false;
                continue;
            }
            try {
                if (opcode == 0x22u && count == 5) {
                    access(packetAddress(packet[1], packet[2]), 4, SplitAccess::Kind::Read);
                    guarded.emplace_back(cursor, cursor + Pm4::ConditionalWords(packet));
                } else if (opcode == 0x20u && count == 4 && ((packet[1] >> 16u) & 7u) != 0) {
                    access(packetAddress(packet[2], packet[3]), ((packet[1] >> 16u) & 7u) == 3 ? 8 : 4, SplitAccess::Kind::Read);
                } else if (opcode == 0x45u && count == 9) {
                    access(packetAddress(packet[2], packet[3]), 4, SplitAccess::Kind::Read);
                } else if (Pm4::IndirectRegisterOpcode(opcode) && count == 5) {
                    readAtAccept(packetAddress(packet[1], packet[2]), static_cast<std::uint64_t>(packet[4]) * (packet[3] == 0x80000000u ? 8 : 4));
                } else if ((opcode == 0x3cu || opcode == 0x93u) && count >= 2 && ((packet[1] >> 4u) & 3u) == 1u) {
                    const auto wait = Pm4::DecodeWait(packet);
                    scan.targets.push_back(SplitTarget{wait.address, wait.address + wait.bytes, 0, true});
                    access(wait.address, wait.bytes, SplitAccess::Kind::Wait);
                    bool labelled = false;
                    for (std::uint64_t dword = wait.address; dword < wait.address + wait.bytes; dword += 4) labelled = labelled || std::find(labels.begin(), labels.end(), dword) != labels.end();
                    if (!labelled) readAtAccept(wait.address, wait.bytes);
                } else if (opcode == 0x39u && count >= 3) {
                    const auto semaphore = packetAddress(packet[1], packet[2]);
                    scan.targets.push_back(SplitTarget{semaphore, semaphore + 8, 0, true});
                } else if (opcode == 0x50u && count == 7 && (Pm4::DmaSource(packet) == 0 || Pm4::DmaSource(packet) == 3)) {
                    access(packetAddress(packet[2], packet[3]), Pm4::DmaBytes(packet), SplitAccess::Kind::Read);
                } else if (opcode == 0x40u && count == 6) {
                    const auto source = Pm4::CopyDataSource(packet);
                    if (source != 0 && source != Pm4::CopyDataGpuClockSource && !Pm4::CopyDataImmediate(source)) access(packetAddress(packet[2], packet[3]), Pm4::CopyDataBytes(packet), SplitAccess::Kind::Read);
                }
                if (opcode == 0x49u || opcode == 0x37u) {
                    if (const auto label = Pm4::DecodeLabelWrite(packet); label && label->address % 4 == 0 && label->Bytes().size() <= 64) {
                        for (std::size_t offset = 0; offset < label->Bytes().size(); offset += 4) labels.push_back(label->address + offset);
                    }
                }
                if (opcode == 0x39u && count < 3) {
                    scan.opaque = true;
                    continue;
                }
                for (const auto& store : Pm4::DecodeStores(packet)) {
                    if (store.bytes == 0) continue;
                    scan.targets.push_back(SplitTarget{store.address, store.address + store.bytes, 0, false});
                    access(store.address, store.bytes, !store.conditional && !Pm4::Predicated(header) && !inside && opcode != 0x39u ? SplitAccess::Kind::FullStore : SplitAccess::Kind::Store);
                }
            } catch (const std::exception&) {
                scan.opaque = true;
            }
        }
        return false;
    };
    walk(walk, stream, false);
    return scan;
}

void requireDepthOptions(const Options& options) {
    if (options.release) throw std::runtime_error("frame replay: --depth-profile needs --no-release: a replay at depth never writes recorded or synthesized values to release a wait");
}

void requireDepthCapture(const FrameCaptureReader& reader, const Options& options) {
    if (reader.Version() < AgcDriver::ReleasesCallsVersion) throw std::runtime_error("frame replay: --depth-profile needs a capture whose memory deltas carry store and wait floors and mark the waits they release (calls format " + std::to_string(AgcDriver::ReleasesCallsVersion) + " or later), and this one has calls format " + std::to_string(reader.Version()));
    if (!options.synthesize) {
        const auto lost = std::count_if(reader.Calls().begin(), reader.Calls().end(), [](const FrameCall& call) { return call.kind == FrameCallKind::Memory && (call.floorFlags & FrameFloorFlag::WaitValueLost) != 0; });
        if (lost != 0) throw std::runtime_error("frame replay: " + std::to_string(lost) + " memory deltas restore dwords that submissions passed waits on with values no delta or noted driver store holds (floor flags " + addressText(FrameFloorFlag::WaitValueLost) + "); only a synthesized release replays such a wait, and --no-synthesize forbids it");
    }
    const auto& names = options.depth->names;
    for (const auto& position : options.depthHold) {
        if (std::find(names.begin(), names.end(), position) == names.end()) throw std::runtime_error("frame replay: --depth-hold names " + position + ", which " + options.depth->path.string() + " does not list");
    }
}

void restore(const FrameCaptureReader& reader, const FrameMemoryChunk& chunk) {
    reader.Read(chunk, std::span(reinterpret_cast<std::byte*>(static_cast<std::uintptr_t>(chunk.address)), static_cast<std::size_t>(chunk.bytes)));
}

bool committedWritable(std::uint64_t address, std::uint64_t bytes) {
#ifdef _WIN32
    for (auto at = address; at < address + bytes;) {
        MEMORY_BASIC_INFORMATION info{};
        if (VirtualQuery(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(at)), &info, sizeof(info)) == 0 || info.State != MEM_COMMIT) return false;
        const auto access = info.Protect & 0xffu;
        if ((info.Protect & PAGE_GUARD) != 0 || (access != PAGE_READWRITE && access != PAGE_EXECUTE_READWRITE)) return false;
        at = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
    }
    return true;
#elif defined(__linux__)
    auto cursor = address;
    HostMapping::Walk(address, address + bytes, "frame replay", [&](const HostMapping::Mapping& mapping) {
        if (mapping.start > cursor || !mapping.readable || !mapping.writable) return;
        cursor = std::max(cursor, mapping.end);
    });
    return cursor >= address + bytes;
#else
    static_cast<void>(address);
    static_cast<void>(bytes);
    return false;
#endif
}

void commit(const FrameMemoryChunk& chunk) {
    auto* const pointer = reinterpret_cast<void*>(static_cast<std::uintptr_t>(chunk.address));
    const auto bytes = static_cast<std::size_t>(chunk.bytes);
#ifdef __linux__
    const auto page = static_cast<std::uint64_t>(sysconf(_SC_PAGESIZE));
    const auto begin = chunk.address & ~(page - 1);
    const auto end = (chunk.address + chunk.bytes + page - 1) & ~(page - 1);
    if (begin != chunk.address || end != chunk.address + chunk.bytes) {
        GuestArena::GuestArenaSetProtection_nid_postfix(static_cast<std::uintptr_t>(begin), static_cast<std::size_t>(end - begin), 0x04);
        return;
    }
#endif
    GuestArena::GuestArenaCommit_nid_postfix(pointer, bytes, 0x04, bytes);
}

std::pair<std::uint64_t, std::uint64_t> progress();

struct Skipped {
    std::vector<std::pair<std::uint64_t, std::uint64_t>> ranges;
    std::uint64_t bytes = 0;

    void Add(const FrameMemoryChunk& chunk) {
        ranges.emplace_back(chunk.address, chunk.address + chunk.bytes);
        bytes += chunk.bytes;
    }

    bool Overlaps(const FrameMemoryChunk& chunk) const {
        return std::any_of(ranges.begin(), ranges.end(), [&](const auto& range) { return chunk.address < range.second && range.first < chunk.address + chunk.bytes; });
    }
};

void mapHostRanges(const FrameCaptureReader& reader, Skipped& skipped) {
    std::vector<const FrameMemoryChunk*> host;
    for (const auto& chunk : reader.Chunks()) {
        if (chunk.kind == FrameMemoryKind::Range && chunk.rangeKind == FrameRangeKind::Host) host.push_back(&chunk);
    }
#ifdef _WIN32
    constexpr std::uint64_t Granularity = 64u * 1024u;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> envelopes;
    for (const auto* chunk : host) {
        const auto begin = chunk->address & ~(Granularity - 1);
        const auto end = (chunk->address + chunk->bytes + Granularity - 1) & ~(Granularity - 1);
        if (!envelopes.empty() && begin <= envelopes.back().second) envelopes.back().second = std::max(envelopes.back().second, end);
        else envelopes.emplace_back(begin, end);
    }
    std::vector<std::pair<std::uint64_t, std::uint64_t>> refused;
    for (const auto& [begin, end] : envelopes) {
        if (VirtualAlloc(reinterpret_cast<void*>(static_cast<std::uintptr_t>(begin)), static_cast<SIZE_T>(end - begin), MEM_RESERVE, PAGE_NOACCESS) == nullptr) refused.emplace_back(begin, end);
    }
    for (const auto* chunk : host) {
        const bool blocked = std::any_of(refused.begin(), refused.end(), [&](const auto& range) { return chunk->address < range.second && range.first < chunk->address + chunk->bytes; });
        if (!blocked && VirtualAlloc(reinterpret_cast<void*>(static_cast<std::uintptr_t>(chunk->address)), static_cast<SIZE_T>(chunk->bytes), MEM_COMMIT, PAGE_READWRITE) != nullptr) continue;
        if ((chunk->access & FrameRangeAccess::Gpu) != 0) throw std::runtime_error("frame replay: cannot map the GPU-visible host range at " + std::to_string(chunk->address));
        skipped.Add(*chunk);
    }
#elif defined(__linux__)
    const auto page = static_cast<std::uint64_t>(sysconf(_SC_PAGESIZE));
    std::sort(host.begin(), host.end(), [](const auto* left, const auto* right) { return left->address < right->address; });
    std::vector<std::pair<std::uint64_t, std::uint64_t>> envelopes;
    for (const auto* chunk : host) {
        const auto begin = chunk->address & ~(page - 1);
        const auto end = (chunk->address + chunk->bytes + page - 1) & ~(page - 1);
        if (!envelopes.empty() && begin < envelopes.back().second) envelopes.back().second = std::max(envelopes.back().second, end);
        else envelopes.emplace_back(begin, end);
    }
    std::vector<std::pair<std::uint64_t, std::uint64_t>> refused;
    for (const auto& [begin, end] : envelopes) {
        auto* const wanted = reinterpret_cast<void*>(static_cast<std::uintptr_t>(begin));
        const auto bytes = static_cast<std::size_t>(end - begin);
        void* const placed = mmap(wanted, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
        if (placed == wanted) continue;
        if (placed != MAP_FAILED) munmap(placed, bytes);
        refused.emplace_back(begin, end);
    }
    for (const auto* chunk : host) {
        const bool blocked = std::any_of(refused.begin(), refused.end(), [&](const auto& range) { return chunk->address < range.second && range.first < chunk->address + chunk->bytes; });
        if (!blocked) continue;
        if ((chunk->access & FrameRangeAccess::Gpu) != 0) throw std::runtime_error("frame replay: cannot map the GPU-visible host range at " + std::to_string(chunk->address));
        skipped.Add(*chunk);
    }
#else
    for (const auto* chunk : host) skipped.Add(*chunk);
#endif
}

void restoreSnapshot(const FrameCaptureReader& reader, const Skipped& skipped) {
    std::vector<const FrameMemoryChunk*> snapshot;
    for (const auto& chunk : reader.Chunks()) {
        if (chunk.kind == FrameMemoryKind::Snapshot && !skipped.Overlaps(chunk)) snapshot.push_back(&chunk);
    }
    const auto workers = std::max<std::size_t>(1, std::thread::hardware_concurrency());
    for (std::size_t first = 0; first < snapshot.size(); first += workers) {
        std::vector<std::future<void>> pending;
        for (auto index = first; index < std::min(snapshot.size(), first + workers); ++index) {
            pending.push_back(std::async(std::launch::async, [&reader, chunk = snapshot[index]] { restore(reader, *chunk); }));
        }
        for (auto& future : pending) future.get();
    }
}

Skipped restoreMemory(const FrameCaptureReader& reader) {
    std::uintptr_t base = 0;
    std::size_t size = 0;
    GuestArena::GuestArenaRange_nid_postfix(&base, &size);
    if (!reader.Header().empty() && headerNumber(reader.Header(), "arena_base") != base) throw std::runtime_error("frame replay: the guest arena is at a different address than in the capture");
    requireVerifiedDeltas(reader);
    requireMemoryInRanges(reader);
    progress();
    Skipped skipped;
    mapHostRanges(reader, skipped);
    std::vector<bool> owned;
    for (const auto& chunk : reader.Chunks()) owned.push_back(chunk.kind == FrameMemoryKind::Range && chunk.rangeKind != FrameRangeKind::Host && committedWritable(chunk.address, chunk.bytes));
    {
        GuestAllocations::Mutation mutation;
        for (std::size_t index = 0; index < reader.Chunks().size(); ++index) {
            const auto& chunk = reader.Chunks()[index];
            if (chunk.kind != FrameMemoryKind::Range || chunk.rangeKind == FrameRangeKind::Host) continue;
            const bool gpu = (chunk.access & FrameRangeAccess::Gpu) != 0;
            if (owned[index]) {
                if (!gpu) skipped.Add(chunk);
                else std::memset(reinterpret_cast<void*>(static_cast<std::uintptr_t>(chunk.address)), 0, static_cast<std::size_t>(chunk.bytes));
                continue;
            }
            auto* const pointer = reinterpret_cast<void*>(static_cast<std::uintptr_t>(chunk.address));
            const auto bytes = static_cast<std::size_t>(chunk.bytes);
            try {
                GuestArena::GuestArenaMarkUsed_nid_postfix(pointer, bytes);
            } catch (const std::exception&) {
                if (gpu) throw;
                skipped.Add(chunk);
                continue;
            }
            commit(chunk);
            mutation.Add(pointer, bytes, (chunk.access & FrameRangeAccess::Readable) != 0, (chunk.access & FrameRangeAccess::Writable) != 0, gpu);
        }
    }
    restoreSnapshot(reader, skipped);
    return skipped;
}

void resetMemory(const FrameCaptureReader& reader, const Skipped& skipped) {
    AgcDriverWaitIdle_nid_postfix();
    for (const auto& chunk : reader.Chunks()) {
        if (chunk.kind == FrameMemoryKind::Range && !skipped.Overlaps(chunk)) std::memset(reinterpret_cast<void*>(static_cast<std::uintptr_t>(chunk.address)), 0, static_cast<std::size_t>(chunk.bytes));
    }
    restoreSnapshot(reader, skipped);
}

std::pair<std::uint64_t, std::uint64_t> progress() {
    std::uint64_t accepted = 0;
    std::uint64_t completed = 0;
    AgcDriverSerialProgress_nid_postfix(&accepted, &completed);
    return {accepted, completed};
}

std::optional<std::uint64_t> frameStartSerial(const FrameCaptureReader& reader) {
    const auto& calls = reader.Calls();
    const auto start = std::find_if(calls.begin(), calls.end(), [](const FrameCall& call) { return call.kind == FrameCallKind::FrameStart; });
    if (start == calls.end()) return std::nullopt;
    return start->accepted;
}

class DecodePool {
public:
    DecodePool() {
        const auto helpers = std::max<std::size_t>(1, std::thread::hardware_concurrency() / 2) - 1;
        try {
            for (std::size_t index = 0; index < helpers; ++index) threads.emplace_back([this] { serve(); });
        } catch (...) {
            stop();
            throw;
        }
    }

    DecodePool(const DecodePool&) = delete;
    DecodePool& operator=(const DecodePool&) = delete;

    ~DecodePool() {
        stop();
    }

    std::size_t Participants() const {
        return threads.size() + 1;
    }

    std::int64_t Run(std::size_t count, const std::function<void(std::size_t)>& task, std::vector<std::exception_ptr>& failures) {
        failures.assign(count, nullptr);
        {
            const std::lock_guard lock(mutex);
            job = &task;
            errors = &failures;
            total = count;
            next.store(0);
            spentNs = 0;
            inside = 1;
            ++round;
        }
        if (count > 1) wake.notify_all();
        take();
        std::unique_lock lock(mutex);
        idle.wait(lock, [&] { return inside == 0; });
        job = nullptr;
        errors = nullptr;
        return spentNs;
    }

private:
    void take() {
        const auto began = Clock::now();
        for (auto index = next.fetch_add(1); index < total; index = next.fetch_add(1)) {
            try {
                (*job)(index);
            } catch (...) {
                (*errors)[index] = std::current_exception();
            }
        }
        const auto spent = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - began).count();
        const std::lock_guard lock(mutex);
        spentNs += spent;
        if (--inside == 0) idle.notify_all();
    }

    void serve() {
        std::uint64_t seen = 0;
        std::unique_lock lock(mutex);
        while (true) {
            wake.wait(lock, [&] { return stopping || (inside != 0 && round != seen); });
            if (stopping) return;
            seen = round;
            ++inside;
            lock.unlock();
            take();
            lock.lock();
        }
    }

    void stop() {
        {
            const std::lock_guard lock(mutex);
            stopping = true;
        }
        wake.notify_all();
        for (auto& thread : threads) thread.join();
    }

    std::mutex mutex;
    std::condition_variable wake;
    std::condition_variable idle;
    std::vector<std::thread> threads;
    const std::function<void(std::size_t)>* job = nullptr;
    std::vector<std::exception_ptr>* errors = nullptr;
    std::size_t total = 0;
    std::atomic<std::size_t> next{0};
    std::int64_t spentNs = 0;
    std::uint64_t round = 0;
    std::size_t inside = 0;
    bool stopping = false;
};

struct Replay {
    const Options& options;
    const FrameCaptureReader& reader;
    Frames& frames = *new Frames;
    std::map<std::uint32_t, std::shared_ptr<ReplayOutput>> outputs;
    std::mutex memoryMutex;
    std::map<std::uint64_t, std::vector<const FrameMemoryChunk*>> groups;
    Skipped skipped;
    static constexpr std::size_t DecodeWindowBytes = 8 * AgcDriver::FrameCaptureWriter::ChunkBytes;
    std::vector<std::byte> decoded;
    std::vector<std::exception_ptr> decodeFailures;
    std::vector<std::vector<std::size_t>> rewindCandidates;
    std::unique_ptr<DecodePool> decoders;
    std::atomic<std::uint64_t> paceTimeouts{0};
    std::atomic<std::uint64_t> staleRegistrations{0};
    std::atomic<std::uint64_t> failedRegistrations{0};
    std::atomic<std::uint64_t> submits{0};
    std::atomic<std::uint64_t> deltaBytes{0};
    std::atomic<std::int64_t> paceWaitedNs{0};
    std::atomic<std::int64_t> heldWaitedNs{0};
    std::atomic<std::int64_t> inFlightWaitedNs{0};
    std::atomic<std::int64_t> restoredNs{0};
    std::atomic<std::int64_t> decodeWallNs{0};
    std::atomic<std::int64_t> decodeWorkNs{0};
    std::atomic<std::uint64_t> depthSamples{0};
    std::atomic<std::uint64_t> depthTotal{0};
    std::atomic<std::uint64_t> depthMax{0};
    std::atomic<std::int64_t> memoryGpuWaitedNs{0};
    std::atomic<std::uint64_t> memoryGpuDrains{0};
    std::atomic<std::uint64_t> memoryGpuWaits{0};
    std::atomic<std::uint64_t> memoryGpuSubmits{0};
    std::uint64_t gpuWaitedThrough = 0;
    struct GpuEpochs {
        bool tracked;
        explicit GpuEpochs(bool tracked) : tracked(tracked) {
            if (tracked) AgcDriverTrackGpuEpochs_nid_postfix(true);
        }
        ~GpuEpochs() {
            if (tracked) AgcDriverTrackGpuEpochs_nid_postfix(false);
        }
        GpuEpochs(const GpuEpochs&) = delete;
        GpuEpochs& operator=(const GpuEpochs&) = delete;
    };
    GpuEpochs gpuEpochs{options.pace};
    std::atomic<bool> stalled{false};
    std::atomic<std::uint64_t> stalledAt{0};
    std::atomic<std::uint64_t> earlyReleases{0};
    std::atomic<std::uint64_t> lateReleases{0};
    std::atomic<std::uint64_t> syntheticReleases{0};
    std::atomic<std::uint64_t> keptReservations{0};
    std::atomic<std::size_t> sequenced{0};
    std::uint64_t unfinished = 0;
    std::size_t threads = 0;
    std::uint64_t singleThreadOrdinal = 0;
    std::uint64_t mappedOrdinal = 0;
    std::uint64_t replayBase = 0;
    std::uint64_t replayFlips = 0;
    std::uint64_t captureFlips = 0;
    std::map<std::uint64_t, std::optional<std::uint64_t>> replaySerials;
    std::optional<std::uint64_t> captureBase = frameStartSerial(reader);
    std::size_t pass = 0;
    bool recording = false;
    Clock::time_point startedAt{};
    Clock::time_point endedAt{};
    std::optional<DepthPlan> plan;
    std::span<const FrameCall> window;
    std::size_t current = 0;
    std::vector<PositionCensus> census;
    std::uint64_t tailHeld = 0;
    std::atomic<std::uint64_t> fastDeltas{0};
    std::atomic<std::int64_t> gateWaitedNs{0};
    struct ReadHit {
        std::uint64_t call = 0;
        std::uint64_t address = 0;
        std::size_t bytes = 0;
        AgcDriver::PendingGpuRead read;
    };
    std::atomic<std::uint64_t> readWaits{0};
    std::atomic<std::int64_t> readCheckNs{0};
    std::optional<ReadHit> firstRead;
    std::vector<SplitTarget> inFlight;
    std::uint64_t opaqueThrough = 0;
    struct IssuePrep {
        SplitScan scan;
        std::vector<std::uint32_t> words;
        bool fromCopy = false;
        std::uint64_t need = 0;
    } prep;
    std::atomic<std::int64_t> issueWaitedNs{0};
    std::atomic<std::int64_t> scanNs{0};
    std::array<std::uint64_t, 4> issueWaits{};
    std::uint64_t unsettledLandings = 0;

    struct SettledHold {
        bool held;
        ~SettledHold() {
            if (held) AgcDriverReleaseSettled_nid_postfix();
        }
    };

    double paceWaitedSeconds() const {
        return static_cast<double>(paceWaitedNs.load()) / 1e9;
    }

    static double secondsOf(const std::atomic<std::int64_t>& totalNs) {
        return static_cast<double>(totalNs.load()) / 1e9;
    }

    double depthMean() const {
        const auto samples = depthSamples.load();
        return samples == 0 ? 0.0 : static_cast<double>(depthTotal.load()) / static_cast<double>(samples);
    }

    void memoryWaited(std::atomic<std::int64_t>& waited, Clock::time_point began) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - began).count();
        paceWaitedNs += elapsed;
        waited += elapsed;
    }

    void sampleDepth() {
        const auto [accepted, completed] = progress();
        const auto depth = accepted - completed;
        ++depthSamples;
        depthTotal += depth;
        auto deepest = depthMax.load();
        while (depth > deepest && !depthMax.compare_exchange_weak(deepest, depth)) {}
    }

    void report() const {
        const auto [accepted, completed] = progress();
        std::fprintf(stderr, "[frame-replay] %llu submits, %zu frames ready, %.1f s since the first frame, pacing waited %.1f s (%llu timeouts), serials %llu/%llu, blocked on%s\n",
                     static_cast<unsigned long long>(submits.load()), frames.Times().size(), std::chrono::duration<double>(Clock::now() - startedAt).count(), paceWaitedSeconds(),
                     static_cast<unsigned long long>(paceTimeouts.load()), static_cast<unsigned long long>(completed), static_cast<unsigned long long>(accepted), describeBlocked().c_str());
    }

    static std::string describeBlocked() {
        std::array<AgcDriver::AwaitedWait, 64> awaited{};
        const auto count = AgcDriverAwaitedWaits_nid_postfix(awaited.data(), awaited.size());
        std::string text;
        for (std::size_t slot = 0; slot < count; ++slot) {
            const auto& wait = awaited[slot];
            if (wait.address == 0) continue;
            char entry[128];
            std::snprintf(entry, sizeof(entry), " 0x%llx (function %u, reference 0x%llx, mask 0x%llx, now 0x%llx)", static_cast<unsigned long long>(wait.address), static_cast<unsigned>(wait.function),
                          static_cast<unsigned long long>(wait.reference), static_cast<unsigned long long>(wait.mask), static_cast<unsigned long long>(load(wait.address, wait.bytes == 8 ? 8 : 4)));
            text += entry;
        }
        return text.empty() ? " nothing" : text;
    }

    static bool paced(FrameCallKind kind) {
        return kind == FrameCallKind::Submit || kind == FrameCallKind::Flip || kind == FrameCallKind::ReserveEopFlip || kind == FrameCallKind::Suspend;
    }

    void pace(const FrameCall& call) {
        if (!options.pace || !recording || !paced(call.kind)) return;
        if (plan) return paceAtDepth(call);
        if (call.accepted < call.completed) return;
        if (stalled.load()) {
            if (progress().second == stalledAt.load()) return;
            stalled.store(false);
        }
        const auto inFlight = call.accepted - call.completed;
        const auto began = Clock::now();
        const auto deadline = began + options.paceTimeout;
        const auto limit = began + options.busyTimeout;
        const auto waited = [&] { paceWaitedNs += std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - began).count(); };
        while (true) {
            const auto [accepted, completed] = progress();
            if (accepted - completed <= inFlight) return waited();
            const auto now = Clock::now();
            if (now >= deadline && (now >= limit || (AgcDriverSettled_nid_postfix() && progress() == std::pair{accepted, completed}))) {
                waited();
                stalled.store(true);
                stalledAt.store(completed);
                std::fprintf(stderr, "[frame-replay] pacing gave up at call %llu (%s): serials %llu/%llu, the capture had %llu in flight, blocked on%s\n", static_cast<unsigned long long>(call.ordinal),
                             std::string(AgcDriver::FrameCallName(call.kind)).c_str(), static_cast<unsigned long long>(completed), static_cast<unsigned long long>(accepted), static_cast<unsigned long long>(inFlight), describeBlocked().c_str());
                ++paceTimeouts;
                if (options.strict) throw std::runtime_error("frame replay: call " + std::to_string(call.ordinal) + " waited longer than the pacing timeout");
                return;
            }
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
    }

    std::uint64_t boundOf(std::size_t index) const {
        const auto& call = window[index];
        const auto recorded = call.accepted >= call.completed ? call.accepted - call.completed : 0;
        const auto chosen = options.depthProbe ? *options.depthProbe : std::min(options.depth->bounds[plan->position[index]], options.depthCap.value_or(~std::uint64_t{0}));
        return std::max(recorded, chosen);
    }

    void paceAtDepth(const FrameCall& call) {
        const auto index = current;
        const auto position = plan->position[index];
        const auto bound = boundOf(index);
        const auto accepted = progress().first;
        const auto target = accepted > bound ? accepted - bound : 0;
        const auto began = Clock::now();
        awaitCompleted(target, began + options.busyTimeout, [&](const std::string& reason) {
            return std::runtime_error("frame replay: call " + std::to_string(call.ordinal) + " (" + std::string(AgcDriver::FrameCallName(call.kind)) + ", position " + options.depth->names[position] + ") may start with at most " + std::to_string(bound) + " submissions in flight, but " + reason);
        });
        paceWaitedNs += std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - began).count();
        if (call.kind == FrameCallKind::Submit) prepareIssue(call);
        if (plan->frame[index] >= 2) {
            census[position].issue.push_back(accepted - progress().second);
            const auto need = call.kind == FrameCallKind::Submit ? prep.need : 0;
            census[position].issueBound.push_back(need != 0 ? std::min(bound, accepted - need) : bound);
        }
    }

    std::runtime_error issueBlocked(const FrameCall& call, std::uint64_t address, std::uint64_t serial, const std::string& reason) const {
        const auto taken = std::find_if(replaySerials.begin(), replaySerials.end(), [serial](const auto& entry) { return entry.second == serial; });
        return std::runtime_error("frame replay: call " + std::to_string(call.ordinal) + " (submit, position " + options.depth->names[plan->position[current]] + ") reads, stores or waits on " + addressText(address) + ", which submission " + std::to_string(serial) + " (capture serial " +
                                  (taken != replaySerials.end() ? std::to_string(taken->first) : std::string("none")) + ") still stores into or a delta held back until it has run, but " + reason);
    }

    void prepareIssue(const FrameCall& call) {
        prep = {};
        const auto limit = call.completed <= *captureBase ? replayBase : mappedSerial(call.completed);
        const auto* const guest = reinterpret_cast<const std::uint32_t*>(static_cast<std::uintptr_t>(call.address));
        const auto words = static_cast<std::size_t>(call.size);
        for (std::size_t pass = 1;; ++pass) {
            const auto scanned = Clock::now();
            prep.scan = {};
            if (words != 0 && !AgcDriver::GuestMemory::Accessible(guest, words * sizeof(std::uint32_t))) prep.scan.opaque = true;
            else prep.scan = scanSubmission(std::span(guest, words), call.address);
            scanNs += std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - scanned).count();
            std::uint64_t need = 0;
            std::uint64_t at = 0;
            std::vector<std::pair<std::uint64_t, std::uint64_t>> landing;
            for (const auto& [begin, end] : prep.scan.acceptReads) {
                for (const auto& target : inFlight) {
                    if (target.wait || target.serial > limit) continue;
                    const auto low = std::max(begin, target.begin);
                    const auto high = std::min(end, target.end);
                    if (low >= high) continue;
                    if (target.serial <= gpuWaitedThrough && AgcDriverGuestStoreSettled_nid_postfix(low, static_cast<std::size_t>(high - low))) continue;
                    if (target.serial > need) {
                        need = target.serial;
                        at = low;
                    }
                    landing.emplace_back(low, high);
                }
            }
            if (need == 0) break;
            if (pass > 4) throw issueBlocked(call, at, need, "the bytes it reads at accept kept changing");
            ++issueWaits[0];
            prep.need = std::max(prep.need, need);
            issueWait(call, need, at, landing);
        }
        if (prep.scan.rewind || prep.scan.opaque) opaqueThrough = std::max(opaqueThrough, mappedSerial(call.values.back()));
    }

    void issueWait(const FrameCall& call, std::uint64_t serial, std::uint64_t address, std::span<const std::pair<std::uint64_t, std::uint64_t>> landing) {
        const auto began = Clock::now();
        awaitCompleted(serial, began + options.busyTimeout, [&](const std::string& reason) { return issueBlocked(call, address, serial, reason); });
        waitThroughOnGpu(call, serial);
        for (const auto& [begin, end] : landing) {
            if (!AgcDriverGuestStoreSettled_nid_postfix(begin, static_cast<std::size_t>(end - begin))) throw issueBlocked(call, begin, serial, "its stores there are not in guest memory yet");
        }
        pruneInFlight();
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - began).count();
        issueWaitedNs += elapsed;
        paceWaitedNs += elapsed;
    }

    void pruneInFlight() {
        std::erase_if(inFlight, [&](const SplitTarget& target) {
            if (target.serial > gpuWaitedThrough) return false;
            if (target.wait || AgcDriverGuestStoreSettled_nid_postfix(target.begin, static_cast<std::size_t>(target.end - target.begin))) return true;
            ++unsettledLandings;
            return false;
        });
    }

    void execute(const FrameCall& call) {
        const struct Issuing {
            Issuing() { issuing = true; }
            ~Issuing() { issuing = false; }
        } scope;
        pace(call);
        if (recording && call.kind == FrameCallKind::Submit) sampleDepth();
        switch (call.kind) {
        case FrameCallKind::RegisterShader:
            registration([&] { AgcDriverRegisterShader_nid_postfix(shader(call.address)); });
            return;
        case FrameCallKind::ResolveShaderAbi:
            registration([&] { AgcDriverResolveShaderAbi_nid_postfix(shader(call.address), call.context, call.primitive); });
            return;
        case FrameCallKind::ResolveGraphicsAbi:
            registration([&] { AgcDriverResolveGraphicsAbi_nid_postfix(shader(call.values.at(0)), shader(call.values.at(1)), static_cast<std::uint32_t>(call.values.at(2))); });
            return;
        case FrameCallKind::ResolveGraphicsStagesAbi: {
            std::vector<const Shader*> stages;
            for (const auto value : call.values) stages.push_back(shader(value));
            registration([&] { AgcDriverResolveGraphicsStagesAbi_nid_postfix(stages, call.context, call.primitive); });
            return;
        }
        case FrameCallKind::RegisterVideoOutput: {
            const auto handle = static_cast<std::uint32_t>(call.values.at(0));
            auto output = std::make_shared<ReplayOutput>(frames);
            AgcDriverRegisterVideoOutput_nid_postfix(handle, output);
            outputs.emplace(handle, std::move(output));
            return;
        }
        case FrameCallKind::UnregisterVideoOutput: {
            const auto handle = static_cast<std::uint32_t>(call.values.at(0));
            AgcDriverUnregisterVideoOutput_nid_postfix(handle, outputs.at(handle));
            outputs.erase(handle);
            return;
        }
        case FrameCallKind::SetTessellationFactorRing:
            sceAgcDriverSetTFRing(reinterpret_cast<const volatile void*>(static_cast<std::uintptr_t>(call.address)), static_cast<std::uint32_t>(call.size));
            return;
        case FrameCallKind::Submit: {
            const Packet packet{prep.fromCopy ? prep.words.data() : reinterpret_cast<std::uint32_t*>(static_cast<std::uintptr_t>(call.address)), static_cast<std::uint32_t>(call.size), static_cast<std::uint8_t>(call.flags), {}};
            issue(call, [&] {
                if (call.queue == 0) sceAgcDriverSubmitDcb(&packet);
                else sceAgcDriverSubmitAcb(call.queue, &packet);
            });
            if (plan) {
                const auto serial = *replaySerials.at(call.values.back());
                for (auto target : prep.scan.targets) {
                    target.serial = serial;
                    inFlight.push_back(target);
                }
                prep = {};
            }
            if (++submits % 25 == 0) report();
            return;
        }
        case FrameCallKind::Flip:
            issue(call, [&] { AgcDriverSubmitFlip_nid_postfix(flipInfo(call)); });
            return;
        case FrameCallKind::ReserveEopFlip: {
            const auto token = static_cast<std::uint32_t>(call.values.at(4));
            std::uint32_t reserved = 0;
            try {
                reserved = AgcDriverReserveEopFlipAt_nid_postfix(flipInfo(call), token);
            } catch (const AgcDriver::EopFlipAlreadyReserved&) {
                if (pass == 0) throw;
                ++keptReservations;
                return;
            }
            if (reserved != token) throw std::runtime_error("frame replay: EOP flip token " + std::to_string(token) + " was not reserved");
            return;
        }
        case FrameCallKind::Suspend:
            issue(call, [] { AgcDriverSuspendPoint_nid_postfix(); });
            return;
        case FrameCallKind::Memory: {
            const std::unique_lock lock(memoryMutex);
            const auto began = Clock::now();
            const bool pacing = options.pace && recording;
            if (pacing && reader.Version() >= AgcDriver::InFlightCallsVersion && (call.flags & AgcDriver::InFlightDeltaFlag) != 0) {
                if (call.ordinal > singleThreadOrdinal) throw std::runtime_error("frame replay: memory delta at call " + std::to_string(call.ordinal) + " was read with submissions in flight after a second thread made calls, which this replay cannot order");
                if (const auto accepted = progress().first; captureBase && accepted != mappedSerial(call.accepted)) throw std::runtime_error("frame replay: memory delta at call " + std::to_string(call.ordinal) + " was read in flight at capture serial " + std::to_string(call.accepted) + ", which maps to replay serial " + std::to_string(mappedSerial(call.accepted)) + ", but this replay had accepted " + std::to_string(accepted));
                waitStarted(call);
                memoryWaited(inFlightWaitedNs, began);
                waitSerialsOnGpu(call);
                restoreGroup(call, Restore::Held);
                return;
            }
            if (pacing && plan) return restoreAtDepth(call, began);
            const bool settled = !pacing || AgcDriverHoldSettled_nid_postfix(stalled.load() && progress().second == stalledAt.load() ? 0 : options.busyTimeout.count());
            const SettledHold hold{pacing};
            memoryWaited(heldWaitedNs, began);
            if (!settled && options.strict) throw std::runtime_error("frame replay: the replay did not settle before memory delta " + std::to_string(call.ordinal));
            if (pacing && settled) waitSettledOnGpu(call);
            restoreGroup(call, Restore::Held);
            return;
        }
        case FrameCallKind::FrameStart:
        case FrameCallKind::End:
        case FrameCallKind::DisplayBuffer:
            return;
        }
        throw std::runtime_error("frame replay: unknown call kind");
    }

    void waitStarted(const FrameCall& call) {
        const auto accepted = progress().first;
        const auto limit = Clock::now() + options.busyTimeout;
        auto lastSeen = progress();
        auto quietSince = Clock::now();
        if (const auto through = completedSerial(call, accepted); through != 0) awaitSerial(through, limit, lastSeen, quietSince, [&](const std::string& reason) { return std::runtime_error("frame replay: memory delta at call " + std::to_string(call.ordinal) + " waits for submission " + std::to_string(call.completed) + " (replay serial " + std::to_string(through) + "), which had completed when the capture read the delta, but " + reason); }, [](std::uint64_t serial) { return AgcDriverSerialDone_nid_postfix(serial) && progress().second >= serial; });
        for (std::size_t index = 1; index < call.values.size(); ++index) {
            const auto serial = startedSerial(call, accepted, call.values[index]);
            if (!serial) continue;
            awaitSerial(*serial, limit, lastSeen, quietSince, [&](const std::string& reason) { return std::runtime_error("frame replay: memory delta at call " + std::to_string(call.ordinal) + " waits for submission " + std::to_string(call.values[index]) + " (replay serial " + std::to_string(*serial) + "), which had started when the capture read the delta, but " + reason); }, AgcDriverSerialDone_nid_postfix);
        }
    }

    std::uint64_t completedSerial(const FrameCall& call, std::uint64_t accepted) const {
        if (!captureBase) return call.accepted - call.completed < accepted ? accepted - (call.accepted - call.completed) : 0;
        if (call.completed <= *captureBase) return replayBase;
        auto found = replaySerials.upper_bound(call.completed);
        if (found == replaySerials.begin() || std::prev(found)->first != call.completed) throw std::runtime_error("frame replay: memory delta at call " + std::to_string(call.ordinal) + " waits for submission " + std::to_string(call.completed) + ", which had completed when the capture read the delta, but no call since the frame start recorded that serial");
        while (found != replaySerials.begin()) {
            --found;
            if (found->second) return *found->second;
        }
        return replayBase;
    }

    std::optional<std::uint64_t> startedSerial(const FrameCall& call, std::uint64_t accepted, std::uint64_t serial) const {
        if (!captureBase) return accepted - (call.accepted - serial);
        const auto found = replaySerials.find(serial);
        if (found == replaySerials.end()) throw std::runtime_error("frame replay: memory delta at call " + std::to_string(call.ordinal) + " lists submission " + std::to_string(serial) + ", which had started when the capture read the delta, but no call since the frame start recorded that serial");
        return found->second;
    }

    template <typename TBlocked, typename TDone>
    void awaitSerial(std::uint64_t serial, Clock::time_point limit, std::pair<std::uint64_t, std::uint64_t>& lastSeen, Clock::time_point& quietSince, TBlocked&& blocked, TDone&& done) {
        while (!done(serial)) {
            const auto now = Clock::now();
            if (const auto seen = progress(); seen != lastSeen) {
                lastSeen = seen;
                quietSince = now;
            } else if (now - quietSince >= options.paceTimeout && AgcDriverSettled_nid_postfix() && !done(serial) && progress() == seen) {
                throw blocked("no queue runs and the serials stopped at " + std::to_string(seen.second) + "/" + std::to_string(seen.first) + "; blocked on" + describeBlocked());
            }
            if (now >= limit) throw blocked("it did not complete within " + std::to_string(options.busyTimeout.count()) + " ms");
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
    }

    template <typename TBlocked>
    void awaitCompleted(std::uint64_t serial, Clock::time_point limit, TBlocked&& blocked) {
        auto lastSeen = progress();
        auto quietSince = Clock::now();
        while (!AgcDriverSerialDone_nid_postfix(serial) || progress().second < serial) {
            const auto now = Clock::now();
            if (const auto seen = progress(); seen != lastSeen) {
                lastSeen = seen;
                quietSince = now;
            } else if (now - quietSince >= options.paceTimeout && AgcDriverSettled_nid_postfix() && progress() == seen) {
                throw blocked("no queue runs and the serials stopped at " + std::to_string(seen.second) + "/" + std::to_string(seen.first) + "; blocked on" + describeBlocked());
            }
            if (now >= limit) throw blocked("they did not complete within " + std::to_string(options.busyTimeout.count()) + " ms");
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
    }

    static bool rewindWait(const AgcDriver::AwaitedWait& wait) {
        return wait.function == 0 && wait.bytes == 0;
    }

    void waitSettledOnGpu(const FrameCall& call) {
        ++memoryGpuDrains;
        const auto began = Clock::now();
        const bool ran = AgcDriverWaitRecordedOnGpuFor_nid_postfix(options.busyTimeout.count());
        memoryGpuWaitedNs += std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - began).count();
        if (!ran) throw std::runtime_error("frame replay: the GPU work recorded so far did not finish before memory delta " + std::to_string(call.ordinal));
    }

    void waitSerialsOnGpu(const FrameCall& call) {
        const auto accepted = progress().first;
        const auto through = completedSerial(call, accepted);
        std::vector<std::uint64_t> started;
        for (std::size_t index = 1; index < call.values.size(); ++index) {
            if (const auto serial = startedSerial(call, accepted, call.values[index])) started.push_back(*serial);
        }
        if (started.empty() && through <= gpuWaitedThrough) return;
        const auto began = Clock::now();
        AgcDriver::GpuSerialsWait outcome;
        const bool ran = AgcDriverWaitSerialsOnGpuFor_nid_postfix(through, started.data(), started.size(), options.busyTimeout.count(), &outcome);
        memoryGpuWaitedNs += std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - began).count();
        if (!ran) throw std::runtime_error("frame replay: the GPU work of the submissions memory delta " + std::to_string(call.ordinal) + " waits for did not finish within " + std::to_string(options.busyTimeout.count()) + " ms");
        memoryGpuWaits += outcome.waited ? 1 : 0;
        memoryGpuSubmits += outcome.submitted ? 1 : 0;
        gpuWaitedThrough = std::max(gpuWaitedThrough, through);
    }

    bool heldPosition(std::uint32_t position) const {
        const auto& name = options.depth->names[position];
        return std::find(options.depthHold.begin(), options.depthHold.end(), name) != options.depthHold.end();
    }

    DepthCause depthCause(const FrameCall& call, std::size_t index) const {
        const auto next = plan->next[index];
        if (next != DepthPlan::Tail && heldPosition(plan->position[next])) return DepthCause::Forced;
        if (next == DepthPlan::Tail) return DepthCause::Tail;
        if (call.completed != call.accepted) return DepthCause::Parked;
        const auto floors = memoryFloors(call);
        if (!floors.store) return DepthCause::StoreUnknown;
        if ((floors.flags & FrameFloorFlag::IncompleteSerialWait) != 0) return DepthCause::WaitIncomplete;
        if ((floors.flags & FrameFloorFlag::WaitValueLost) != 0) return DepthCause::WaitLost;
        if ((floors.flags & FrameFloorFlag::WaitOrderLost) != 0) return DepthCause::WaitOrder;
        if ((floors.flags & FrameFloorFlag::ReleasesWait) != 0) return DepthCause::Releases;
        return DepthCause::Ahead;
    }

    std::uint64_t floorDistance(const FrameCall& call, std::size_t index) const {
        if (reader.Version() >= AgcDriver::InFlightCallsVersion && (call.flags & AgcDriver::InFlightDeltaFlag) != 0) {
            auto newest = call.completed;
            for (std::size_t value = 1; value < call.values.size(); ++value) newest = std::max(newest, call.values[value]);
            return call.accepted > newest ? call.accepted - newest : 0;
        }
        if (depthCause(call, index) != DepthCause::Ahead) return 0;
        const auto floors = memoryFloors(call);
        const auto floor = std::max(floors.store.value_or(0), floors.wait.value_or(0));
        return call.accepted > floor ? call.accepted - floor : 0;
    }

    std::uint64_t replayTarget(const FrameCall& call, std::uint64_t serial) const {
        if (serial <= *captureBase) return replayBase;
        const auto found = replaySerials.find(serial);
        if (found == replaySerials.end() || !found->second) throw std::runtime_error("frame replay: memory delta at call " + std::to_string(call.ordinal) + " waits for capture serial " + std::to_string(serial) + ", which no call since the frame start took");
        return *found->second;
    }

    bool waitThroughOnGpu(const FrameCall& call, std::uint64_t through) {
        if (through <= gpuWaitedThrough) return false;
        const auto began = Clock::now();
        AgcDriver::GpuSerialsWait outcome;
        const bool ran = AgcDriverWaitSerialsOnGpuFor_nid_postfix(through, nullptr, 0, options.busyTimeout.count(), &outcome);
        memoryGpuWaitedNs += std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - began).count();
        if (!ran) throw std::runtime_error("frame replay: the GPU work of the submissions memory delta " + std::to_string(call.ordinal) + " waits for did not finish within " + std::to_string(options.busyTimeout.count()) + " ms");
        memoryGpuWaits += outcome.waited ? 1 : 0;
        memoryGpuSubmits += outcome.submitted ? 1 : 0;
        gpuWaitedThrough = through;
        return outcome.waited;
    }

    void restoreHeld(const FrameCall& call, DepthCause cause) {
        const auto began = Clock::now();
        const bool settled = AgcDriverHoldSettled_nid_postfix(options.busyTimeout.count());
        const SettledHold hold{true};
        memoryWaited(heldWaitedNs, began);
        if (!settled) throw std::runtime_error("frame replay: memory delta at call " + std::to_string(call.ordinal) + " (" + std::string(HeldReasons[static_cast<std::size_t>(cause)]) + ") is restored with nothing running or every busy queue parked on title memory, but the replay did not settle within " + std::to_string(options.busyTimeout.count()) + " ms");
        waitSettledOnGpu(call);
        restoreGroup(call, Restore::Held);
        const auto next = plan->next[current];
        if (next == DepthPlan::Tail) ++tailHeld;
        else ++census[plan->position[next]].held[static_cast<std::size_t>(cause)];
    }

    void restoreAtDepth(const FrameCall& call, Clock::time_point began) {
        const auto index = current;
        if (const auto cause = depthCause(call, index); cause != DepthCause::Ahead) return restoreHeld(call, cause);
        const auto next = plan->next[index];
        const auto position = plan->position[next];
        const auto bound = boundOf(next);
        const auto floors = memoryFloors(call);
        const auto store = floors.store.value_or(0);
        const auto wait = floors.wait.value_or(0);
        const auto& paced = window[next];
        enum class Term { Depth, Store, Wait } by = Term::Depth;
        auto serial = paced.accepted > bound ? paced.accepted - bound : 0;
        if (store > serial) {
            serial = store;
            by = Term::Store;
        }
        if (wait > serial) {
            serial = wait;
            by = Term::Wait;
        }
        const auto target = replayTarget(call, serial);
        auto& entry = census[position];
        if (!AgcDriverSerialDone_nid_postfix(target) || progress().second < target) ++entry.cpuWaits;
        awaitCompleted(target, began + options.busyTimeout, [&](const std::string& reason) {
            return std::runtime_error("frame replay: memory delta at call " + std::to_string(call.ordinal) + " waits for capture serial " + std::to_string(serial) + " (replay serial " + std::to_string(target) + "; store floor " + std::to_string(store) + ", wait floor " + std::to_string(wait) + ", bound " + std::to_string(bound) + " at position " + options.depth->names[position] + "), but " + reason);
        });
        const auto waitedThrough = gpuWaitedThrough;
        if (waitThroughOnGpu(call, target)) ++entry.gpuWaits;
        if (gpuWaitedThrough != waitedThrough) pruneInFlight();
        memoryWaited(gateWaitedNs, began);
        const auto [accepted, completed] = progress();
        if (!restoreGroup(call, Restore::AtDepth)) return restoreHeld(call, DepthCause::Pending);
        ++fastDeltas;
        ++(by == Term::Depth ? entry.byDepth : by == Term::Store ? entry.byStore : entry.byWait);
        if (plan->frame[next] >= 2) entry.restore.push_back(accepted - completed);
    }

    static bool rewindHeader(std::uint32_t header) {
        return (header >> 30u) == 3u && ((header >> 8u) & 0xffu) == 0x59u;
    }

    static void findRewinds(std::uint64_t address, std::span<const std::byte> bytes, std::vector<std::size_t>& found) {
        found.clear();
        for (auto at = static_cast<std::size_t>((4 - address % 4) % 4); at + 8 <= bytes.size(); at += 4) {
            std::uint32_t header = 0;
            std::memcpy(&header, bytes.data() + at, sizeof(header));
            if (!rewindHeader(header)) continue;
            std::uint32_t value = 0;
            std::memcpy(&value, bytes.data() + at + 4, sizeof(value));
            if ((value & AgcDriver::Pm4::RewindValid) != 0) found.push_back(at + 4);
        }
    }

    template <typename TApply>
    void decodeWindows(const std::vector<const FrameMemoryChunk*>& chunks, TApply&& apply) {
        if (!decoders) decoders = std::make_unique<DecodePool>();
        std::vector<std::size_t> starts;
        for (std::size_t first = 0; first < chunks.size();) {
            starts.clear();
            std::size_t used = 0;
            auto last = first;
            do {
                starts.push_back(used);
                used += static_cast<std::size_t>(chunks[last++]->bytes);
            } while (last < chunks.size() && used <= DecodeWindowBytes && chunks[last]->bytes <= DecodeWindowBytes - used);
            if (decoded.size() < used) decoded.resize(used);
            if (rewindCandidates.size() < last - first) rewindCandidates.resize(last - first);
            const auto began = Clock::now();
            decodeWorkNs += decoders->Run(last - first, [&](std::size_t index) {
                const auto& chunk = *chunks[first + index];
                const auto bytes = std::span(decoded).subspan(starts[index], static_cast<std::size_t>(chunk.bytes));
                reader.Read(chunk, bytes);
                findRewinds(chunk.address, bytes, rewindCandidates[index]);
            }, decodeFailures);
            decodeWallNs += std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - began).count();
            for (auto index = first; index < last; ++index) {
                if (decodeFailures[index - first]) std::rethrow_exception(decodeFailures[index - first]);
                apply(*chunks[index], std::span(decoded).subspan(starts[index - first], static_cast<std::size_t>(chunks[index]->bytes)), rewindCandidates[index - first]);
            }
            first = last;
        }
    }

    bool restoreGroup(const FrameCall& call, Restore mode) {
        const auto found = groups.find(call.values.at(0));
        if (found == groups.end()) return true;
        const auto began = Clock::now();
        std::array<AgcDriver::AwaitedWait, 64> awaited{};
        const auto count = AgcDriverAwaitedWaits_nid_postfix(awaited.data(), awaited.size());
        std::vector<std::uint64_t> parked;
        for (std::size_t slot = 0; slot < count; ++slot) {
            if (rewindWait(awaited[slot])) parked.push_back(awaited[slot].address);
        }
        std::vector<std::pair<std::uint32_t*, std::uint32_t>> controls;
        const auto& chunks = found->second;
        std::vector<std::pair<std::size_t, std::size_t>> runs;
        for (std::size_t first = 0; first < chunks.size();) {
            if (skipped.Overlaps(*chunks[first])) {
                ++first;
                continue;
            }
            auto last = first + 1;
            auto end = chunks[first]->address + chunks[first]->bytes;
            while (last < chunks.size() && chunks[last]->address == end && !skipped.Overlaps(*chunks[last])) {
                end += chunks[last]->bytes;
                ++last;
            }
            if (mode == Restore::Held) AgcDriverPrepareGuestStore_nid_postfix(chunks[first]->address, static_cast<std::size_t>(end - chunks[first]->address));
            runs.emplace_back(first, last);
            first = last;
        }
        if (mode == Restore::AtDepth) {
            for (const auto& [first, last] : runs) {
                const auto begin = chunks[first]->address;
                const auto end = chunks[last - 1]->address + chunks[last - 1]->bytes;
                if (!AgcDriverGuestStoreSettled_nid_postfix(begin, static_cast<std::size_t>(end - begin))) return false;
            }
        }
        std::vector<const FrameMemoryChunk*> ordered;
        for (const auto& [first, last] : runs) ordered.insert(ordered.end(), chunks.begin() + static_cast<std::ptrdiff_t>(first), chunks.begin() + static_cast<std::ptrdiff_t>(last));
        std::size_t run = 0;
        std::size_t applied = 0;
        bool reading = false;
        std::size_t firstControl = 0;
        std::vector<std::vector<std::byte>> contents;
        decodeWindows(ordered, [&](const FrameMemoryChunk& chunk, std::span<std::byte> bytes, std::vector<std::size_t>& offsets) {
            const auto [first, last] = runs[run];
            if (applied == 0) {
                const auto checked = Clock::now();
                const auto begin = chunks[first]->address;
                const auto end = chunks[last - 1]->address + chunks[last - 1]->bytes;
                reading = mode == Restore::AtDepth && AgcDriverPendingGpuRead_nid_postfix(begin, static_cast<std::size_t>(end - begin), nullptr);
                if (mode == Restore::AtDepth) readCheckNs += std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - checked).count();
                contents.clear();
                firstControl = controls.size();
            }
            auto* const target = reinterpret_cast<std::byte*>(static_cast<std::uintptr_t>(chunk.address));
            for (const auto address : parked) {
                if (address % 4 == 0 && address >= chunk.address && bytes.size() >= 4 && address - chunk.address <= bytes.size() - 4) offsets.push_back(static_cast<std::size_t>(address - chunk.address));
            }
            std::sort(offsets.begin(), offsets.end());
            const auto control = [&](std::size_t at) {
                if (at + 4 > bytes.size()) return false;
                std::uint32_t value = 0;
                std::memcpy(&value, bytes.data() + at, sizeof(value));
                if ((value & AgcDriver::Pm4::RewindValid) == 0) return false;
                std::uint32_t header = 0;
                if (at >= 4) std::memcpy(&header, bytes.data() + at - 4, sizeof(header));
                if (!rewindHeader(header) && std::find(parked.begin(), parked.end(), chunk.address + at) == parked.end()) return false;
                controls.emplace_back(reinterpret_cast<std::uint32_t*>(target + at), value);
                std::memcpy(bytes.data() + at, target + at, sizeof(value));
                return true;
            };
            std::size_t next = 0;
            for (auto at : offsets) {
                if (at < next) continue;
                while (control(at)) at += 4;
                next = at + 4;
            }
            if (reading) {
                contents.emplace_back(bytes.begin(), bytes.end());
            } else {
                std::memcpy(target, bytes.data(), bytes.size());
                deltaBytes += chunk.bytes;
            }
            if (++applied < last - first) return;
            applied = 0;
            ++run;
            if (!reading) return;
            const auto compared = Clock::now();
            waitForReaders(call, std::span(chunks).subspan(first, last - first), contents, std::span(controls).subspan(firstControl));
            readCheckNs += std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - compared).count();
            for (auto index = first; index < last; ++index) {
                const auto& kept = contents[index - first];
                std::memcpy(reinterpret_cast<std::byte*>(static_cast<std::uintptr_t>(chunks[index]->address)), kept.data(), kept.size());
                deltaBytes += chunks[index]->bytes;
            }
        });
        for (const auto& [control, value] : controls) std::atomic_ref<std::uint32_t>(*control).store(value, std::memory_order_release);
        restoredNs += std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - began).count();
        return true;
    }

    void waitForReaders(const FrameCall& call, std::span<const FrameMemoryChunk* const> run, const std::vector<std::vector<std::byte>>& contents, std::span<const std::pair<std::uint32_t*, std::uint32_t>> deferred) {
        std::vector<std::pair<std::uint64_t, std::uint64_t>> spans;
        for (std::size_t index = 0; index < run.size(); ++index) {
            const auto address = run[index]->address;
            const auto& bytes = contents[index];
            const auto* const current = reinterpret_cast<const std::byte*>(static_cast<std::uintptr_t>(address));
            for (std::size_t at = 0; at < bytes.size();) {
                const auto differs = std::mismatch(bytes.begin() + static_cast<std::ptrdiff_t>(at), bytes.end(), current + at).first;
                if (differs == bytes.end()) break;
                const auto dword = (address + static_cast<std::uint64_t>(differs - bytes.begin())) & ~std::uint64_t{3};
                if (!spans.empty() && spans.back().second >= dword) spans.back().second = std::max(spans.back().second, dword + 4);
                else spans.emplace_back(dword, dword + 4);
                at = static_cast<std::size_t>(dword + 4 - address);
            }
        }
        for (const auto& [control, value] : deferred) {
            if (std::atomic_ref<std::uint32_t>(*control).load(std::memory_order_acquire) != value) spans.emplace_back(reinterpret_cast<std::uintptr_t>(control), reinterpret_cast<std::uintptr_t>(control) + 4);
        }
        for (const auto& [begin, end] : spans) {
            const auto bytes = static_cast<std::size_t>(end - begin);
            AgcDriver::PendingGpuRead read;
            while (AgcDriverPendingGpuRead_nid_postfix(begin, bytes, &read)) {
                if (!firstRead) firstRead = ReadHit{call.ordinal, begin, bytes, read};
                if (!AgcDriverWaitGpuBatchFor_nid_postfix(read.batch, options.busyTimeout.count())) throw std::runtime_error("frame replay: GPU batch " + std::to_string(read.batch) + ", which reads " + addressText(begin) + " (" + std::to_string(bytes) + " bytes) of the memory delta at call " + std::to_string(call.ordinal) + " in place, did not finish within " + std::to_string(options.busyTimeout.count()) + " ms");
                ++readWaits;
            }
        }
    }

    bool releaseRewind(const FrameCall& call, const AgcDriver::AwaitedWait& wait) {
        if (call.kind != FrameCallKind::Memory) return false;
        const auto found = groups.find(call.values.at(0));
        if (found == groups.end()) return false;
        for (const auto* chunk : found->second) {
            if (chunk->address > wait.address || wait.address + 4 > chunk->address + chunk->bytes || skipped.Overlaps(*chunk)) continue;
            const auto bytes = reader.Read(*chunk);
            std::uint32_t value = 0;
            std::memcpy(&value, bytes.data() + (wait.address - chunk->address), sizeof(value));
            if ((value & AgcDriver::Pm4::RewindValid) == 0 || !AgcDriverHoldSettled_nid_postfix(0)) return false;
            const SettledHold hold{true};
            waitSettledOnGpu(call);
            restoreGroup(call, Restore::Held);
            return true;
        }
        return false;
    }

    static std::uint64_t load(std::uint64_t address, std::size_t bytes) {
        std::uint64_t value = 0;
        std::memcpy(&value, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(address)), bytes);
        return value;
    }

    static bool satisfies(const AgcDriver::AwaitedWait& wait, std::uint64_t value) {
        const auto masked = value & wait.mask;
        const auto reference = wait.reference & wait.mask;
        switch (wait.function) {
        case 1: return masked < reference;
        case 2: return masked <= reference;
        case 3: return masked == reference;
        case 4: return masked != reference;
        case 5: return masked >= reference;
        case 6: return masked > reference;
        default: return false;
        }
    }

    static std::optional<std::uint64_t> satisfying(const AgcDriver::AwaitedWait& wait, std::uint64_t current) {
        const auto keep = current & ~wait.mask;
        const auto reference = wait.reference & wait.mask;
        switch (wait.function) {
        case 2:
        case 3:
        case 5: return keep | reference;
        case 1: return reference == 0 ? std::nullopt : std::optional<std::uint64_t>(keep);
        case 4: return keep | (~reference & wait.mask);
        case 6: return reference == wait.mask ? std::nullopt : std::optional<std::uint64_t>(keep | ((reference + 1) & wait.mask));
        default: return std::nullopt;
        }
    }

    bool writeRecordedValue(const FrameCall& call, const AgcDriver::AwaitedWait& wait) {
        if (call.kind != FrameCallKind::Memory) return false;
        const auto found = groups.find(call.values.at(0));
        if (found == groups.end()) return false;
        const auto address = wait.address;
        for (const auto* chunk : found->second) {
            if (chunk->address > address || address + 4 > chunk->address + chunk->bytes || skipped.Overlaps(*chunk)) continue;
            const auto bytes = reader.Read(*chunk);
            const auto length = static_cast<std::size_t>(std::min<std::uint64_t>(wait.bytes == 8 ? 8 : 4, chunk->address + chunk->bytes - address));
            const auto* const recorded = bytes.data() + (address - chunk->address);
            auto* const target = reinterpret_cast<std::byte*>(static_cast<std::uintptr_t>(address));
            if (std::memcmp(target, recorded, length) == 0) return false;
            std::uint64_t value = 0;
            std::memcpy(&value, recorded, length);
            if (wait.function != 0 && !satisfies(wait, value)) return false;
            std::memcpy(target, recorded, length);
            return true;
        }
        return false;
    }

    void releaseAwaited(std::span<const FrameCall> calls, std::size_t from) {
        std::array<AgcDriver::AwaitedWait, 64> awaited{};
        const auto count = AgcDriverAwaitedWaits_nid_postfix(awaited.data(), awaited.size());
        bool released = false;
        for (std::size_t slot = 0; slot < count && !released; ++slot) {
            const auto& wait = awaited[slot];
            if (wait.address == 0) continue;
            const std::size_t bytes = wait.bytes == 8 ? 8 : 4;
            if (wait.function != 0 && satisfies(wait, load(wait.address, bytes))) continue;
            if (rewindWait(wait)) {
                for (auto index = from; reader.Version() < AgcDriver::InFlightCallsVersion && index < calls.size() && !released; ++index) released = releaseRewind(calls[index], wait);
                if (released) ++earlyReleases;
                continue;
            }
            for (auto index = std::min(from, calls.size()); index > 0 && !released; --index) {
                released = writeRecordedValue(calls[index - 1], wait);
                if (released) ++lateReleases;
            }
            if (!released && options.synthesize) {
                if (const auto value = satisfying(wait, load(wait.address, bytes))) {
                    std::memcpy(reinterpret_cast<void*>(static_cast<std::uintptr_t>(wait.address)), &*value, bytes);
                    ++syntheticReleases;
                    released = true;
                }
            }
            if (released) ++earlyReleases;
        }
    }

    void runWindow(std::span<const FrameCall> calls) {
        window = calls;
        plan.reset();
        if (options.depth && recording) {
            if (!captureBase) throw std::runtime_error(std::string(DepthWithoutFrameStart));
            plan = planDepth(calls, *options.depth);
            inFlight.clear();
            opaqueThrough = 0;
            prep = {};
            plan->ceiling.assign(calls.size(), 0);
            for (std::size_t index = 0; index < calls.size(); ++index) {
                if (plan->position[index] != DepthPlan::NoPosition) plan->ceiling[index] = boundOf(index);
            }
            for (std::size_t index = 0; index < calls.size(); ++index) {
                const auto next = plan->next[index];
                if (calls[index].kind == FrameCallKind::Memory && next != DepthPlan::Tail) plan->ceiling[next] = std::min(plan->ceiling[next], floorDistance(calls[index], index));
            }
        }
        std::map<std::uint64_t, std::vector<std::size_t>> byThread;
        for (std::size_t index = 0; index < calls.size(); ++index) byThread[calls[index].thread].push_back(index);
        threads = byThread.size();
        singleThreadOrdinal = calls.empty() ? 0 : calls.back().ordinal;
        for (const auto& call : calls) {
            if (call.thread == calls.front().thread) continue;
            singleThreadOrdinal = call.ordinal - 1;
            break;
        }
        mappedOrdinal = 0;
        for (const auto& call : calls) {
            if (options.pace && recording && reader.Version() >= AgcDriver::InFlightCallsVersion && captureBase && call.kind == FrameCallKind::Memory && (call.flags & AgcDriver::InFlightDeltaFlag) != 0 && call.ordinal <= singleThreadOrdinal) mappedOrdinal = std::max(mappedOrdinal, call.ordinal);
        }
        if (plan) mappedOrdinal = calls.back().ordinal + 1;
        if (captureBase && *captureBase != 0 && progress().first == 0) primeAcceptedSerial();
        replayBase = progress().first;
        replayFlips = 0;
        captureFlips = 0;
        replaySerials.clear();
        std::mutex mutex;
        std::condition_variable turn;
        std::size_t started = 0;
        std::exception_ptr failure;
        std::atomic<bool> finished{false};
        sequenced.store(0);
        stalled.store(false);
        if (pass == 1) {
            depthSamples.store(0);
            depthTotal.store(0);
            depthMax.store(0);
            census.clear();
            tailHeld = 0;
        }
        if (plan) {
            census.resize(options.depth->names.size());
            for (std::size_t index = 0; index < calls.size(); ++index) {
                if (plan->position[index] != DepthPlan::NoPosition && plan->frame[index] >= 2) census[plan->position[index]].ceiling.push_back(plan->ceiling[index]);
            }
        }
        std::thread breaker([&] {
            if (!options.release) return;
            try {
                auto lastSeen = progress();
                auto lastProgress = Clock::now();
                while (!finished.load()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    const auto seen = progress();
                    const std::unique_lock lock(memoryMutex, std::try_to_lock);
                    if (seen != lastSeen || !lock.owns_lock()) {
                        lastSeen = seen;
                        lastProgress = Clock::now();
                        continue;
                    }
                    if (Clock::now() - lastProgress < options.releaseAfter || !AgcDriverSettled_nid_postfix() || progress() != lastSeen) continue;
                    releaseAwaited(calls, sequenced.load());
                    lastProgress = Clock::now();
                }
            } catch (...) {
                const std::lock_guard guard(mutex);
                if (failure == nullptr) failure = std::current_exception();
                turn.notify_all();
            }
        });
        std::vector<std::thread> workers;
        for (const auto& entry : byThread) {
            workers.emplace_back([&, &indices = entry.second] {
                for (const auto index : indices) {
                    std::unique_lock lock(mutex);
                    turn.wait(lock, [&] { return started == index || failure != nullptr; });
                    if (failure != nullptr) return;
                    current = index;
                    const auto advance = [&] {
                        ++started;
                        sequenced.store(started);
                        lock.unlock();
                        turn.notify_all();
                    };
                    const auto kind = calls[index].kind;
                    const bool ordered = kind == FrameCallKind::Memory || kind == FrameCallKind::RegisterVideoOutput || kind == FrameCallKind::UnregisterVideoOutput;
                    if (!ordered) advance();
                    try {
                        execute(calls[index]);
                        if (ordered) advance();
                    } catch (...) {
                        if (!lock.owns_lock()) lock.lock();
                        if (failure == nullptr) failure = std::current_exception();
                        turn.notify_all();
                        return;
                    }
                }
            });
        }
        for (auto& worker : workers) worker.join();
        const bool failed = [&] { const std::lock_guard guard(mutex); return failure != nullptr; }();
        if (!failed) drain();
        finished.store(true);
        breaker.join();
        if (failure != nullptr) std::rethrow_exception(failure);
    }

    void drain() {
        auto lastCompleted = progress().second;
        auto lastProgress = Clock::now();
        auto lastReport = lastProgress;
        while (true) {
            const auto [accepted, completed] = progress();
            unfinished = accepted - completed;
            if (unfinished == 0) return;
            const auto now = Clock::now();
            if (completed != lastCompleted) {
                lastCompleted = completed;
                lastProgress = now;
            } else if (now - lastReport >= options.drainTimeout) {
                lastReport = now;
                std::fprintf(stderr, "[frame-replay] draining: %llu submissions pending for %.0f s (serials %llu/%llu), blocked on%s\n", static_cast<unsigned long long>(unfinished),
                             std::chrono::duration<double>(now - lastProgress).count(), static_cast<unsigned long long>(completed), static_cast<unsigned long long>(accepted), describeBlocked().c_str());
            }
            if (const auto quiet = now - lastProgress; quiet >= options.busyTimeout || (quiet >= options.drainTimeout && AgcDriverSettled_nid_postfix())) {
                std::fprintf(stderr, "[frame-replay] %llu submissions did not complete after the last captured call (serials %llu/%llu), blocked on%s\n", static_cast<unsigned long long>(unfinished),
                             static_cast<unsigned long long>(completed), static_cast<unsigned long long>(accepted), describeBlocked().c_str());
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    static std::optional<std::uint64_t> takenSerial(const FrameCall& call) {
        if (call.values.size() != (call.kind == FrameCallKind::Flip ? 5u : 1u)) return std::nullopt;
        return call.values.back();
    }

    std::uint64_t mappedSerial(std::uint64_t serial) const {
        return replayBase + (serial - *captureBase) + replayFlips - captureFlips;
    }

    static void primeAcceptedSerial() {
        std::async(std::launch::async, [] { AgcDriverSubmitGraphics_nid_postfix(nullptr, 0); }).get();
        AgcDriverWaitIdle_nid_postfix();
    }

    template <typename TFunction>
    void issue(const FrameCall& call, TFunction&& function) {
        if (!recording || call.ordinal >= mappedOrdinal) {
            traced(call, std::forward<TFunction>(function));
            return;
        }
        const std::string name(AgcDriver::FrameCallName(call.kind));
        const auto taken = takenSerial(call);
        if (!taken) throw std::runtime_error("frame replay: call " + std::to_string(call.ordinal) + " (" + name + ") has no recorded serial" + (plan ? std::string(DepthUnmapped) : ", so the submissions in flight at the memory delta of call " + std::to_string(mappedOrdinal) + " cannot be mapped to this replay's"));
        const auto before = progress().first;
        traced(call, std::forward<TFunction>(function));
        const auto after = progress().first;
        const auto mapped = *taken == 0 ? before : mappedSerial(*taken);
        if (after == mapped) {
            if (*taken != 0) replaySerials.emplace(*taken, after);
            return;
        }
        if (call.kind == FrameCallKind::Flip && *taken != 0 && after == before && mapped == before + 1) {
            ++captureFlips;
            replaySerials.emplace(*taken, std::nullopt);
            return;
        }
        if (call.kind == FrameCallKind::Flip && *taken == 0 && after == before + 1) {
            ++replayFlips;
            const auto limit = Clock::now() + options.busyTimeout;
            auto lastSeen = progress();
            auto quietSince = Clock::now();
            awaitSerial(after, limit, lastSeen, quietSince, [&](const std::string& reason) { return std::runtime_error("frame replay: call " + std::to_string(call.ordinal) + " (" + name + ") took replay serial " + std::to_string(after) + " where the capture's call took none and presented at once, but " + reason); }, AgcDriverSerialDone_nid_postfix);
            return;
        }
        throw std::runtime_error("frame replay: call " + std::to_string(call.ordinal) + " (" + name + ") took " + (after == before ? std::string("no replay serial") : "replay serial " + std::to_string(after)) + " where " +
                                 (*taken == 0 ? std::string("the capture's call took none") : "the capture's serial " + std::to_string(*taken) + " maps to " + std::to_string(mapped)) + (plan ? std::string(DepthUnmapped) : ", so the memory delta of call " + std::to_string(mappedOrdinal) + " read in flight cannot be mapped to this replay's submissions"));
    }

    template <typename TFunction>
    void traced(const FrameCall& call, TFunction&& function) {
        if (options.traceSerials) {
            std::fprintf(stderr, "[frame-replay] call %llu (%s, thread %llu, queue 0x%x, %llu dwords at 0x%llx)\n", static_cast<unsigned long long>(call.ordinal), std::string(AgcDriver::FrameCallName(call.kind)).c_str(),
                         static_cast<unsigned long long>(call.thread), call.queue, static_cast<unsigned long long>(call.size), static_cast<unsigned long long>(call.address));
        }
        function();
    }

    template <typename TFunction>
    void registration(TFunction&& function) {
        try {
            function();
        } catch (const std::exception& error) {
            if (recording && options.strict) throw;
            ++(recording ? failedRegistrations : staleRegistrations);
            std::fprintf(stderr, "[frame-replay] %s registration failed: %s\n", recording ? "recorded" : "prologue", error.what());
        }
    }

    static const Shader* shader(std::uint64_t address) {
        return reinterpret_cast<const Shader*>(static_cast<std::uintptr_t>(address));
    }

    static AgcDriver::FlipInfo flipInfo(const FrameCall& call) {
        return {static_cast<std::uint32_t>(call.values.at(0)), static_cast<std::int32_t>(call.values.at(1)), static_cast<std::uint32_t>(call.values.at(2)), static_cast<std::int64_t>(call.values.at(3))};
    }
};

int usage() {
    std::fprintf(stderr, "usage: agc_frame_replay <capture directory> [--no-pace] [--pace-timeout-ms <n>] [--no-release] [--release-after-ms <n>] [--strict]\n"
                         "                        [--repeat <n>] [--cold-pipelines] [--flip-hashes <file>] [--expect-flip-hashes <file>] [--dump <address>:<bytes>:<file>]...\n"
                         "                        [--depth-profile <file> [--depth-cap <n> | --depth-probe <n>] [--depth-hold <position>]...]\n"
                         "  replays the driver calls and guest memory that APS5_FRAME_CAPTURE=<directory> recorded, through this\n"
                         "  build's AGC driver with no title running, and prints one JSON line with the frame times\n"
                         "  --no-pace           issues calls without waiting for the in-flight depth the title had\n"
                         "  --pace-timeout-ms   milliseconds a call waits for the captured in-flight depth once no queue runs (10000); a memory delta\n"
                         "                      read in flight fails the replay once no queue runs and the serials have not moved for that long\n"
                         "  --no-release        never copies recorded CPU stores early to release a GPU wait; a capture of the current calls format never applies a later delta early\n"
                         "  --release-after-ms  milliseconds without a completion before awaited dwords are released (3)\n"
                         "  --no-synthesize     releases a wait only with a value the capture recorded, never with its reference value\n"
                         "  --strict            fails on a pacing timeout or a failed registration inside the captured frames\n"
                         "  --repeat <n>        replays the frames n times in this process, restoring the snapshot before each pass;\n"
                         "                      with n > 1 the frame times and the in-flight depth come from passes 2 to n, after the first warmed the caches\n"
                         "  --cold-pipelines    starts with an empty Vulkan pipeline cache instead of the title's pipeline cache file\n"
                         "  --shaders           replays nothing and maps no guest memory; lists each shader registration with its code\n"
                         "                      address, size and code hash as the snapshot and the captured writes before it hold them,\n"
                         "                      and how many registrations share that code\n"
                         "  --shader-code <dir> as --shaders, and writes each distinct code as <dir>/type<n>-<hash>.bin\n"
                         "  --trace-serials     prints each submission, flip and suspend point with its call ordinal and command\n"
                         "                      address as it is issued; with APS5_TRACE_GPU the driver prints the serial it accepts\n"
                         "                      for each address\n"
                         "  --submits <n>       replays nothing; lists the first n submissions and flips of the captured frames with\n"
                         "                      their queue and serials, the packets of each and the fields of its waits and releases\n"
                         "  --dump              writes guest memory to a file after the replay (hexadecimal or decimal)\n"
                         "  --frames <dir>      writes each recorded and replayed display buffer as reference-<n>.bmp and replay-<n>.bmp\n"
                         "  --flip-hashes <file>\n"
                         "                      hashes each display buffer the capture lists, and its DCC keys, at every flip of the first\n"
                         "                      pass, on the queue worker once the packets before the flip have run and the GPU writes over\n"
                         "                      them are flushed, and writes the hashes to <file> once the pass finished without a failure\n"
                         "  --expect-flip-hashes <file>\n"
                         "                      hashes every pass the same way and fails at the first flip whose hashes differ from <file>,\n"
                         "                      or when a pass presents another number of flips than <file> lists\n"
                         "  --depth-profile <file>\n"
                         "                      starts each suspend point and queue 0 submission of the captured frames once no more\n"
                         "                      submissions are in flight than the bound <file> (aps5-depth-profile 1) gives its position, or\n"
                         "                      than the capture had there, and fails once no queue runs for the pacing timeout; refuses\n"
                         "                      captured frames that are not one thread's suspend points, each followed by the queue 0\n"
                         "                      submissions <file> bounds, or whose calls do not take the serials the capture recorded;\n"
                         "                      needs --no-release; restores a memory delta read with no work in flight once the submissions\n"
                         "                      its store and wait floors name, and those the next call's bound leaves out of flight, have run,\n"
                         "                      and the recorded GPU batches that read the bytes it changes in place have finished\n"
                         "  --depth-cap <n>     lowers every bound of the depth profile to at most n\n"
                         "  --depth-probe <n>   sets every bound of the depth profile to n, for sensitivity runs\n"
                         "  --depth-hold <position>\n"
                         "                      restores the memory deltas before the calls at that position of the depth profile (suspend,\n"
                         "                      s1, s2 and so on) with no queue running, as without the profile; repeatable\n");
    return 2;
}

Dump parseDump(std::string_view text) {
    const auto first = text.find(':');
    const auto second = first == std::string_view::npos ? std::string_view::npos : text.find(':', first + 1);
    if (second == std::string_view::npos) throw std::invalid_argument("frame replay: --dump takes <address>:<bytes>:<file>");
    const auto number = [](std::string_view part) {
        const std::string value(part);
        char* end = nullptr;
        const auto parsed = std::strtoull(value.c_str(), &end, 0);
        if (value.empty() || end != value.c_str() + value.size()) throw std::invalid_argument("frame replay: '" + value + "' is not a number");
        return static_cast<std::uint64_t>(parsed);
    };
    return {number(text.substr(0, first)), number(text.substr(first + 1, second - first - 1)), std::filesystem::path(std::string(text.substr(second + 1)))};
}

void writeDump(const Dump& dump) {
    auto* const pointer = reinterpret_cast<const std::byte*>(static_cast<std::uintptr_t>(dump.address));
    GuestArena::GuestArenaPrepareHostRead_nid_no_patch(pointer, static_cast<std::size_t>(dump.bytes));
    std::ofstream file(dump.path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(pointer), static_cast<std::streamsize>(dump.bytes));
    if (!file.good()) throw std::runtime_error("frame replay: cannot write " + dump.path.string());
}

struct FrameCheck {
    std::size_t compared = 0;
    double identical = 1.0;
    int maxDifference = 0;
};

void writeFrame(const std::filesystem::path& path, const AgcDriver::DisplayBuffer& buffer, std::span<const std::byte> pixels) {
    std::FILE* file = std::fopen(path.string().c_str(), "wb");
    const auto failure = file != nullptr ? AgcDriver::WriteBmpAndClose_nid_no_patch(file, buffer.width, buffer.height, pixels) : AgcDriver::FrameDumpIoFailure{"open", errno};
    if (failure) std::fprintf(stderr, "[frame-replay] cannot write %s (%s failed)\n", path.string().c_str(), failure.operation);
}

AgcDriver::DisplayBuffer recordedDisplayBuffer(const FrameCall& call) {
    AgcDriver::DisplayBuffer buffer{};
    buffer.address = call.address;
    buffer.pixelFormat = call.values[1];
    buffer.width = static_cast<std::uint32_t>(call.values[2]);
    buffer.height = static_cast<std::uint32_t>(call.values[3]);
    buffer.tilingMode = static_cast<std::uint32_t>(call.values[4]);
    buffer.pitchInPixel = static_cast<std::uint32_t>(call.values[5]);
    buffer.dccAddress = call.values[6];
    buffer.dccClearColor = call.values[7];
    buffer.alignment = static_cast<std::uint32_t>(call.values[8]);
    return buffer;
}

FrameCheck compareFrames(const FrameCaptureReader& reader, const Options& options) {
    FrameCheck check;
    if (!options.frames.empty()) std::filesystem::create_directories(options.frames);
    for (const auto& call : reader.Calls()) {
        if (call.kind != FrameCallKind::DisplayBuffer || call.values.size() < 9) continue;
        const auto buffer = recordedDisplayBuffer(call);
        const auto chunk = std::find_if(reader.Chunks().begin(), reader.Chunks().end(), [&](const FrameMemoryChunk& candidate) { return candidate.kind == FrameMemoryKind::Reference && candidate.group == call.values[0]; });
        if (chunk == reader.Chunks().end()) continue;
        const auto reference = reader.Read(*chunk);
        const auto replayed = AgcDriver::ReadDisplayBuffer(buffer);
        ++check.compared;
        std::size_t same = 0;
        const auto pixels = std::min(reference.size(), replayed.size()) / 4;
        for (std::size_t pixel = 0; pixel < pixels; ++pixel) {
            bool equal = true;
            for (std::size_t channel = 0; channel < 4; ++channel) {
                const auto difference = std::abs(static_cast<int>(reference[pixel * 4 + channel]) - static_cast<int>(replayed[pixel * 4 + channel]));
                check.maxDifference = std::max(check.maxDifference, difference);
                equal = equal && difference == 0;
            }
            same += equal ? 1 : 0;
        }
        const auto total = std::max(reference.size(), replayed.size()) / 4;
        check.identical = std::min(check.identical, total == 0 ? 1.0 : static_cast<double>(same) / static_cast<double>(total));
        if (options.frames.empty()) continue;
        const auto index = std::to_string(call.values[0]);
        writeFrame(options.frames / ("reference-" + index + ".bmp"), buffer, reference);
        writeFrame(options.frames / ("replay-" + index + ".bmp"), buffer, replayed);
    }
    return check;
}

std::uint64_t contentHash(const std::byte* bytes, std::size_t size) {
    std::uint64_t hash = 0xcbf29ce484222325ull;
    for (std::size_t index = 0; index < size; ++index) {
        hash ^= std::to_integer<std::uint64_t>(bytes[index]);
        hash *= 0x100000001b3ull;
    }
    return hash;
}

constexpr std::string_view FlipHashesMagic = "aps5-flip-hashes 1";

std::vector<FlipRange> hashedRanges(const FrameCaptureReader& reader) {
    std::vector<FlipRange> ranges;
    for (const auto& call : reader.Calls()) {
        if (call.kind != FrameCallKind::DisplayBuffer || call.values.size() < 9) continue;
        const auto buffer = recordedDisplayBuffer(call);
        ranges.push_back({buffer.address, AgcDriver::DisplayBufferSize(buffer), buffer.alignment, false});
        if (buffer.dccAddress != 0) ranges.push_back({buffer.dccAddress, AgcDriver::DisplayBufferKeyBytes(buffer), 1, true});
    }
    if (ranges.empty()) throw std::runtime_error("frame replay: --flip-hashes and --expect-flip-hashes hash the display buffers a capture lists after its last frame, and this capture lists none");
    for (const auto& chunk : reader.Chunks()) {
        if (chunk.kind != FrameMemoryKind::Delta) continue;
        for (const auto& range : ranges) {
            if (chunk.address >= range.address + range.bytes || range.address >= chunk.address + chunk.bytes) continue;
            throw std::runtime_error("frame replay: memory delta " + std::to_string(chunk.group) + " at " + addressText(chunk.address) + " (" + std::to_string(chunk.bytes) + " bytes) holds bytes of " + (range.keys ? "the DCC keys at " : "the display buffer at ") + addressText(range.address) + ", so their hash at a flip would depend on when the replay restores the delta");
        }
    }
    return ranges;
}

std::vector<FlipHash> readFlipHashes(const std::filesystem::path& path, const std::vector<FlipRange>& ranges) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) throw std::runtime_error("frame replay: cannot open " + path.string());
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::vector<std::string_view> lines;
    for (std::size_t begin = 0; begin < text.size();) {
        const auto end = std::min(text.find('\n', begin), text.size());
        lines.push_back(std::string_view(text).substr(begin, end - begin));
        begin = end + 1;
    }
    if (lines.empty() || lines.front() != FlipHashesMagic) throw std::runtime_error("frame replay: " + path.string() + " does not start with the line '" + std::string(FlipHashesMagic) + "'");
    const auto parse = [](std::string_view line, FlipHash& entry) {
        const auto* cursor = line.data();
        const auto* const end = line.data() + line.size();
        const auto number = [&](std::uint64_t& value, int base) {
            const auto [last, error] = std::from_chars(cursor, end, value, base);
            if (error != std::errc{}) return false;
            cursor = last;
            return true;
        };
        const auto literal = [&](std::string_view expected) {
            if (static_cast<std::size_t>(end - cursor) < expected.size() || std::string_view(cursor, expected.size()) != expected) return false;
            cursor += expected.size();
            return true;
        };
        return number(entry.flip, 10) && literal(" 0x") && number(entry.address, 16) && literal(" ") && number(entry.hash, 16) && cursor == end;
    };
    const auto count = ranges.size();
    std::vector<FlipHash> hashes;
    for (std::size_t index = 1; index < lines.size(); ++index) {
        FlipHash entry;
        if (!parse(lines[index], entry)) throw std::runtime_error("frame replay: line " + std::to_string(index + 1) + " of " + path.string() + " is not '<flip> 0x<address> <hash>'");
        const auto flip = static_cast<std::uint64_t>(hashes.size() / count + 1);
        const auto& range = ranges[hashes.size() % count];
        if (entry.flip != flip || entry.address != range.address) throw std::runtime_error("frame replay: line " + std::to_string(index + 1) + " of " + path.string() + " lists flip " + std::to_string(entry.flip) + " at " + addressText(entry.address) + ", where this capture hashes flip " + std::to_string(flip) + " at " + addressText(range.address));
        hashes.push_back(entry);
    }
    if (hashes.empty()) throw std::runtime_error("frame replay: " + path.string() + " lists no flips");
    if (hashes.size() % count != 0) throw std::runtime_error("frame replay: " + path.string() + " ends inside flip " + std::to_string(hashes.size() / count + 1) + ", after " + std::to_string(hashes.size() % count) + " of the " + std::to_string(count) + " ranges this capture hashes at each flip");
    return hashes;
}

void writeFlipHashes(const std::filesystem::path& path, const std::vector<FlipHash>& hashes) {
    std::string text(FlipHashesMagic);
    text += '\n';
    for (const auto& entry : hashes) {
        char line[80];
        std::snprintf(line, sizeof(line), "%llu 0x%llx %016llx\n", static_cast<unsigned long long>(entry.flip), static_cast<unsigned long long>(entry.address), static_cast<unsigned long long>(entry.hash));
        text += line;
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) throw std::runtime_error("frame replay: cannot open " + path.string());
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!file) throw std::runtime_error("frame replay: cannot write " + path.string());
    file.flush();
    if (!file) throw std::runtime_error("frame replay: cannot flush " + path.string());
    file.close();
    if (!file) throw std::runtime_error("frame replay: cannot close " + path.string());
}

class CapturedMemory {
public:
    explicit CapturedMemory(const FrameCaptureReader& reader) : reader(reader) {
        for (const auto& chunk : reader.Chunks()) {
            if (chunk.kind == FrameMemoryKind::Snapshot) snapshots.push_back(&chunk);
            else if (chunk.kind == FrameMemoryKind::Range) ranges.push_back(&chunk);
        }
        const auto byAddress = [](const FrameMemoryChunk* a, const FrameMemoryChunk* b) { return a->address < b->address; };
        std::sort(snapshots.begin(), snapshots.end(), byAddress);
        std::sort(ranges.begin(), ranges.end(), byAddress);
    }

    std::optional<std::vector<std::byte>> Read(std::uint64_t address, std::uint64_t bytes) {
        if (bytes > std::numeric_limits<std::size_t>::max() || bytes > std::numeric_limits<std::uint64_t>::max() - address) return std::nullopt;
        std::vector<std::byte> result(static_cast<std::size_t>(bytes));
        for (auto at = address; at < address + bytes;) {
            const auto* range = covering(ranges, at);
            if (range == nullptr) return std::nullopt;
            const auto* snapshot = covering(snapshots, at);
            auto end = std::min(address + bytes, range->address + range->bytes);
            if (snapshot != nullptr) {
                end = std::min(end, snapshot->address + snapshot->bytes);
                const auto& data = load(*snapshot);
                std::memcpy(result.data() + (at - address), data.data() + (at - snapshot->address), static_cast<std::size_t>(end - at));
            } else {
                const auto next = std::upper_bound(snapshots.begin(), snapshots.end(), at, [](std::uint64_t value, const FrameMemoryChunk* chunk) { return value < chunk->address; });
                if (next != snapshots.end()) end = std::min(end, (*next)->address);
            }
            at = end;
        }
        for (const auto* delta : deltas) {
            const auto begin = std::max(address, delta->address);
            const auto end = std::min(address + bytes, delta->address + delta->bytes);
            if (begin >= end) continue;
            const auto data = reader.Read(*delta);
            std::memcpy(result.data() + (begin - address), data.data() + (begin - delta->address), static_cast<std::size_t>(end - begin));
        }
        return result;
    }

    void Apply(std::span<const FrameMemoryChunk* const> chunks) {
        deltas.insert(deltas.end(), chunks.begin(), chunks.end());
    }

private:
    static const FrameMemoryChunk* covering(const std::vector<const FrameMemoryChunk*>& chunks, std::uint64_t address) {
        const auto next = std::upper_bound(chunks.begin(), chunks.end(), address, [](std::uint64_t value, const FrameMemoryChunk* chunk) { return value < chunk->address; });
        if (next == chunks.begin()) return nullptr;
        const auto* chunk = *std::prev(next);
        return address < chunk->address + chunk->bytes ? chunk : nullptr;
    }

    const std::vector<std::byte>& load(const FrameMemoryChunk& chunk) {
        if (cached != &chunk) {
            data = reader.Read(chunk);
            cached = &chunk;
        }
        return data;
    }

    const FrameCaptureReader& reader;
    std::vector<const FrameMemoryChunk*> snapshots;
    std::vector<const FrameMemoryChunk*> ranges;
    std::vector<const FrameMemoryChunk*> deltas;
    const FrameMemoryChunk* cached = nullptr;
    std::vector<std::byte> data;
};

int listShaders(const Options& options) {
    const FrameCaptureReader reader(options.directory);
    CapturedMemory memory(reader);
    if (!options.shaderCode.empty()) std::filesystem::create_directories(options.shaderCode);
    std::map<std::uint64_t, std::vector<const FrameMemoryChunk*>> groups;
    for (const auto& chunk : reader.Chunks()) {
        if (chunk.kind == FrameMemoryKind::Delta) groups[chunk.group].push_back(&chunk);
    }
    const auto& calls = reader.Calls();
    const auto frameStart = std::find_if(calls.begin(), calls.end(), [](const FrameCall& call) { return call.kind == FrameCallKind::FrameStart; });
    std::map<std::uint64_t, std::size_t> copies;
    std::size_t listed = 0, inWindow = 0, unreadable = 0;
    for (auto call = calls.begin(); call != calls.end(); ++call) {
        if (call->kind == FrameCallKind::Memory) {
            if (const auto found = groups.find(call->values.at(0)); found != groups.end()) memory.Apply(found->second);
            continue;
        }
        if (call->kind != FrameCallKind::RegisterShader) continue;
        const auto header = memory.Read(call->address, sizeof(Shader));
        if (!header) {
            ++unreadable;
            continue;
        }
        Shader fields{};
        std::memcpy(&fields, header->data(), sizeof(Shader));
        const auto code = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(const_cast<const void*>(fields.code)));
        const auto bytes = fields.shader_size != 0 ? memory.Read(code, fields.shader_size) : std::nullopt;
        if (!bytes) {
            ++unreadable;
            continue;
        }
        const auto hash = contentHash(bytes->data(), bytes->size());
        const auto copy = ++copies[hash];
        if (copy == 1 && !options.shaderCode.empty()) {
            char name[64];
            std::snprintf(name, sizeof(name), "type%u-%016llx.bin", static_cast<unsigned>(fields.type), static_cast<unsigned long long>(hash));
            const auto path = options.shaderCode / name;
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            if (!file.is_open()) throw std::runtime_error("frame replay: cannot open " + path.string());
            file.write(reinterpret_cast<const char*>(bytes->data()), static_cast<std::streamsize>(bytes->size()));
            if (!file) throw std::runtime_error("frame replay: cannot write " + path.string());
            file.flush();
            if (!file) throw std::runtime_error("frame replay: cannot flush " + path.string());
            file.close();
            if (!file) throw std::runtime_error("frame replay: cannot close " + path.string());
        }
        const bool window = frameStart != calls.end() && call > frameStart;
        ++listed;
        inWindow += window ? 1 : 0;
        std::printf("{\"ordinal\":%llu,\"in_window\":%s,\"header\":%llu,\"code\":%llu,\"bytes\":%u,\"type\":%u,\"hash\":\"%016llx\",\"copy\":%zu}\n", static_cast<unsigned long long>(call->ordinal), window ? "true" : "false",
                    static_cast<unsigned long long>(call->address), static_cast<unsigned long long>(code), fields.shader_size, static_cast<unsigned>(fields.type), static_cast<unsigned long long>(hash), copy);
    }
    std::printf("{\"registrations\":%zu,\"in_window\":%zu,\"distinct_code\":%zu,\"unreadable\":%zu}\n", listed, inWindow, copies.size(), unreadable);
    std::fflush(stdout);
    return 0;
}

std::string_view packetName(std::uint32_t opcode) {
    switch (opcode) {
    case 0x10: return "NOP";
    case 0x15: return "DISPATCH_DIRECT";
    case 0x16: return "DISPATCH_INDIRECT";
    case 0x27: return "DRAW_INDEX_2";
    case 0x2d: return "DRAW_INDEX_AUTO";
    case 0x35: return "DRAW_INDEX_OFFSET_2";
    case 0x37: return "WRITE_DATA";
    case 0x3c: return "WAIT_REG_MEM";
    case 0x3f: return "INDIRECT_BUFFER";
    case 0x46: return "EVENT_WRITE";
    case 0x49: return "RELEASE_MEM";
    case 0x50: return "DMA_DATA";
    case 0x58: return "ACQUIRE_MEM";
    case 0x69: return "SET_CONTEXT_REG";
    case 0x76: return "SET_SH_REG";
    case 0x79: return "SET_UCONFIG_REG";
    default: return {};
    }
}

void describeSubmission(const FrameCall& call, CapturedMemory& memory) {
    const auto bytes = call.size <= std::numeric_limits<std::uint64_t>::max() / sizeof(std::uint32_t) ? memory.Read(call.address, call.size * sizeof(std::uint32_t)) : std::nullopt;
    if (!bytes) {
        std::printf("    (command buffer not readable)\n");
        return;
    }
    std::vector<std::uint32_t> words(static_cast<std::size_t>(call.size));
    if (!bytes->empty()) std::memcpy(words.data(), bytes->data(), bytes->size());
    std::map<std::string, std::size_t> counts;
    for (std::size_t at = 0; at < call.size;) {
        const auto header = words[at];
        const auto type = header >> 30u;
        const auto length = AgcDriver::Pm4::PacketWords(header);
        if (length > words.size() - at) {
            std::printf("    @%zu (truncated packet: %zu dwords, %zu available)\n", at, length, words.size() - at);
            break;
        }
        if (type == 3u && !AgcDriver::Pm4::FillerPacket(header)) {
            const auto opcode = (header >> 8u) & 0xffu;
            const auto name = packetName(opcode);
            char label[24];
            std::snprintf(label, sizeof(label), "0x%02x", opcode);
            const std::string key = name.empty() ? std::string(label) : std::string(name);
            const auto word = [&](std::size_t index) { return index < length ? words[at + index] : 0u; };
            if (opcode == 0x3c) {
                std::printf("    @%zu WAIT_REG_MEM function %u space %u address 0x%llx reference 0x%x mask 0x%x\n", at, word(1) & 7u, (word(1) >> 4u) & 3u,
                            static_cast<unsigned long long>(word(2)) | (static_cast<unsigned long long>(word(3)) << 32u), word(4), word(5));
            } else if (opcode == 0x49 || opcode == 0x37 || opcode == 0x46) {
                std::printf("    @%zu %s", at, key.c_str());
                for (std::size_t index = 1; index < length && index < 8; ++index) std::printf(" %08x", word(index));
                std::printf("\n");
            }
            ++counts[key];
        }
        at += length;
    }
    std::printf("    packets:");
    for (const auto& [name, count] : counts) std::printf(" %s x%zu", name.c_str(), count);
    std::printf("\n");
}

int listSubmits(const Options& options) {
    const FrameCaptureReader reader(options.directory);
    CapturedMemory memory(reader);
    std::map<std::uint64_t, std::vector<const FrameMemoryChunk*>> groups;
    for (const auto& chunk : reader.Chunks()) {
        if (chunk.kind == FrameMemoryKind::Delta) groups[chunk.group].push_back(&chunk);
    }
    const auto& calls = reader.Calls();
    const auto frameStart = std::find_if(calls.begin(), calls.end(), [](const FrameCall& call) { return call.kind == FrameCallKind::FrameStart; });
    std::size_t listed = 0;
    for (auto call = calls.begin(); call != calls.end() && listed < options.submits; ++call) {
        if (call->kind == FrameCallKind::Memory) {
            if (const auto found = groups.find(call->values.at(0)); found != groups.end()) memory.Apply(found->second);
            continue;
        }
        if (call < frameStart) continue;
        if (call->kind != FrameCallKind::Submit && call->kind != FrameCallKind::Flip && call->kind != FrameCallKind::ReserveEopFlip) continue;
        ++listed;
        std::printf("#%zu ordinal %llu %s thread %llu queue 0x%x serials %llu/%llu address 0x%llx dwords %llu\n", listed, static_cast<unsigned long long>(call->ordinal), std::string(AgcDriver::FrameCallName(call->kind)).c_str(),
                    static_cast<unsigned long long>(call->thread), call->queue, static_cast<unsigned long long>(call->completed), static_cast<unsigned long long>(call->accepted),
                    static_cast<unsigned long long>(call->address), static_cast<unsigned long long>(call->size));
        if (call->kind == FrameCallKind::Submit) describeSubmission(*call, memory);
    }
    std::fflush(stdout);
    return 0;
}

struct Pass {
    std::size_t firstFrame = 0;
    std::size_t frames = 0;
    double seconds = 0.0;
    std::uint64_t paceTimeouts = 0;
    std::uint64_t synthesized = 0;
    std::optional<std::uint64_t> gpuNs;
    std::optional<std::uint64_t> presents;
};

std::string milliseconds(std::optional<std::uint64_t> nanoseconds) {
    if (!nanoseconds) return "null";
    char text[32];
    std::snprintf(text, sizeof(text), "%.3f", static_cast<double>(*nanoseconds) / 1e6);
    return text;
}

std::string perFrame(std::optional<std::uint64_t> nanoseconds, std::size_t frames) {
    if (!nanoseconds || frames == 0) return "null";
    char text[32];
    std::snprintf(text, sizeof(text), "%.3f", static_cast<double>(*nanoseconds) / 1e6 / static_cast<double>(frames));
    return text;
}

double percentile(std::vector<double> values, double fraction) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    return values[std::min(values.size() - 1, static_cast<std::size_t>(fraction * static_cast<double>(values.size())))];
}

std::string jsonText(std::string_view value) {
    std::string text = "\"";
    for (const char character : value) {
        if (character == '"' || character == '\\') {
            text += '\\';
            text += character;
        } else if (static_cast<unsigned char>(character) < 0x20u) {
            char escaped[8];
            std::snprintf(escaped, sizeof(escaped), "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(character)));
            text += escaped;
        } else {
            text += character;
        }
    }
    return text + "\"";
}

std::string depthJson(const Options& options, const Replay& replay) {
    if (!options.depth) return "null";
    const auto& profile = *options.depth;
    const auto number = [](const std::optional<std::uint64_t>& value) { return value ? std::to_string(*value) : std::string("null"); };
    const auto name = profile.path.filename().u8string();
    char gate[32];
    std::snprintf(gate, sizeof(gate), "%.3f", Replay::secondsOf(replay.gateWaitedNs));
    char reads[32];
    std::snprintf(reads, sizeof(reads), "%.3f", Replay::secondsOf(replay.readCheckNs));
    char issueWaited[32];
    std::snprintf(issueWaited, sizeof(issueWaited), "%.3f", Replay::secondsOf(replay.issueWaitedNs));
    char scans[32];
    std::snprintf(scans, sizeof(scans), "%.3f", Replay::secondsOf(replay.scanNs));
    std::string first = "null";
    if (replay.firstRead) {
        const auto& hit = *replay.firstRead;
        first = "{\"call\":" + std::to_string(hit.call) + ",\"address\":\"" + addressText(hit.address) + "\",\"bytes\":" + std::to_string(hit.bytes) + ",\"batch\":" + std::to_string(hit.read.batch) + ",\"queue\":\"" + addressText(hit.read.queue) + "\",\"kind\":" + jsonText(readKindName(hit.read.kind)) +
                ",\"open\":" + (hit.read.open ? "true" : "false") + "}";
    }
    std::string text = "{\"profile\":" + jsonText(std::string(name.begin(), name.end())) + ",\"cap\":" + number(options.depthCap) + ",\"probe\":" + number(options.depthProbe) + ",\"frames\":" + std::to_string(replay.plan ? replay.plan->frames : 0) +
                       ",\"fast_deltas\":" + std::to_string(replay.fastDeltas.load()) + ",\"gate_wait_s\":" + gate + ",\"tail\":" + std::to_string(replay.tailHeld) + ",\"read_waits\":" + std::to_string(replay.readWaits.load()) + ",\"read_wait_first\":" + first +
                       ",\"read_check_s\":" + reads + ",\"issue_waits\":{\"inflight\":" + std::to_string(replay.issueWaits[0]) + "},\"issue_wait_s\":" + issueWaited + ",\"scan_s\":" + scans + ",\"unsettled_landings\":" + std::to_string(replay.unsettledLandings) + ",\"positions\":[";
    const auto spread = [](std::vector<std::uint64_t> values, bool deepest) {
        if (values.empty()) return std::string("null");
        std::sort(values.begin(), values.end());
        const auto at = [&](double fraction) { return std::to_string(values[std::min(values.size() - 1, static_cast<std::size_t>(fraction * static_cast<double>(values.size())))]); };
        return "[" + std::to_string(values.front()) + "," + at(0.1) + "," + at(0.5) + (deepest ? "," + std::to_string(values.back()) : std::string()) + "]";
    };
    const PositionCensus none{};
    for (std::size_t position = 0; position < profile.names.size(); ++position) {
        const auto& entry = position < replay.census.size() ? replay.census[position] : none;
        std::string held;
        for (std::size_t cause = 0; cause < HeldCauses; ++cause) {
            if (static_cast<DepthCause>(cause) != DepthCause::Tail) held += std::string(held.empty() ? "" : ",") + "\"" + std::string(HeldNames[cause]) + "\":" + std::to_string(entry.held[cause]);
        }
        text += std::string(position == 0 ? "" : ",") + "{\"position\":" + jsonText(profile.names[position]) + ",\"bound\":" + std::to_string(profile.bounds[position]) + ",\"observed\":" + std::to_string(profile.observed[position]) +
                ",\"issued\":" + std::to_string(entry.issue.size()) + ",\"issue_depth\":" + spread(entry.issue, true) + ",\"issue_bound\":" + spread(entry.issueBound, false) + ",\"ceiling\":" + spread(entry.ceiling, false) + ",\"restored\":" + std::to_string(entry.restore.size()) +
                ",\"restore_depth\":" + spread(entry.restore, true) + ",\"by_depth\":" + std::to_string(entry.byDepth) + ",\"by_store\":" + std::to_string(entry.byStore) + ",\"by_wait\":" + std::to_string(entry.byWait) +
                ",\"cpu_waits\":" + std::to_string(entry.cpuWaits) + ",\"gpu_waits\":" + std::to_string(entry.gpuWaits) + ",\"held\":{" + held + "}}";
    }
    return text + "]}";
}

int run(const Options& options) {
    if (std::error_code error; !options.flipHashes.empty() && !options.expectedFlipHashes.empty() && std::filesystem::equivalent(options.flipHashes, options.expectedFlipHashes, error)) throw std::runtime_error("frame replay: --flip-hashes and --expect-flip-hashes name the same file " + options.flipHashes.string());
    const auto loadStart = Clock::now();
    const FrameCaptureReader reader(options.directory);
    if (reader.Truncated()) std::fprintf(stderr, "[frame-replay] the capture is truncated; replaying the complete records\n");
    const bool hashing = !options.flipHashes.empty() || !options.expectedFlipHashes.empty();
    std::vector<FlipRange> ranges;
    std::vector<FlipHash> expected;
    if (hashing) {
        ranges = hashedRanges(reader);
        if (!options.expectedFlipHashes.empty()) expected = readFlipHashes(options.expectedFlipHashes, ranges);
    }
    if (options.pace) requireRecordedSerials(reader);
    if (options.depth) {
        requireDepthOptions(options);
        requireDepthCapture(reader, options);
        if (!AgcDriverGpuReadTracking_nid_postfix()) throw std::runtime_error("frame replay: --depth-profile waits for recorded GPU work that reads a delta's bytes in place, and APS5_COPY_READ_TRACKING=0 turned that tracking off");
        if (!frameStartSerial(reader)) throw std::runtime_error(std::string(DepthWithoutFrameStart));
        const auto& captured = reader.Calls();
        const auto start = std::find_if(captured.begin(), captured.end(), [](const FrameCall& call) { return call.kind == FrameCallKind::FrameStart; });
        planDepth(std::span(captured).subspan(static_cast<std::size_t>(start - captured.begin()) + 1), *options.depth);
    }
    auto skipped = restoreMemory(reader);
    const auto loaded = Clock::now();
    Replay replay{options, reader};
    if (hashing) replay.frames.HashFlips(std::move(ranges), std::move(expected), options.expectedFlipHashes.string());
    replay.skipped = std::move(skipped);
    for (const auto& chunk : reader.Chunks()) {
        if (chunk.kind == FrameMemoryKind::Delta) replay.groups[chunk.group].push_back(&chunk);
    }
    const auto& calls = reader.Calls();
    const auto frameStart = std::find_if(calls.begin(), calls.end(), [](const FrameCall& call) { return call.kind == FrameCallKind::FrameStart; });
    double titleSeconds = 0.0;
    std::uint64_t titleUnknown = 0;
    for (auto call = frameStart == calls.end() ? calls.end() : frameStart + 1; call != calls.end(); ++call) {
        if (!AgcDriver::FrameTitleCall(call->kind)) continue;
        if (const auto title = titleNs(reader, *call)) titleSeconds += static_cast<double>(*title) / 1e9;
        else ++titleUnknown;
    }
    AgcDriverRequestBatchStamps_nid_postfix();
    AgcDriverCountReplayPresents_nid_postfix();
    for (auto call = calls.begin(); call != frameStart; ++call) replay.execute(*call);
    const auto journalStart = Clock::now();
    AgcDriverWaitForPipelineJournal_nid_postfix();
    const auto journalSeconds = std::chrono::duration<double>(Clock::now() - journalStart).count();
    std::set<std::uint32_t> initialOutputs;
    for (const auto& [handle, output] : replay.outputs) initialOutputs.insert(handle);
    std::vector<Pass> passes;
    for (std::size_t pass = 0; pass < options.repeat && (pass == 0 || replay.unfinished == 0); ++pass) {
        if (pass > 0) {
            resetMemory(reader, replay.skipped);
            replay.pass = pass;
            replay.recording = false;
            for (auto output = replay.outputs.begin(); output != replay.outputs.end();) {
                if (initialOutputs.contains(output->first)) {
                    ++output;
                    continue;
                }
                AgcDriverUnregisterVideoOutput_nid_postfix(output->first, output->second);
                output = replay.outputs.erase(output);
            }
            for (const auto handle : initialOutputs) {
                if (replay.outputs.contains(handle)) continue;
                auto output = std::make_shared<ReplayOutput>(replay.frames);
                AgcDriverRegisterVideoOutput_nid_postfix(handle, output);
                replay.outputs.emplace(handle, std::move(output));
            }
            for (auto call = calls.begin(); call != frameStart; ++call) {
                if (call->kind == FrameCallKind::ReserveEopFlip) replay.execute(*call);
            }
        }
        replay.frames.StartPass(pass);
        Pass measured{replay.frames.Times().size(), 0, 0.0, replay.paceTimeouts.load(), replay.syntheticReleases.load()};
        replay.recording = true;
        std::uint64_t gpuBefore = 0;
        std::uint64_t untimedBefore = 0;
        AgcDriverBatchGpuTime_nid_postfix(&gpuBefore, &untimedBefore);
        const auto presentsBefore = AgcDriverReplayPresents_nid_postfix();
        replay.startedAt = Clock::now();
        if (frameStart != calls.end()) replay.runWindow(std::span(calls).subspan(static_cast<std::size_t>(frameStart - calls.begin()) + 1));
        else AgcDriverWaitIdle_nid_postfix();
        replay.endedAt = Clock::now();
        if (replay.unfinished == 0) replay.frames.EndPass();
        if (pass == 0 && !options.flipHashes.empty() && replay.unfinished == 0 && !replay.frames.Failure()) writeFlipHashes(options.flipHashes, replay.frames.Written());
        if (const auto failure = replay.frames.Failure()) std::rethrow_exception(failure);
        if (replay.unfinished == 0) {
            AgcDriverWaitIdle_nid_postfix();
            std::uint64_t gpuAfter = 0;
            std::uint64_t untimedAfter = 0;
            AgcDriverBatchGpuTime_nid_postfix(&gpuAfter, &untimedAfter);
            if (untimedAfter == untimedBefore) measured.gpuNs = gpuAfter - gpuBefore;
            measured.presents = AgcDriverReplayPresents_nid_postfix() - presentsBefore;
        }
        measured.frames = replay.frames.Times().size() - measured.firstFrame;
        measured.seconds = std::chrono::duration<double>(replay.endedAt - replay.startedAt).count();
        measured.paceTimeouts = replay.paceTimeouts.load() - measured.paceTimeouts;
        measured.synthesized = replay.syntheticReleases.load() - measured.synthesized;
        passes.push_back(measured);
    }
    FrameCheck check;
    if (replay.unfinished == 0) {
        for (const auto& dump : options.dumps) writeDump(dump);
        check = compareFrames(reader, options);
    } else if (!options.dumps.empty()) {
        std::fprintf(stderr, "[frame-replay] no dumps written: the GPU work did not finish\n");
    }
    const auto ready = replay.frames.Times();
    const std::size_t firstMeasured = passes.size() > 1 ? 1 : 0;
    std::vector<double> frameMs;
    std::string frameList;
    std::string passList;
    double seconds = 0.0;
    std::size_t frames = 0;
    std::optional<std::uint64_t> gpuNs = std::uint64_t{0};
    for (std::size_t index = 0; index < passes.size(); ++index) {
        const auto& measured = passes[index];
        std::vector<double> passMs;
        for (auto frame = measured.firstFrame + 1; frame < measured.firstFrame + measured.frames; ++frame) passMs.push_back(std::chrono::duration<double, std::milli>(ready[frame] - ready[frame - 1]).count());
        char entry[256];
        std::snprintf(entry, sizeof(entry), "%s{\"replay_s\":%.3f,\"frames\":%zu,\"p50\":%.3f,\"pace_timeouts\":%llu,\"synthesized\":%llu,\"gpu_ms\":%s,\"presents\":%s}", passList.empty() ? "" : ",", measured.seconds, measured.frames,
                      percentile(passMs, 0.5), static_cast<unsigned long long>(measured.paceTimeouts), static_cast<unsigned long long>(measured.synthesized), milliseconds(measured.gpuNs).c_str(), measured.presents ? std::to_string(*measured.presents).c_str() : "null");
        passList += entry;
        if (index < firstMeasured) continue;
        for (const auto value : passMs) frameList += (frameList.empty() ? "" : ",") + std::to_string(static_cast<long long>(value));
        frameMs.insert(frameMs.end(), passMs.begin(), passMs.end());
        seconds += measured.seconds;
        frames += measured.frames;
        gpuNs = gpuNs && measured.gpuNs ? std::optional(*gpuNs + *measured.gpuNs) : std::nullopt;
    }
    const auto slowest = frameMs.empty() ? 0.0 : *std::max_element(frameMs.begin(), frameMs.end());
    std::printf("{\"frames\":%zu,\"replay_s\":%.3f,\"fps\":%.3f,\"frame_ms\":{\"p50\":%.3f,\"p90\":%.3f,\"max\":%.3f,\"each\":[%s]},\"passes\":[%s],\"kept_reservations\":%llu,\"submits\":%llu,\"delta_mib\":%.1f,\"pace_timeouts\":%llu,\"pace_wait_s\":%.3f,\"memory_held_wait_s\":%.3f,\"memory_in_flight_wait_s\":%.3f,\"memory_gpu_wait_s\":%.3f,\"memory_gpu_drains\":%llu,\"memory_gpu_waits\":%llu,\"memory_gpu_submits\":%llu,\"restore_s\":%.3f,\"restore_decode_s\":%.3f,\"restore_decode_work_s\":%.3f,\"restore_decode_threads\":%zu,\"in_flight_max\":%llu,\"in_flight_mean\":%.3f,\"stale_registrations\":%llu,\"failed_registrations\":%llu,\"skipped_mib\":%.1f,\"threads\":%zu,\"released_waits\":%llu,\"of_them_from_earlier_writes\":%llu,\"of_them_synthesized\":%llu,\"unfinished\":%llu,\"frames_compared\":%zu,\"pixels_identical\":%.6f,\"max_channel_diff\":%d,\"title_s\":%.3f,\"title_unknown\":%llu,\"load_s\":%.3f,\"journal_s\":%.3f,\"flips_hashed\":%llu,\"flip_hash_s\":%.3f,\"depth\":%s,\"gpu_ms_per_frame\":%s}\n",
                frames, seconds, seconds > 0 ? static_cast<double>(frames) / seconds : 0.0, percentile(frameMs, 0.5), percentile(frameMs, 0.9), slowest, frameList.c_str(), passList.c_str(),
                static_cast<unsigned long long>(replay.keptReservations.load()),
                static_cast<unsigned long long>(replay.submits.load()), static_cast<double>(replay.deltaBytes.load()) / (1024.0 * 1024.0), static_cast<unsigned long long>(replay.paceTimeouts.load()),
                replay.paceWaitedSeconds(), Replay::secondsOf(replay.heldWaitedNs), Replay::secondsOf(replay.inFlightWaitedNs), Replay::secondsOf(replay.memoryGpuWaitedNs), static_cast<unsigned long long>(replay.memoryGpuDrains.load()), static_cast<unsigned long long>(replay.memoryGpuWaits.load()), static_cast<unsigned long long>(replay.memoryGpuSubmits.load()), Replay::secondsOf(replay.restoredNs), Replay::secondsOf(replay.decodeWallNs), Replay::secondsOf(replay.decodeWorkNs), replay.decoders ? replay.decoders->Participants() : std::size_t{0}, static_cast<unsigned long long>(replay.depthMax.load()), replay.depthMean(),
                static_cast<unsigned long long>(replay.staleRegistrations.load()), static_cast<unsigned long long>(replay.failedRegistrations.load()),
                static_cast<double>(replay.skipped.bytes) / (1024.0 * 1024.0), replay.threads, static_cast<unsigned long long>(replay.earlyReleases.load()), static_cast<unsigned long long>(replay.lateReleases.load()),
                static_cast<unsigned long long>(replay.syntheticReleases.load()),
                static_cast<unsigned long long>(replay.unfinished), check.compared, check.identical, check.maxDifference, titleSeconds, static_cast<unsigned long long>(titleUnknown), std::chrono::duration<double>(loaded - loadStart).count(), journalSeconds,
                static_cast<unsigned long long>(replay.frames.HashedFlips()), replay.frames.HashSeconds(), depthJson(options, replay).c_str(), perFrame(gpuNs, frames).c_str());
    std::fflush(stdout);
    return replay.unfinished == 0 ? 0 : 3;
}

void configureEnvironment(const Options& options) {
#ifdef _WIN32
    _putenv_s("APS5_FRAME_CAPTURE", "");
    _putenv_s("APS5_HEADLESS_PIPELINE_CACHE", options.coldPipelines ? "" : "1");
#else
    unsetenv("APS5_FRAME_CAPTURE");
    if (options.coldPipelines) unsetenv("APS5_HEADLESS_PIPELINE_CACHE");
    else setenv("APS5_HEADLESS_PIPELINE_CACHE", "1", 1);
#endif
}

}

int main(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (argument == "--no-pace") options.pace = false;
        else if (argument == "--no-release") options.release = false;
        else if (argument == "--no-synthesize") options.synthesize = false;
        else if (argument == "--pace-timeout-ms" && index + 1 < argc) options.paceTimeout = std::chrono::milliseconds(std::strtoll(argv[++index], nullptr, 10));
        else if (argument == "--release-after-ms" && index + 1 < argc) options.releaseAfter = std::chrono::milliseconds(std::strtoll(argv[++index], nullptr, 10));
        else if (argument == "--strict") options.strict = true;
        else if (argument == "--repeat" && index + 1 < argc) options.repeat = std::max<std::size_t>(1, std::strtoull(argv[++index], nullptr, 10));
        else if (argument == "--shaders") options.shaders = true;
        else if (argument == "--shader-code" && index + 1 < argc) {
            options.shaders = true;
            options.shaderCode = argv[++index];
        }
        else if (argument == "--submits" && index + 1 < argc) {
            const std::string_view value = argv[++index];
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), options.submits);
            if (error != std::errc{} || end != value.data() + value.size() || options.submits == 0) return usage();
        } else if (argument == "--trace-serials") options.traceSerials = true;
        else if (argument == "--cold-pipelines") options.coldPipelines = true;
        else if (argument == "--dump" && index + 1 < argc) options.dumps.push_back(parseDump(argv[++index]));
        else if (argument == "--frames" && index + 1 < argc) options.frames = argv[++index];
        else if (argument == "--flip-hashes" && index + 1 < argc) options.flipHashes = argv[++index];
        else if (argument == "--expect-flip-hashes" && index + 1 < argc) options.expectedFlipHashes = argv[++index];
        else if (argument == "--depth-profile" && index + 1 < argc) {
            if (!options.depthFile.empty() || *argv[index + 1] == '\0') return usage();
            options.depthFile = argv[++index];
        } else if ((argument == "--depth-cap" || argument == "--depth-probe") && index + 1 < argc) {
            auto& bound = argument == "--depth-cap" ? options.depthCap : options.depthProbe;
            const std::string_view value = argv[++index];
            std::uint64_t parsed = 0;
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
            if (bound || error != std::errc{} || end != value.data() + value.size()) return usage();
            bound = parsed;
        } else if (argument == "--depth-hold" && index + 1 < argc) {
            if (*argv[index + 1] == '\0') return usage();
            options.depthHold.emplace_back(argv[++index]);
        } else if (!argument.starts_with("-") && options.directory.empty()) options.directory = argument;
        else return usage();
    }
    if (options.directory.empty()) return usage();
    if ((options.shaders || options.submits != 0) && (!options.flipHashes.empty() || !options.expectedFlipHashes.empty())) return usage();
    if ((options.depthCap || options.depthProbe) && (options.depthFile.empty() || (options.depthCap && options.depthProbe))) return usage();
    if (!options.depthHold.empty() && options.depthFile.empty()) return usage();
    if (!options.depthFile.empty() && (!options.pace || options.shaders || options.submits != 0)) return usage();
    configureEnvironment(options);
    int code = 2;
    const bool listing = options.shaders || options.submits != 0;
    try {
        if (!options.depthFile.empty()) options.depth = readDepthProfile(options.depthFile);
        code = options.shaders ? listShaders(options) : options.submits != 0 ? listSubmits(options) : run(options);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "agc_frame_replay: %s\n", error.what());
    }
    if (!listing) {
        try {
            AgcDriverShutdown_nid_postfix();
        } catch (const std::exception& error) {
            std::fprintf(stderr, "agc_frame_replay: %s\n", error.what());
            if (code == 0) code = 2;
        }
        if (code == 0) return 0;
    }
    std::fflush(stdout);
    std::fflush(stderr);
    std::_Exit(code);
}
