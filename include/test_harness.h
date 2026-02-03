// test_harness.h - Test Framework for Memory Allocators

#ifndef TEST_HARNESS_H
#define TEST_HARNESS_H

#include "allocator.h"
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

// ANSI Color Codes, i like colors so here u go
#define COLOR_RED "\033[31m"
#define COLOR_GREEN "\033[32m"
#define COLOR_YELLOW "\033[33m"
#define COLOR_CYAN "\033[36m"
#define COLOR_RESET "\033[0m"

typedef enum {
  TEST_PASS = 0,
  TEST_FAIL = 1,
  TEST_SKIP = 2,
} test_result_t;

typedef struct {
  const char *test_id;
  const char *description;
  test_result_t (*test_fn)(allocator_t *alloc);
} test_case_t;

typedef struct {
  size_t total;
  size_t passed;
  size_t failed;
  size_t skipped;
} test_summary_t;

// Test Macros this is like super duper basic unit testing

#define TEST_ASSERT(cond, msg)                                                 \
  do {                                                                         \
    if (!(cond)) {                                                             \
      fprintf(stderr, "    [" COLOR_RED "FAIL" COLOR_RESET "] %s:%d: %s\n",    \
              __FILE__, __LINE__, msg);                                        \
      return TEST_FAIL;                                                        \
    }                                                                          \
  } while (0)

#define TEST_ASSERT_EQ(a, b, msg)                                              \
  do {                                                                         \
    if ((a) != (b)) {                                                          \
      fprintf(stderr,                                                          \
              "    [" COLOR_RED "FAIL" COLOR_RESET                             \
              "] %s:%d: %s (got %zu, expected %zu)\n",                         \
              __FILE__, __LINE__, msg, (size_t)(a), (size_t)(b));              \
      return TEST_FAIL;                                                        \
    }                                                                          \
  } while (0)

#define TEST_ASSERT_NEQ(a, b, msg)                                             \
  do {                                                                         \
    if ((a) == (b)) {                                                          \
      fprintf(stderr,                                                          \
              "    [" COLOR_RED "FAIL" COLOR_RESET                             \
              "] %s:%d: %s (both are %zu)\n",                                  \
              __FILE__, __LINE__, msg, (size_t)(a));                           \
      return TEST_FAIL;                                                        \
    }                                                                          \
  } while (0)

#define TEST_ASSERT_NOT_NULL(ptr, msg)                                         \
  do {                                                                         \
    if ((ptr) == NULL) {                                                       \
      fprintf(stderr,                                                          \
              "    [" COLOR_RED "FAIL" COLOR_RESET "] %s:%d: %s (got NULL)\n", \
              __FILE__, __LINE__, msg);                                        \
      return TEST_FAIL;                                                        \
    }                                                                          \
  } while (0)

#define TEST_ASSERT_NULL(ptr, msg)                                             \
  do {                                                                         \
    if ((ptr) != NULL) {                                                       \
      fprintf(stderr,                                                          \
              "    [" COLOR_RED "FAIL" COLOR_RESET                             \
              "] %s:%d: %s (expected NULL, got %p)\n",                         \
              __FILE__, __LINE__, msg, (ptr));                                 \
      return TEST_FAIL;                                                        \
    }                                                                          \
  } while (0)

#define TEST_ASSERT_ALIGNED(ptr, align, msg)                                   \
  do {                                                                         \
    if (((uintptr_t)(ptr) % (align)) != 0) {                                   \
      fprintf(stderr,                                                          \
              "    [" COLOR_RED "FAIL" COLOR_RESET                             \
              "] %s:%d: %s (ptr %p not aligned to %zu)\n",                     \
              __FILE__, __LINE__, msg, (void *)(ptr), (size_t)(align));        \
      return TEST_FAIL;                                                        \
    }                                                                          \
  } while (0)

#define TEST_SKIP_IF(cond, msg)                                                \
  do {                                                                         \
    if (cond) {                                                                \
      fprintf(stderr, "    [" COLOR_YELLOW "SKIP" COLOR_RESET "] %s\n", msg);  \
      return TEST_SKIP;                                                        \
    }                                                                          \
  } while (0)

static inline void fill_pattern(void *ptr, size_t size, uint8_t seed) {
  uint8_t *p = (uint8_t *)ptr;
  for (size_t i = 0; i < size; i++) {
    p[i] = (uint8_t)((seed + i) & 0xFF);
  }
}

static inline bool verify_pattern(void *ptr, size_t size, uint8_t seed) {
  uint8_t *p = (uint8_t *)ptr;
  for (size_t i = 0; i < size; i++) {
    if (p[i] != (uint8_t)((seed + i) & 0xFF)) {
      return false;
    }
  }
  return true;
}

static inline bool is_zeroed(void *ptr, size_t size) {
  uint8_t *p = (uint8_t *)ptr;
  for (size_t i = 0; i < size; i++) {
    if (p[i] != 0) {
      return false;
    }
  }
  return true;
}

// shout out to my crypto people
typedef struct {
  uint64_t state;
} test_rng_t;

static inline void rng_seed(test_rng_t *rng, uint64_t seed) {
  rng->state = seed;
}

static inline uint64_t rng_next(test_rng_t *rng) {
  // xorshift64
  uint64_t x = rng->state;
  x ^= x << 13;
  x ^= x >> 7;
  x ^= x << 17;
  rng->state = x;
  return x;
}

static inline size_t rng_range(test_rng_t *rng, size_t min, size_t max) {
  return min + (rng_next(rng) % (max - min + 1));
}

static inline void run_test_suite(const char *suite_name, test_case_t *tests,
                                  size_t num_tests, allocator_t *alloc,
                                  test_summary_t *summary) {
  printf("Test Suite: %s\n", suite_name);
  printf("Allocator:  %s (%s)\n", alloc->name, alloc->version);

  for (size_t i = 0; i < num_tests; i++) {
    printf("[" COLOR_CYAN "%s" COLOR_RESET "] %s... ", tests[i].test_id,
           tests[i].description);
    fflush(stdout);

    summary->total++;
    test_result_t result = tests[i].test_fn(alloc);

    switch (result) {
    case TEST_PASS:
      printf("\033[32mPASS\033[0m\n");
      summary->passed++;
      break;
    case TEST_FAIL:
      printf("\033[31mFAIL\033[0m\n");
      summary->failed++;
      break;
    case TEST_SKIP:
      printf("\033[33mSKIP\033[0m\n");
      summary->skipped++;
      break;
    }
  }
}

static inline void print_summary(test_summary_t *summary) {
  printf("Summary: %zu total, \033[32m%zu passed\033[0m, \033[31m%zu "
         "failed\033[0m, \033[33m%zu skipped\033[0m\n",
         summary->total, summary->passed, summary->failed, summary->skipped);
}

// stolen from evt codebase

static inline uint64_t get_time_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

#ifdef __cplusplus
}
#endif

#endif // TEST_HARNESS_H
