// Small timing counters for the performance display and the debug report (mobile apps).
#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>

namespace WC09Perf
{
inline std::atomic<uint64_t> RenderUs{0};    // time spent in the 2D/3D renderers (inside the emulated frame)
inline std::atomic<uint64_t> Render3DUs{0};
inline std::atomic<int> Hd2DCount{0}, Hd3DCount{0};
inline std::atomic<uint64_t> HdUploads{0};   // HD pictures turned into textures
inline thread_local int Depth = 0;

struct Scope
{
    std::atomic<uint64_t>* Extra;
    std::chrono::steady_clock::time_point T0;
    bool Outer;
    explicit Scope(std::atomic<uint64_t>* extra = nullptr) : Extra(extra), T0(std::chrono::steady_clock::now()), Outer(Depth++ == 0) {}
    ~Scope()
    {
        Depth--;
        uint64_t us = (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - T0).count();
        if (Outer) RenderUs += us;
        if (Extra) *Extra += us;
    }
};
}
