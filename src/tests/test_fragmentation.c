// test_fragmentation.c - Fragmentation Test Suite
// Tests: TC-FRAG-*

#include "allocator.h"
#include "test_harness.h"
#include <stdint.h>
#include <string.h>

// Helper: Get RSS from /proc/self/statm

static size_t get_rss_bytes(void) {
  FILE *f = fopen("/proc/self/statm", "r");
  if (!f)
    return 0;

  size_t size, resident;
  if (fscanf(f, "%zu %zu", &size, &resident) != 2) {
    fclose(f);
    return 0;
  }
  fclose(f);

  return resident * 4096;
}

// TC-FRAG-001: Swiss cheese pattern

static test_result_t test_frag_001(allocator_t *alloc) {
  const size_t num_blocks = 10000;
  const size_t block_size = 256;
  void **ptrs = alloc->malloc(num_blocks * sizeof(void *));
  TEST_ASSERT_NOT_NULL(ptrs, "meta allocation");

  fprintf(stderr, "\r    Allocating %zu blocks...", num_blocks);
  for (size_t i = 0; i < num_blocks; i++) {
    ptrs[i] = alloc->malloc(block_size);
    TEST_ASSERT_NOT_NULL(ptrs[i], "initial allocation");
  }

  fprintf(stderr, "\r    Creating swiss cheese pattern...");
  for (size_t i = 0; i < num_blocks; i += 2) {
    alloc->free(ptrs[i]);
    ptrs[i] = NULL;
  }

  fprintf(stderr, "\r    Attempting large allocation in fragmented heap...");
  size_t large_size = block_size * 100; // 25KB
  void *large = alloc->malloc(large_size);

  TEST_ASSERT_NOT_NULL(large,
                       "large allocation in fragmented heap should succeed");

  alloc->free(large);
  for (size_t i = 0; i < num_blocks; i++) {
    if (ptrs[i])
      alloc->free(ptrs[i]);
  }
  alloc->free(ptrs);

  fprintf(stderr, "\r    Swiss cheese pattern test passed            \n");
  return TEST_PASS;
}

// TC-FRAG-002: Sawtooth pattern

static test_result_t test_frag_002(allocator_t *alloc) {
  const size_t peak_allocs = 5000;
  const size_t alloc_size = 1024;
  void **ptrs = alloc->malloc(peak_allocs * sizeof(void *));
  TEST_ASSERT_NOT_NULL(ptrs, "meta allocation");

  size_t baseline_rss = get_rss_bytes();
  size_t max_rss = baseline_rss;

  for (size_t cycle = 0; cycle < 10; cycle++) {
    for (size_t i = 0; i < peak_allocs; i++) {
      ptrs[i] = alloc->malloc(alloc_size);
      TEST_ASSERT_NOT_NULL(ptrs[i], "peak allocation");
    }

    size_t peak_rss = get_rss_bytes();
    if (peak_rss > max_rss)
      max_rss = peak_rss;

    for (size_t i = 0; i < peak_allocs; i++) {
      alloc->free(ptrs[i]);
    }

    size_t post_free_rss = get_rss_bytes();

    fprintf(stderr, "\r    Cycle %zu: peak RSS=%zu KB, post-free RSS=%zu KB",
            cycle, peak_rss / 1024, post_free_rss / 1024);
  }

  size_t final_rss = get_rss_bytes();
  alloc->free(ptrs);

  fprintf(stderr, "\n    Baseline: %zu KB, Final: %zu KB, Max: %zu KB\n",
          baseline_rss / 1024, final_rss / 1024, max_rss / 1024);

  return TEST_PASS;
}

// TC-FRAG-003: Size class thrashing

static test_result_t test_frag_003(allocator_t *alloc) {
  const size_t iterations = 100000;
  size_t sizes[] = {16, 32, 64, 128, 256, 512, 1024, 2048};
  const size_t num_sizes = sizeof(sizes) / sizeof(sizes[0]);

  void *live[8][100];
  memset(live, 0, sizeof(live));

  size_t baseline_rss = get_rss_bytes();
  test_rng_t rng;
  rng_seed(&rng, 0xABCDEF01);

  for (size_t i = 0; i < iterations; i++) {
    size_t class_idx = rng_next(&rng) % num_sizes;
    size_t slot = rng_next(&rng) % 100;

    if (live[class_idx][slot]) {
      alloc->free(live[class_idx][slot]);
    }

    live[class_idx][slot] = alloc->malloc(sizes[class_idx]);
    TEST_ASSERT_NOT_NULL(live[class_idx][slot], "size class allocation");

    if (i % 10000 == 0) {
      size_t current_rss = get_rss_bytes();
      fprintf(stderr, "\r    Progress: %zu/%zu, RSS=%zu KB", i, iterations,
              current_rss / 1024);
    }
  }

  for (size_t c = 0; c < num_sizes; c++) {
    for (size_t s = 0; s < 100; s++) {
      if (live[c][s])
        alloc->free(live[c][s]);
    }
  }

  size_t final_rss = get_rss_bytes();
  fprintf(stderr, "\n    Baseline: %zu KB, Final: %zu KB\n",
          baseline_rss / 1024, final_rss / 1024);

  TEST_ASSERT(final_rss < baseline_rss * 10, "RSS should not grow unbounded");

  return TEST_PASS;
}

// TC-FRAG-004: Long-running simulation

static test_result_t test_frag_004(allocator_t *alloc) {
  const size_t duration_ops = 500000;
  const size_t max_live = 10000;

  void **ptrs = alloc->malloc(max_live * sizeof(void *));
  size_t *sizes = alloc->malloc(max_live * sizeof(size_t));
  TEST_ASSERT_NOT_NULL(ptrs, "meta allocation");
  TEST_ASSERT_NOT_NULL(sizes, "size tracking allocation");

  memset(ptrs, 0, max_live * sizeof(void *));
  size_t live_count = 0;

  test_rng_t rng;
  rng_seed(&rng, 0x13579BDF);

  size_t baseline_rss = get_rss_bytes();
  size_t max_rss = baseline_rss;

  for (size_t op = 0; op < duration_ops; op++) {
    bool do_alloc = (live_count < 100) ||
                    (rng_next(&rng) % 100 < 60 && live_count < max_live);

    if (do_alloc) {
      size_t slot = rng_next(&rng) % max_live;
      size_t attempts = 0;
      while (ptrs[slot] != NULL && attempts < 100) {
        slot = (slot + 1) % max_live;
        attempts++;
      }
      if (ptrs[slot] == NULL) {
        // Realistic size distribution (power-law-ish)
        size_t size;
        size_t r = rng_next(&rng) % 100;
        if (r < 70)
          size = rng_range(&rng, 16, 256); // 70% small
        else if (r < 95)
          size = rng_range(&rng, 256, 4096); // 25% medium
        else
          size = rng_range(&rng, 4096, 65536); // 5% large

        ptrs[slot] = alloc->malloc(size);
        if (ptrs[slot]) {
          sizes[slot] = size;
          live_count++;
        }
      }
    } else if (live_count > 0) {
      size_t slot = rng_next(&rng) % max_live;
      size_t attempts = 0;
      while (ptrs[slot] == NULL && attempts < 100) {
        slot = (slot + 1) % max_live;
        attempts++;
      }
      if (ptrs[slot] != NULL) {
        alloc->free(ptrs[slot]);
        ptrs[slot] = NULL;
        live_count--;
      }
    }

    if (op % 50000 == 0) {
      size_t current_rss = get_rss_bytes();
      if (current_rss > max_rss)
        max_rss = current_rss;
      fprintf(stderr, "\r    Op %zu/%zu: live=%zu, RSS=%zu KB", op,
              duration_ops, live_count, current_rss / 1024);
    }
  }

  for (size_t i = 0; i < max_live; i++) {
    if (ptrs[i])
      alloc->free(ptrs[i]);
  }
  alloc->free(ptrs);
  alloc->free(sizes);

  size_t final_rss = get_rss_bytes();
  fprintf(stderr, "\n    Baseline: %zu KB, Max: %zu KB, Final: %zu KB\n",
          baseline_rss / 1024, max_rss / 1024, final_rss / 1024);

  return TEST_PASS;
}

test_case_t fragmentation_tests[] = {
    {"TC-FRAG-001", "swiss cheese pattern", test_frag_001},
    {"TC-FRAG-002", "sawtooth pattern", test_frag_002},
    {"TC-FRAG-003", "size class thrashing", test_frag_003},
    {"TC-FRAG-004", "long-running simulation", test_frag_004},
};

const size_t num_fragmentation_tests =
    sizeof(fragmentation_tests) / sizeof(fragmentation_tests[0]);
