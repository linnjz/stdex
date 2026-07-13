/*
 * Copyright (c) 2026 LinnJz
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

// Design ref: Windows Kits 10 UCRT `ucrt/malloc.h` `_malloca`/`_freea`.
// Threshold, marker-ahead-of-pointer dispatch, and size computation mirror
// `_ALLOCA_S_THRESHOLD`, `_MarkAllocaS`, and `_MallocaComputeSize`.

#include <cassert> // assert
#include <cstddef> // size_t、std::max_align_t
#include <cstdint> // uint32_t、uintptr_t
#include <cstdlib> // std::malloc、std::free
#include <cstring> // std::memcpy
#include <limits>  // std::numeric_limits

#if defined(_MSC_VER)
#  include <malloc.h> // _alloc
#endif

#ifndef HYBRID_ALLOC_DEFAULT_THRESHOLD
#  define HYBRID_ALLOC_DEFAULT_THRESHOLD 1024
#endif

#ifndef HYBRID_ALLOC_MAX_STACK_ALIGNMENT
#  define HYBRID_ALLOC_MAX_STACK_ALIGNMENT 4096
#endif

#ifndef HYBRID_ALLOC_DOUBLE_FREE_CHECK
#  define HYBRID_ALLOC_DOUBLE_FREE_CHECK 1
#endif

namespace stdex
{

namespace hybrid_alloc_detail
{
inline constexpr size_t kHybridMaxAlign = alignof(std::max_align_t);

struct alignas(kHybridMaxAlign) hybrid_alloc_header
{
  uint32_t marker;   // origin and state of this allocation
  void *raw_ptr;     // base pointer used for free (may differ from user ptr)
  size_t alloc_size; // total allocated size
  size_t alignment;  // alignment used for this allocation
};

inline constexpr size_t kHybridAllocHeaderSize = sizeof(hybrid_alloc_header);

static_assert(kHybridAllocHeaderSize % kHybridMaxAlign == 0, "hybrid_alloc_header size must be multiple of max_align_t");

/// Marker values distinguish stack vs heap origin and detect double-frees.
inline constexpr uint32_t kAllocStackMarker        = 0xCC'CC;
inline constexpr uint32_t kAllocHeapMarker         = 0xDD'DD;
inline constexpr uint32_t kAlignedAllocStackMarker = 0xEE'EE;
inline constexpr uint32_t kAlignedAllocHeapMarker  = 0xFF'FF;
inline constexpr uint32_t kAllocFreedHeapMarker    = 0xDE'AD;
inline constexpr uint32_t kAllocFreedStackMarker   = 0xBE'EF;

inline constexpr bool is_stack_marker(uint32_t marker) noexcept
{
  return marker == kAllocStackMarker || marker == kAlignedAllocStackMarker;
}

inline constexpr bool is_heap_marker(uint32_t marker) noexcept
{
  return marker == kAllocHeapMarker || marker == kAlignedAllocHeapMarker;
}

inline constexpr bool is_freed_marker(uint32_t marker) noexcept
{
  return marker == kAllocFreedHeapMarker || marker == kAllocFreedStackMarker;
}

// Raises sub-max_align_t alignments up to kHybridMaxAlign so that the header
// placement logic stays uniform.
inline constexpr size_t normalize_alignment(size_t alignment) noexcept
{
  return alignment < kHybridMaxAlign ? kHybridMaxAlign : alignment;
}

inline constexpr bool is_valid_alignment(size_t alignment) noexcept
{
  return alignment && !(alignment & (alignment - 1));
}

/// Computes the total bytes needed for a hybrid allocation.
///
/// Includes the user payload, header, and enough padding to satisfy alignment.
/// Returns false for invalid alignment or overflow.
inline constexpr bool total_size(size_t size, size_t alignment, size_t *total) noexcept
{
  if (!is_valid_alignment(alignment)) return false;

  alignment = normalize_alignment(alignment);

  size_t const padding = alignment - 1;
  if (size > std::numeric_limits<size_t>::max() - kHybridAllocHeaderSize - padding) {
    return false;
  }

  *total = size + kHybridAllocHeaderSize + padding;
  return true;
}

// Like total_size but returns 0 on overflow.
inline constexpr size_t total_size_or_zero(size_t size, size_t alignment) noexcept
{
  size_t total = 0;
  total_size(size, alignment, &total);
  return total;
}

// Whether the request fits the stack threshold after accounting for overhead.
inline bool is_use_stack(size_t size, size_t threshold, size_t alignment) noexcept
{
  size_t total;
  return size <= threshold && total_size(size, alignment, &total);
}

// Debug assertion that the requested alignment is within the stack limit.
inline void check_stack_alignment(size_t alignment) noexcept
{
  assert(normalize_alignment(alignment) <= HYBRID_ALLOC_MAX_STACK_ALIGNMENT && "hybrid allocation stack alignment is too large");
}

/// Initializes a raw allocation and returns the aligned user pointer.
///
/// Writes the header immediately before the returned pointer. `free_ptr` is
/// null for stack storage and the heap base pointer otherwise.
inline void *init_hybrid_alloc(
    void *raw,
    size_t alignment,
    uint32_t marker,
    void *free_ptr,
    size_t alloc_size) noexcept
{
  if (raw == nullptr) return nullptr;

  size_t const normalized_alignment = normalize_alignment(alignment);
  assert(is_valid_alignment(alignment));

  auto *base = static_cast<unsigned char *>(raw) + kHybridAllocHeaderSize;
  // NOLINTNEXTLINE(performance-no-int-to-ptr)
  auto *user = reinterpret_cast<unsigned char *>(
      (reinterpret_cast<uintptr_t>(base) + normalized_alignment - 1) &
      ~(normalized_alignment - 1));
  auto *header       = reinterpret_cast<hybrid_alloc_header *>(user - kHybridAllocHeaderSize);
  header->marker     = marker;
  header->raw_ptr    = free_ptr;
  header->alloc_size = alloc_size;
  header->alignment  = normalized_alignment;
  return user;
}

// Wrapper for the common stack-allocation path; raw_ptr is set to nullptr.
inline void *init_stack_alloc(void *raw, size_t alignment, uint32_t marker, size_t alloc_size) noexcept
{
  return init_hybrid_alloc(raw, alignment, marker, nullptr, alloc_size);
}

// Allocates total bytes via malloc and initialises the header.
inline void *init_heap_alloc_with_total(size_t total, size_t alignment, uint32_t marker) noexcept
{
  void *raw = std::malloc(total);
  return init_hybrid_alloc(raw, alignment, marker, raw, total);
}

// Computes total size, then allocates on the heap.
inline void *init_heap_alloc(size_t size, size_t alignment, uint32_t marker) noexcept
{
  size_t total;
  if (!total_size(size, alignment, &total)) return nullptr;

  return init_heap_alloc_with_total(total, alignment, marker);
}

// Retrieve the header preceding a user pointer.
inline hybrid_alloc_header *get_hybrid_alloc_header(void *ptr) noexcept
{
  return reinterpret_cast<hybrid_alloc_header *>(
      static_cast<unsigned char *>(ptr) - kHybridAllocHeaderSize);
}

inline hybrid_alloc_header const *get_hybrid_alloc_header(void const *ptr) noexcept
{
  return reinterpret_cast<hybrid_alloc_header const *>(
      static_cast<unsigned char const *>(ptr) - kHybridAllocHeaderSize);
}

} // namespace hybrid_alloc_detail

/// Frees memory allocated by the HYBRID_ALLOC* macros.
///
/// Stack allocations are a no-op; heap allocations are released with
/// std::free(). Double-free checking is enabled in debug builds.
inline void hybrid_free(void *ptr) noexcept
{
  if (ptr == nullptr) return;

  auto *header      = hybrid_alloc_detail::get_hybrid_alloc_header(ptr);
  auto const marker = header->marker;
#if HYBRID_ALLOC_DOUBLE_FREE_CHECK
  assert(!hybrid_alloc_detail::is_freed_marker(marker) && "hybrid allocation was already freed");
#endif

  if (hybrid_alloc_detail::is_heap_marker(marker)) {
#if HYBRID_ALLOC_DOUBLE_FREE_CHECK
    header->marker = hybrid_alloc_detail::kAllocFreedHeapMarker;
#endif
    std::free(header->raw_ptr);
    return;
  }

  assert(hybrid_alloc_detail::is_stack_marker(marker) && "invalid hybrid allocation marker");
#if HYBRID_ALLOC_DOUBLE_FREE_CHECK
  header->marker = hybrid_alloc_detail::kAllocFreedStackMarker;
#endif
}

/// Returns the recorded allocation size, including header and padding.
inline size_t hybrid_get_total_size(void const *ptr) noexcept
{
  if (ptr == nullptr) return 0;
  return hybrid_alloc_detail::get_hybrid_alloc_header(ptr)->alloc_size;
}

/// Returns a heap-backed copy of a hybrid allocation.
///
/// Heap allocations are returned unchanged. Stack allocations are copied to a
/// new heap buffer; the original stack storage is left in place.
inline void *hybrid_mark_persistent(void *ptr, size_t size) noexcept
{
  if (ptr == nullptr) return nullptr;

  auto *header      = hybrid_alloc_detail::get_hybrid_alloc_header(ptr);
  auto const marker = header->marker;
#if HYBRID_ALLOC_DOUBLE_FREE_CHECK
  assert(!hybrid_alloc_detail::is_freed_marker(marker) && "hybrid allocation was already freed");
#endif

  if (hybrid_alloc_detail::is_heap_marker(marker)) {
    return ptr;
  }

  assert(hybrid_alloc_detail::is_stack_marker(marker) && "invalid hybrid allocation marker");

  auto const heap_marker = marker == hybrid_alloc_detail::kAlignedAllocStackMarker
                             ? hybrid_alloc_detail::kAlignedAllocHeapMarker
                             : hybrid_alloc_detail::kAllocHeapMarker;

  void *persistent = hybrid_alloc_detail::init_heap_alloc(size, header->alignment, heap_marker);
  if (persistent == nullptr) return nullptr;

  std::memcpy(persistent, ptr, size);
  return persistent;
}

} // namespace stdex

/// @def HYBRID_ALLOC_THRESHOLD(size, threshold)
/// Allocates bytes on the stack when `size <= threshold`, otherwise on heap.
///
/// The returned pointer is compatible with hybrid_free(). On MSVC, avoid
/// passing expressions with side effects.
#if defined(__GNUC__) || defined(__clang__)
#  define HYBRID_ALLOC_THRESHOLD(size, threshold)                                        \
    __extension__({                                                                      \
      size_t const stdex_hybrid_size      = (size);                                      \
      size_t const stdex_hybrid_threshold = (threshold);                                 \
      size_t const stdex_hybrid_alignment = stdex::hybrid_alloc_detail::kHybridMaxAlign; \
      size_t stdex_hybrid_total           = 0;                                           \
      void *stdex_hybrid_result           = nullptr;                                     \
      if (stdex::hybrid_alloc_detail::total_size(                                        \
              stdex_hybrid_size,                                                         \
              stdex_hybrid_alignment,                                                    \
              &stdex_hybrid_total)) {                                                    \
        if (stdex_hybrid_size <= stdex_hybrid_threshold) {                               \
          stdex::hybrid_alloc_detail::check_stack_alignment(stdex_hybrid_alignment);     \
          stdex_hybrid_result = stdex::hybrid_alloc_detail::init_stack_alloc(            \
              __builtin_alloca(stdex_hybrid_total),                                      \
              stdex_hybrid_alignment,                                                    \
              stdex::hybrid_alloc_detail::kAllocStackMarker,                             \
              stdex_hybrid_total);                                                       \
        } else {                                                                         \
          stdex_hybrid_result = stdex::hybrid_alloc_detail::init_heap_alloc_with_total(  \
              stdex_hybrid_total,                                                        \
              stdex_hybrid_alignment,                                                    \
              stdex::hybrid_alloc_detail::kAllocHeapMarker);                             \
        }                                                                                \
      }                                                                                  \
      stdex_hybrid_result;                                                               \
    })
#elif defined(_MSC_VER)
#  define HYBRID_ALLOC_THRESHOLD(size, threshold)                              \
    (stdex::hybrid_alloc_detail::is_use_stack(                                 \
         (size), (threshold), stdex::hybrid_alloc_detail::kHybridMaxAlign)     \
         ? (stdex::hybrid_alloc_detail::check_stack_alignment(                 \
                stdex::hybrid_alloc_detail::kHybridMaxAlign),                  \
            stdex::hybrid_alloc_detail::init_stack_alloc(                      \
                _alloca(                                                       \
                    stdex::hybrid_alloc_detail::total_size_or_zero(            \
                        (size), stdex::hybrid_alloc_detail::kHybridMaxAlign)), \
                stdex::hybrid_alloc_detail::kHybridMaxAlign,                   \
                stdex::hybrid_alloc_detail::kAllocStackMarker,                 \
                stdex::hybrid_alloc_detail::total_size_or_zero(                \
                    (size), stdex::hybrid_alloc_detail::kHybridMaxAlign)))     \
         : stdex::hybrid_alloc_detail::init_heap_alloc(                        \
               (size), stdex::hybrid_alloc_detail::kHybridMaxAlign,            \
               stdex::hybrid_alloc_detail::kAllocHeapMarker))
#else
#  error "HYBRID_ALLOC_THRESHOLD is not available for this compiler"
#endif

/// Convenience wrapper using the default hybrid allocation threshold.
#define HYBRID_ALLOC(size) HYBRID_ALLOC_THRESHOLD(size, HYBRID_ALLOC_DEFAULT_THRESHOLD)

/// @def HYBRID_ALIGNED_ALLOC_THRESHOLD(size, alignment, threshold)
/// Like HYBRID_ALLOC_THRESHOLD, with an explicit alignment.
///
/// Stack alignment is capped at HYBRID_ALLOC_MAX_STACK_ALIGNMENT.
#if defined(__GNUC__) || defined(__clang__)
#  define HYBRID_ALIGNED_ALLOC_THRESHOLD(size, alignment, threshold)                    \
    __extension__({                                                                     \
      size_t const stdex_hybrid_size      = (size);                                     \
      size_t const stdex_hybrid_threshold = (threshold);                                \
      size_t const stdex_hybrid_alignment = (alignment);                                \
      size_t stdex_hybrid_total           = 0;                                          \
      void *stdex_hybrid_result           = nullptr;                                    \
      if (stdex::hybrid_alloc_detail::total_size(                                       \
              stdex_hybrid_size,                                                        \
              stdex_hybrid_alignment,                                                   \
              &stdex_hybrid_total)) {                                                   \
        if (stdex_hybrid_size <= stdex_hybrid_threshold) {                              \
          stdex::hybrid_alloc_detail::check_stack_alignment(stdex_hybrid_alignment);    \
          stdex_hybrid_result = stdex::hybrid_alloc_detail::init_stack_alloc(           \
              __builtin_alloca(stdex_hybrid_total),                                     \
              stdex_hybrid_alignment,                                                   \
              stdex::hybrid_alloc_detail::kAlignedAllocStackMarker,                     \
              stdex_hybrid_total);                                                      \
        } else {                                                                        \
          stdex_hybrid_result = stdex::hybrid_alloc_detail::init_heap_alloc_with_total( \
              stdex_hybrid_total,                                                       \
              stdex_hybrid_alignment,                                                   \
              stdex::hybrid_alloc_detail::kAlignedAllocHeapMarker);                     \
        }                                                                               \
      }                                                                                 \
      stdex_hybrid_result;                                                              \
    })
#elif defined(_MSC_VER)
#  define HYBRID_ALIGNED_ALLOC_THRESHOLD(size, alignment, threshold)                \
    (stdex::hybrid_alloc_detail::is_use_stack((size), (threshold), (alignment))     \
         ? stdex::hybrid_alloc_detail::init_stack_alloc(                            \
               (stdex::hybrid_alloc_detail::check_stack_alignment((alignment)),     \
                _alloca(                                                            \
                    stdex::hybrid_alloc_detail::total_size_or_zero(                 \
                        (size), (alignment)))),                                     \
               (alignment),                                                         \
               stdex::hybrid_alloc_detail::kAlignedAllocStackMarker,                \
               stdex::hybrid_alloc_detail::total_size_or_zero((size), (alignment))) \
         : stdex::hybrid_alloc_detail::init_heap_alloc(                             \
               (size), (alignment), stdex::hybrid_alloc_detail::kAlignedAllocHeapMarker))
#else
#  error "HYBRID_ALIGNED_ALLOC_THRESHOLD is not available for this compiler"
#endif

/// Convenience wrapper for aligned allocation with the default threshold.
#define HYBRID_ALIGNED_ALLOC(size, alignment) HYBRID_ALIGNED_ALLOC_THRESHOLD(size, alignment, HYBRID_ALLOC_DEFAULT_THRESHOLD)
