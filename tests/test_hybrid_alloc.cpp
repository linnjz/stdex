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

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <numeric>
#include <vector>

#include "hybrid_alloc.hpp"

namespace
{

int g_tests_run    = 0;
int g_tests_passed = 0;

#define TEST_ALLOC(name)           \
  do {                             \
    ++g_tests_run;                 \
    std::printf("  %-50s", #name); \
  } while (0)

#define PASS()             \
  do {                     \
    ++g_tests_passed;      \
    std::printf("PASS\n"); \
  } while (0)

#define FAIL_MSG(msg)               \
  do {                              \
    std::printf("FAIL: %s\n", msg); \
  } while (0)

} // namespace

// ============================================================================
// Basic HYBRID_ALLOC tests
// ============================================================================

static void test_hybrid_alloc_small_stack()
{
  TEST_ALLOC(HybridAllocSmallStack);
  // Small allocation should use stack (default threshold = 1024)
  void *ptr = HYBRID_ALLOC(128);
  assert(ptr != nullptr);
  // Write and read back
  std::memset(ptr, 0xAB, 128);
  for (size_t i = 0; i < 128; ++i) {
    assert(static_cast<unsigned char *>(ptr)[i] == 0xAB);
  }
  stdex::hybrid_free(ptr);
  PASS();
}

static void test_hybrid_alloc_large_heap()
{
  TEST_ALLOC(HybridAllocLargeHeap);
  // Large allocation should use heap
  void *ptr = HYBRID_ALLOC(4096);
  assert(ptr != nullptr);
  // Write and read back
  std::memset(ptr, 0xCD, 4096);
  for (size_t i = 0; i < 4096; ++i) {
    assert(static_cast<unsigned char *>(ptr)[i] == 0xCD);
  }
  stdex::hybrid_free(ptr);
  PASS();
}

static void test_hybrid_alloc_threshold_boundary()
{
  TEST_ALLOC(HybridAllocThresholdBoundary);
  // Test at exact threshold
  constexpr size_t threshold = 256;
  void *ptr1                 = HYBRID_ALLOC_THRESHOLD(threshold, threshold);
  assert(ptr1 != nullptr);
  std::memset(ptr1, 0x11, threshold);

  // Just above threshold
  void *ptr2 = HYBRID_ALLOC_THRESHOLD(threshold + 1, threshold);
  assert(ptr2 != nullptr);
  std::memset(ptr2, 0x22, threshold + 1);

  // Just below threshold
  void *ptr3 = HYBRID_ALLOC_THRESHOLD(threshold - 1, threshold);
  assert(ptr3 != nullptr);
  std::memset(ptr3, 0x33, threshold - 1);

  stdex::hybrid_free(ptr1);
  stdex::hybrid_free(ptr2);
  stdex::hybrid_free(ptr3);
  PASS();
}

// ============================================================================
// HYBRID_ALIGNED_ALLOC tests
// ============================================================================

static void test_hybrid_aligned_alloc_small()
{
  TEST_ALLOC(HybridAlignedAllocSmall);
  void *ptr = HYBRID_ALIGNED_ALLOC(128, 64);
  assert(ptr != nullptr);
  // Check alignment
  assert((reinterpret_cast<uintptr_t>(ptr) % 64) == 0);
  std::memset(ptr, 0xBB, 128);
  stdex::hybrid_free(ptr);
  PASS();
}

static void test_hybrid_aligned_alloc_large()
{
  TEST_ALLOC(HybridAlignedAllocLarge);
  void *ptr = HYBRID_ALIGNED_ALLOC(4096, 128);
  assert(ptr != nullptr);
  assert((reinterpret_cast<uintptr_t>(ptr) % 128) == 0);
  std::memset(ptr, 0xDD, 4096);
  stdex::hybrid_free(ptr);
  PASS();
}

static void test_hybrid_aligned_alloc_threshold()
{
  TEST_ALLOC(HybridAlignedAllocThreshold);
  constexpr size_t threshold = 512;
  // Stack path
  void *ptr1 = HYBRID_ALIGNED_ALLOC_THRESHOLD(256, 64, threshold);
  assert(ptr1 != nullptr);
  assert((reinterpret_cast<uintptr_t>(ptr1) % 64) == 0);
  std::memset(ptr1, 0xAA, 256);

  // Heap path
  void *ptr2 = HYBRID_ALIGNED_ALLOC_THRESHOLD(1024, 64, threshold);
  assert(ptr2 != nullptr);
  assert((reinterpret_cast<uintptr_t>(ptr2) % 64) == 0);
  std::memset(ptr2, 0xBB, 1024);

  stdex::hybrid_free(ptr1);
  stdex::hybrid_free(ptr2);
  PASS();
}

// ============================================================================
// hybrid_get_total_size tests
// ============================================================================

static void test_hybrid_get_total_size()
{
  TEST_ALLOC(HybridGetTotalSize);
  {
    void *ptr = HYBRID_ALLOC(128);
    assert(ptr != nullptr);
    size_t total = stdex::hybrid_get_total_size(ptr);
    assert(total >= 128);
    stdex::hybrid_free(ptr);
  }
  {
    void *ptr = HYBRID_ALLOC(4096);
    assert(ptr != nullptr);
    size_t total = stdex::hybrid_get_total_size(ptr);
    assert(total >= 4096);
    stdex::hybrid_free(ptr);
  }
  // nullptr
  assert(stdex::hybrid_get_total_size(nullptr) == 0);
  PASS();
}

// ============================================================================
// hybrid_mark_persistent tests
// ============================================================================

static void test_hybrid_mark_persistent_from_stack()
{
  TEST_ALLOC(HybridMarkPersistentFromStack);
  void *ptr = HYBRID_ALLOC(128);
  assert(ptr != nullptr);
  std::memset(ptr, 0xEE, 128);

  // Mark as persistent (should copy to heap)
  void *persistent = stdex::hybrid_mark_persistent(ptr, 128);
  assert(persistent != nullptr);
  assert(persistent != ptr); // different pointer (heap copy)

  // Verify data copied correctly
  for (size_t i = 0; i < 128; ++i) {
    assert(static_cast<unsigned char *>(persistent)[i] == 0xEE);
  }

  // Free the persistent copy
  stdex::hybrid_free(persistent);
  // Original stack allocation can still be freed (no-op for stack)
  stdex::hybrid_free(ptr);
  PASS();
}

static void test_hybrid_mark_persistent_from_heap()
{
  TEST_ALLOC(HybridMarkPersistentFromHeap);
  void *ptr = HYBRID_ALLOC(4096);
  assert(ptr != nullptr);
  std::memset(ptr, 0xFF, 4096);

  // Mark as persistent (should return same pointer for heap)
  void *persistent = stdex::hybrid_mark_persistent(ptr, 4096);
  assert(persistent == ptr); // same pointer (already heap)

  stdex::hybrid_free(persistent);
  PASS();
}

// ============================================================================
// Multiple allocations and frees
// ============================================================================

static void test_multiple_allocs_frees()
{
  TEST_ALLOC(MultipleAllocsFrees);
  constexpr int N = 100;
  void *ptrs[N];

  for (int i = 0; i < N; ++i) {
    size_t size = static_cast<size_t>(i + 1) * 16;
    ptrs[i]     = HYBRID_ALLOC(size);
    assert(ptrs[i] != nullptr);
    std::memset(ptrs[i], static_cast<int>(i & 0xFF), size);
  }

  for (int i = 0; i < N; ++i) {
    size_t size = static_cast<size_t>(i + 1) * 16;
    for (size_t j = 0; j < size; ++j) {
      assert(static_cast<unsigned char *>(ptrs[i])[j] == static_cast<unsigned char>(i & 0xFF));
    }
    stdex::hybrid_free(ptrs[i]);
  }
  PASS();
}

// ============================================================================
// Data integrity under pattern
// ============================================================================

static void test_data_integrity_patterns()
{
  TEST_ALLOC(DataIntegrityPatterns);
  // Alternating pattern
  {
    void *ptr = HYBRID_ALLOC(256);
    assert(ptr != nullptr);
    for (size_t i = 0; i < 256; ++i) {
      static_cast<unsigned char *>(ptr)[i] = static_cast<unsigned char>((i % 2 == 0) ? 0xAA : 0x55);
    }
    for (size_t i = 0; i < 256; ++i) {
      unsigned char expected = static_cast<unsigned char>((i % 2 == 0) ? 0xAA : 0x55);
      assert(static_cast<unsigned char *>(ptr)[i] == expected);
    }
    stdex::hybrid_free(ptr);
  }
  // Incrementing pattern
  {
    void *ptr = HYBRID_ALLOC(512);
    assert(ptr != nullptr);
    for (size_t i = 0; i < 512; ++i) {
      static_cast<unsigned char *>(ptr)[i] = static_cast<unsigned char>(i & 0xFF);
    }
    for (size_t i = 0; i < 512; ++i) {
      assert(static_cast<unsigned char *>(ptr)[i] == static_cast<unsigned char>(i & 0xFF));
    }
    stdex::hybrid_free(ptr);
  }
  PASS();
}

// ============================================================================
// Alignment verification for various alignments
// ============================================================================

static void test_various_alignments()
{
  TEST_ALLOC(VariousAlignments);
  size_t alignments[] = { 1, 2, 4, 8, 16, 32, 64, 128, 256, 512 };
  for (size_t align : alignments) {
    void *ptr = HYBRID_ALIGNED_ALLOC(128, align);
    assert(ptr != nullptr);
    assert((reinterpret_cast<uintptr_t>(ptr) % align) == 0);
    stdex::hybrid_free(ptr);
  }
  PASS();
}

// ============================================================================
// Large aligned allocation
// ============================================================================

static void test_large_aligned()
{
  TEST_ALLOC(LargeAligned);
  void *ptr = HYBRID_ALIGNED_ALLOC(16384, 256);
  assert(ptr != nullptr);
  assert((reinterpret_cast<uintptr_t>(ptr) % 256) == 0);
  std::memset(ptr, 0xCC, 16384);
  stdex::hybrid_free(ptr);
  PASS();
}

// ============================================================================
// Nested allocations (stack within stack, heap within heap)
// ============================================================================

static void test_nested_allocations()
{
  TEST_ALLOC(NestedAllocations);
  void *outer = HYBRID_ALLOC(256);
  assert(outer != nullptr);
  std::memset(outer, 0x11, 256);

  // Inner allocation (should work independently)
  void *inner = HYBRID_ALLOC(128);
  assert(inner != nullptr);
  std::memset(inner, 0x22, 128);

  // Verify outer data unchanged
  for (size_t i = 0; i < 256; ++i) {
    assert(static_cast<unsigned char *>(outer)[i] == 0x11);
  }
  // Verify inner data
  for (size_t i = 0; i < 128; ++i) {
    assert(static_cast<unsigned char *>(inner)[i] == 0x22);
  }

  stdex::hybrid_free(inner);
  stdex::hybrid_free(outer);
  PASS();
}

// ============================================================================
// Zero-size allocation
// ============================================================================

static void test_zero_size()
{
  TEST_ALLOC(ZeroSize);
  // Zero size should still return a valid pointer (or nullptr)
  // The implementation uses _alloca/malloc which may return nullptr for 0
  void *ptr = HYBRID_ALLOC(0);
  // Don't dereference, just test that free doesn't crash
  stdex::hybrid_free(ptr);
  PASS();
}

// ============================================================================
// Threshold at 1 (always heap except 0)
// ============================================================================

static void test_threshold_one()
{
  TEST_ALLOC(ThresholdOne);
  void *ptr = HYBRID_ALLOC_THRESHOLD(1, 1);
  assert(ptr != nullptr);
  static_cast<unsigned char *>(ptr)[0] = 0xFF;
  assert(static_cast<unsigned char *>(ptr)[0] == 0xFF);
  stdex::hybrid_free(ptr);
  PASS();
}

// ============================================================================
// Persistence of data after multiple operations
// ============================================================================

static void test_data_persistence()
{
  TEST_ALLOC(DataPersistence);
  void *ptr = HYBRID_ALLOC(1024);
  assert(ptr != nullptr);

  // Fill with data
  for (size_t i = 0; i < 1024; ++i) {
    static_cast<unsigned char *>(ptr)[i] = static_cast<unsigned char>(i & 0xFF);
  }

  // Do some other allocations
  void *temp1 = HYBRID_ALLOC(512);
  void *temp2 = HYBRID_ALLOC(2048);
  stdex::hybrid_free(temp1);
  stdex::hybrid_free(temp2);

  // Verify original data unchanged
  for (size_t i = 0; i < 1024; ++i) {
    assert(static_cast<unsigned char *>(ptr)[i] == static_cast<unsigned char>(i & 0xFF));
  }

  stdex::hybrid_free(ptr);
  PASS();
}

// ============================================================================
// main
// ============================================================================
int main()
{
  std::printf("=== hybrid_alloc tests ===\n\n");

  test_hybrid_alloc_small_stack();
  test_hybrid_alloc_large_heap();
  test_hybrid_alloc_threshold_boundary();
  test_hybrid_aligned_alloc_small();
  test_hybrid_aligned_alloc_large();
  test_hybrid_aligned_alloc_threshold();
  test_hybrid_get_total_size();
  test_hybrid_mark_persistent_from_stack();
  test_hybrid_mark_persistent_from_heap();
  test_multiple_allocs_frees();
  test_data_integrity_patterns();
  test_various_alignments();
  test_large_aligned();
  test_nested_allocations();
  test_zero_size();
  test_threshold_one();
  test_data_persistence();

  std::printf("\n=== Results: %d/%d passed ===\n", g_tests_passed, g_tests_run);
  return g_tests_passed == g_tests_run ? 0 : 1;
}
