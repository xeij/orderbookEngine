#pragma once

// Branch hints. C++20 has [[likely]]/[[unlikely]] but they only apply to
// statements; these macros let us decorate expressions as well.
#if defined(__GNUC__) || defined(__clang__)
#define LOB_LIKELY(x)   __builtin_expect(!!(x), 1)
#define LOB_UNLIKELY(x) __builtin_expect(!!(x), 0)
#define LOB_ALWAYS_INLINE [[gnu::always_inline]] inline
#define LOB_HOT [[gnu::hot]]
#define LOB_COLD [[gnu::cold]]
#define LOB_FLATTEN [[gnu::flatten]]
#define LOB_PREFETCH(ptr) __builtin_prefetch(ptr)
#define LOB_RESTRICT __restrict__
#else
#define LOB_LIKELY(x)   (x)
#define LOB_UNLIKELY(x) (x)
#define LOB_ALWAYS_INLINE inline
#define LOB_HOT
#define LOB_COLD
#define LOB_FLATTEN
#define LOB_PREFETCH(ptr) ((void)0)
#define LOB_RESTRICT
#endif

#if defined(__x86_64__) || defined(_M_X64)
#define LOB_ARCH_X86_64 1
#endif

namespace lob {

inline constexpr std::size_t kCacheLine = 64;

}  // namespace lob
