#pragma once

#include <chrono>
#include <cstdint>

#include "lob/compiler.hpp"

#if defined(LOB_ARCH_X86_64)
#include <x86intrin.h>
#endif

namespace lob {

// rdtscp returns a 64-bit cycle counter and serialises previous instructions.
// On modern Intel/AMD parts this is invariant TSC: the count advances at a
// fixed frequency regardless of CPU C-state, which is what we want for
// latency measurement. We deliberately do not pair it with CPUID -- the
// serialisation cost dwarfs the operations we are timing.
LOB_ALWAYS_INLINE std::uint64_t rdtsc() noexcept {
#if defined(LOB_ARCH_X86_64)
    unsigned aux;
    return __rdtscp(&aux);
#else
    return static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
#endif
}

// Steady wall-clock in nanoseconds; for histogram bucketing it is usually
// more practical than raw TSC ticks because we don't have to discover the
// nominal frequency.
LOB_ALWAYS_INLINE std::uint64_t now_ns() noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
}

}  // namespace lob
