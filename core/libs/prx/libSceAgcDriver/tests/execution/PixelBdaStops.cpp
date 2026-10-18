#include "prx/libSceAgcDriver/Execution/include/VulkanDevice.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Draw.hpp"
#include "prx/libc/include/GuestAllocations.hpp"
#include "Recompiler.hpp"
#include "VulkanTestDevice.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <span>
#include <string>
#include <vector>

namespace {

using AgcDriver::Graphics::Require;
using ShaderRecompiler::ShaderStage;

constexpr std::uint32_t Width = 64;
constexpr std::uint32_t Height = 32;
constexpr std::uint32_t Reads = 160;
constexpr std::size_t GuestBytes = 65536;
alignas(256) std::array<std::byte, Width * Height * 4> Pixels{};

alignas(256) constexpr std::array<std::uint32_t, 6> VertexCode{
    0xe0382000, 0x80000005, 0xbf8c3f70, 0xf80008cf, 0x03020100, 0xbf810000,
};

constexpr std::array<std::uint32_t, 3> Prologue{0x7e040200, 0x7e060201, 0x7e0a02f2};
constexpr std::array<std::uint32_t, 5> ChainedRead{0xdc308000, 0x047d0002, 0xbf8c3f70, 0x4a040902, 0x4a0a0905};
constexpr std::array<std::uint32_t, 3> Epilogue{0xf800180f, 0x05050505, 0xbf810000};

std::vector<std::uint32_t> PixelCode() {
    std::vector<std::uint32_t> code(Prologue.begin(), Prologue.end());
    for (std::uint32_t read = 0; read < Reads; ++read) code.insert(code.end(), ChainedRead.begin(), ChainedRead.end());
    code.insert(code.end(), Epilogue.begin(), Epilogue.end());
    return code;
}

class GuestBlock {
public:
    GuestBlock() {
#ifdef _WIN32
        block = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, GuestBytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
#else
        block = static_cast<std::uint8_t*>(std::aligned_alloc(GuestBytes, GuestBytes));
#endif
        Require(block != nullptr, "pixel BDA stops: cannot allocate the guest block");
        std::memset(block, 0, GuestBytes);
        GuestAllocations::Mutation().Add(block, GuestBytes, true, true, true);
    }

    ~GuestBlock() {
        GuestAllocations::Mutation().Remove(block);
#ifdef _WIN32
        VirtualFree(block, 0, MEM_RELEASE);
#else
        std::free(block);
#endif
    }

    GuestBlock(const GuestBlock&) = delete;
    GuestBlock& operator=(const GuestBlock&) = delete;

    std::uint64_t Address() const { return reinterpret_cast<std::uintptr_t>(block); }

private:
    std::uint8_t* block = nullptr;
};

std::array<std::uint32_t, 4> BufferDescriptor(const void* data, std::uint32_t stride, std::uint32_t count) {
    const auto address = reinterpret_cast<std::uintptr_t>(data);
    return {static_cast<std::uint32_t>(address), static_cast<std::uint32_t>((address >> 32u) & 0xffffu) | (stride << 16u), count, 0x01016facu};
}

std::size_t CountOps(std::span<const std::uint32_t> words, spv::Op op) {
    std::size_t count = 0;
    for (std::size_t at = 5; at < words.size() && (words[at] >> 16u) != 0; at += words[at] >> 16u) {
        if ((words[at] & 0xffffu) == op) ++count;
    }
    return count;
}

std::string Draw(AgcDriver::VulkanDevice& device, std::uint64_t address, std::size_t& exits) {
    Pixels.fill(std::byte{0x40});
    const auto target = device.Target();
    static const std::array<std::array<float, 4>, 3> triangle{{{-1.0f, -1.0f, 0.5f, 1.0f}, {3.0f, -1.0f, 0.5f, 1.0f}, {-1.0f, 3.0f, 0.5f, 1.0f}}};
    std::vector<std::uint32_t> vertexUserData(4, 0u);
    const auto vertexBuffer = BufferDescriptor(triangle.data(), 16u, static_cast<std::uint32_t>(triangle.size()));
    std::copy(vertexBuffer.begin(), vertexBuffer.end(), vertexUserData.begin());
    const std::array<ShaderRecompiler::MemoryRegion, 1> vertexMemory{{{reinterpret_cast<std::uintptr_t>(VertexCode.data()), std::as_bytes(std::span(VertexCode))}}};
    ShaderRecompiler::RecompileRequest vertex{
        {ShaderStage::Vertex, reinterpret_cast<std::uintptr_t>(VertexCode.data()), VertexCode, 0, {}},
        {64u, 0, vertexUserData, std::nullopt, std::nullopt, ShaderRecompiler::ShaderVertexStageInfo{}, vertexMemory},
        target,
        {0, 0, 0, 64}
    };
    vertex.useCache = false;
    const auto vertexResult = ShaderRecompiler::Recompile(vertex);
    const auto vertexPush = static_cast<std::uint32_t>(vertexResult.pushConstants.size());

    alignas(256) static const auto pixelCode = PixelCode();
    ShaderRecompiler::ShaderPixelStageInfo pixel{};
    pixel.inputAddr = ShaderRecompiler::PixelInputBit(ShaderRecompiler::PixelInput::PositionX) | ShaderRecompiler::PixelInputBit(ShaderRecompiler::PixelInput::PositionY);
    pixel.posX = true;
    pixel.posY = true;
    pixel.targetOutputMode[0] = 9;
    pixel.targetExportMapping.fill(0xe4u);
    const std::vector<std::uint32_t> pixelUserData{static_cast<std::uint32_t>(address), static_cast<std::uint32_t>(address >> 32u)};
    const std::array<ShaderRecompiler::MemoryRegion, 1> pixelMemory{{{reinterpret_cast<std::uintptr_t>(pixelCode.data()), std::as_bytes(std::span(pixelCode))}}};
    ShaderRecompiler::RecompileRequest fragment{
        {ShaderStage::Fragment, reinterpret_cast<std::uintptr_t>(pixelCode.data()), pixelCode, 0, {}},
        {64u, 0, pixelUserData, std::nullopt, pixel, std::nullopt, pixelMemory},
        target,
        PixelPushLayout(vertexPush, target)
    };
    fragment.useCache = false;
    const auto pixelResult = ShaderRecompiler::Recompile(fragment);
    exits = CountOps(pixelResult.spirv.Words(), spv::OpTerminateInvocation) + CountOps(pixelResult.spirv.Words(), spv::OpKill);
    const std::array<AgcDriver::Graphics::CompiledShader, 2> shaders{{
        {ShaderStage::Vertex, &vertexResult, 0},
        {ShaderStage::Fragment, &pixelResult, PixelPushOffset(vertexPush, target)}
    }};

    AgcDriver::Graphics::State state{};
    state.stages = {AgcDriver::Graphics::ShaderPath::Vertex, 0u, 64u, 64u, std::nullopt, std::nullopt};
    state.color = {reinterpret_cast<std::uintptr_t>(Pixels.data()), {Width, Height}, VK_FORMAT_R8G8B8A8_UNORM, Pixels.size(), 0xe4u};
    state.colors = {state.color};
    state.hasColorTarget = true;
    state.renderExtent = {Width, Height};
    state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    state.viewport = {0, static_cast<float>(Height), static_cast<float>(Width), -static_cast<float>(Height), 0, 1};
    state.negativeOneToOne = false;
    state.scissor = {{0, 0}, {Width, Height}};
    state.cullMode = VK_CULL_MODE_NONE;
    state.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    state.blend.colorWriteMask = 15;
    state.blends = {state.blend};
    const AgcDriver::Pm4::DrawParameters draw{0, static_cast<std::uint32_t>(triangle.size()), 0, 1, 0, false};
    std::string failure;
    try {
        device.Draw(state, draw, shaders);
        device.WaitIdle();
    } catch (const std::exception& error) {
        failure = error.what();
    }
    device.WaitIdle();
    return failure;
}

void CheckPixels(std::byte expected, const std::string& what) {
    for (std::uint32_t index = 0; index < Width * Height * 4u; ++index) {
        Require(Pixels[index] == expected, what + ": byte " + std::to_string(index) + " is " + std::to_string(std::to_integer<unsigned>(Pixels[index])) + ", expected " + std::to_string(std::to_integer<unsigned>(expected)));
    }
}

}

int main() {
    try {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        GuestBlock guest;
        std::size_t exits = 0;
        const auto mapped = Draw(*device, guest.Address(), exits);
        Require(mapped.empty(), "pixel BDA stops: a pixel shader with " + std::to_string(Reads) + " chained reads of mapped memory failed: " + mapped);
        CheckPixels(std::byte{255}, "pixel BDA stops, " + std::to_string(Reads) + " chained reads of mapped memory");
        Require(exits <= 2u, "pixel BDA stops: the pixel module with " + std::to_string(Reads) + " chained reads ends the invocation at " + std::to_string(exits) + " places, not through one BDA exit and the valid-mask kill");
        const auto unmapped = Draw(*device, guest.Address() + GuestBytes, exits);
        Require(unmapped.find("BDA access failed") != std::string::npos && unmapped.find("reason=1") != std::string::npos, "pixel BDA stops: a read of unmapped memory reported \"" + unmapped + "\", not an unmapped read");
        CheckPixels(std::byte{0x40}, "pixel BDA stops, a stopped read of unmapped memory");
        std::printf("pixel BDA stop tests passed: %u chained reads, %zu invocation exits in the pixel module\n", Reads, exits);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
