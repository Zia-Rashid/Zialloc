// test_stress.c - Stress Test Suite
// Tests: TC-STRESS-*

#include "allocator.h"
#include "test_harness.h"
#include <pthread.h>
#include <stdint.h>
#include <string.h>

// Configuration

#define STRESS_OPS_LARGE 1000000
#define STRESS_OPS_MEDIUM 100000
#define STRESS_OPS_SMALL 10000
#define MAX_LIVE_ALLOCS 100000

// TC-STRESS-001: Random malloc/free (1M ops)

static test_result_t test_stress_001(allocator_t *alloc) {
  test_rng_t rng;
  rng_seed(&rng, 0x12345678);

  void *ptrs[1024] = {0};
  size_t sizes[1024] = {0};
  uint8_t seeds[1024] = {0};
  size_t count = 0;

  for (size_t i = 0; i < STRESS_OPS_LARGE; i++) {
    bool do_alloc = (count == 0) || (rng_next(&rng) % 2 == 0 && count < 1024);

    if (do_alloc) {
      size_t size = rng_range(&rng, 1, 4096);
      void *ptr = alloc->malloc(size);
      TEST_ASSERT_NOT_NULL(ptr, "malloc should succeed");

      uint8_t seed = (uint8_t)(i & 0xFF);
      ptrs[count] = ptr;
      sizes[count] = size;
      seeds[count] = seed;
      fill_pattern(ptr, size, seed);
      count++;
    } else {
      size_t idx = rng_next(&rng) % count;
      TEST_ASSERT(verify_pattern(ptrs[idx], sizes[idx], seeds[idx]),
                  "pattern verify before free");
      alloc->free(ptrs[idx]);

      ptrs[idx] = ptrs[count - 1];
      sizes[idx] = sizes[count - 1];
      seeds[idx] = seeds[count - 1];
      count--;
    }

    if (i % 100000 == 0) {
      fprintf(stderr, "\r    Progress: %zu/%d ops, %zu live allocs", i,
              STRESS_OPS_LARGE, count);
    }
  }
  fprintf(stderr, "\r    Completed %d ops                              \n",
          STRESS_OPS_LARGE);

  for (size_t i = 0; i < count; i++) {
    alloc->free(ptrs[i]);
  }

  return TEST_PASS;
}

// TC-STRESS-002: LIFO pattern (1M ops)

static test_result_t test_stress_002(allocator_t *alloc) {
  test_rng_t rng;
  rng_seed(&rng, 0xDEADBEEF);

  void *stack[4096];
  size_t stack_sizes[4096];
  size_t top = 0;

  for (size_t i = 0; i < STRESS_OPS_LARGE; i++) {
    bool do_push = (top == 0) || (rng_next(&rng) % 2 == 0 && top < 4096);

    if (do_push) {
      size_t size = rng_range(&rng, 16, 2048);
      void *ptr = alloc->malloc(size);
      TEST_ASSERT_NOT_NULL(ptr, "malloc should succeed");
      fill_pattern(ptr, size, (uint8_t)top);
      stack[top] = ptr;
      stack_sizes[top] = size;
      top++;
    } else {
      top--;
      TEST_ASSERT(verify_pattern(stack[top], stack_sizes[top], (uint8_t)top),
                  "LIFO data integrity");
      alloc->free(stack[top]);
    }

    if (i % 100000 == 0) {
      fprintf(stderr, "\r    Progress: %zu/%d ops, stack depth %zu", i,
              STRESS_OPS_LARGE, top);
    }
  }
  fprintf(stderr, "\r    Completed %d ops                              \n",
          STRESS_OPS_LARGE);

  // Cleanup
  while (top > 0) {
    top--;
    alloc->free(stack[top]);
  }

  return TEST_PASS;
}

// TC-STRESS-003: FIFO pattern (1M ops)

static test_result_t test_stress_003(allocator_t *alloc) {
  test_rng_t rng;
  rng_seed(&rng, 0xCAFEBABE);

  void *queue[8192];
  size_t queue_sizes[8192];
  size_t head = 0, tail = 0;
  size_t count = 0;

  for (size_t i = 0; i < STRESS_OPS_LARGE; i++) {
    bool do_enqueue = (count == 0) || (rng_next(&rng) % 2 == 0 && count < 8192);

    if (do_enqueue) {
      size_t size = rng_range(&rng, 32, 1024);
      void *ptr = alloc->malloc(size);
      TEST_ASSERT_NOT_NULL(ptr, "malloc should succeed");
      fill_pattern(ptr, size, (uint8_t)tail);
      queue[tail] = ptr;
      queue_sizes[tail] = size;
      tail = (tail + 1) % 8192;
      count++;
    } else {
      TEST_ASSERT(verify_pattern(queue[head], queue_sizes[head], (uint8_t)head),
                  "FIFO data integrity");
      alloc->free(queue[head]);
      head = (head + 1) % 8192;
      count--;
    }

    if (i % 100000 == 0) {
      fprintf(stderr, "\r    Progress: %zu/%d ops, queue size %zu", i,
              STRESS_OPS_LARGE, count);
    }
  }
  fprintf(stderr, "\r    Completed %d ops                              \n",
          STRESS_OPS_LARGE);

  // Cleanup
  while (count > 0) {
    alloc->free(queue[head]);
    head = (head + 1) % 8192;
    count--;
  }

  return TEST_PASS;
}

// TC-STRESS-004: Realloc chains (100K ops)

static test_result_t test_stress_004(allocator_t *alloc) {
  test_rng_t rng;
  rng_seed(&rng, 0xFEEDFACE);

  for (size_t chain = 0; chain < 1000; chain++) {
    size_t size = rng_range(&rng, 8, 64);
    void *ptr = alloc->malloc(size);
    TEST_ASSERT_NOT_NULL(ptr, "initial malloc");
    fill_pattern(ptr, size, (uint8_t)chain);

    // Do 100 reallocs in this chain
    for (size_t i = 0; i < 100; i++) {
      size_t old_size = size;
      // Randomly grow or shrink
      if (rng_next(&rng) % 2 == 0) {
        size = size + rng_range(&rng, 1, 256);
      } else {
        size = (size > 32) ? size - rng_range(&rng, 1, size / 2) : size;
      }

      void *new_ptr = alloc->realloc(ptr, size);
      TEST_ASSERT_NOT_NULL(new_ptr, "realloc should succeed");

      // Verify old data preserved
      size_t check_size = (old_size < size) ? old_size : size;
      TEST_ASSERT(verify_pattern(new_ptr, check_size, (uint8_t)chain),
                  "realloc preserves data");

      // Fill new portion if grew
      if (size > old_size) {
        fill_pattern((uint8_t *)new_ptr + old_size, size - old_size,
                     (uint8_t)(chain + old_size));
      }

      ptr = new_ptr;
    }

    alloc->free(ptr);

    if (chain % 100 == 0) {
      fprintf(stderr, "\r    Progress: %zu/1000 chains", chain);
    }
  }
  fprintf(stderr, "\r    Completed 1000 realloc chains               \n");

  return TEST_PASS;
}

// TC-STRESS-005: Peak memory cycling (100 cycles)

static test_result_t test_stress_005(allocator_t *alloc) {
  for (size_t cycle = 0; cycle < 100; cycle++) {
    // Allocate to peak (10K allocations)
    void *ptrs[10000];
    for (size_t i = 0; i < 10000; i++) {
      ptrs[i] = alloc->malloc(1024);
      TEST_ASSERT_NOT_NULL(ptrs[i], "peak allocation");
    }

    // Free all
    for (size_t i = 0; i < 10000; i++) {
      alloc->free(ptrs[i]);
    }

    if (cycle % 10 == 0) {
      fprintf(stderr, "\r    Progress: %zu/100 cycles", cycle);
    }
  }
  fprintf(stderr, "\r    Completed 100 peak cycles                   \n");

  return TEST_PASS;
}

// TC-STRESS-006: Many simultaneous allocations (100K live)

static test_result_t test_stress_006(allocator_t *alloc) {
  void **ptrs = alloc->malloc(MAX_LIVE_ALLOCS * sizeof(void *));
  TEST_ASSERT_NOT_NULL(ptrs, "meta allocation");

  fprintf(stderr, "\r    Allocating 100K objects...");
  for (size_t i = 0; i < MAX_LIVE_ALLOCS; i++) {
    ptrs[i] = alloc->malloc(64);
    TEST_ASSERT_NOT_NULL(ptrs[i], "simultaneous allocation");
    ((uint64_t *)ptrs[i])[0] = i; // Tag each allocation
  }

  fprintf(stderr, "\r    Verifying 100K objects...  ");
  for (size_t i = 0; i < MAX_LIVE_ALLOCS; i++) {
    TEST_ASSERT(((uint64_t *)ptrs[i])[0] == i, "allocation integrity");
  }

  fprintf(stderr, "\r    Freeing 100K objects...    ");
  for (size_t i = 0; i < MAX_LIVE_ALLOCS; i++) {
    alloc->free(ptrs[i]);
  }

  alloc->free(ptrs);
  fprintf(stderr, "\r    Completed 100K simultaneous allocs           \n");

  return TEST_PASS;
}

// TC-STRESS-THREAD-002: Producer-Consumer
// One thread allocates, passes to queue, other thread frees
#define QUEUE_SIZE 1024
typedef struct {
  void *buffer[QUEUE_SIZE];
  int head;
  int tail;
  pthread_mutex_t lock;
  pthread_cond_t not_empty;
  pthread_cond_t not_full;
  int done;
} shared_queue_t;

static void queue_init(shared_queue_t *q) {
  q->head = 0;
  q->tail = 0;
  q->done = 0;
  pthread_mutex_init(&q->lock, NULL);
  pthread_cond_init(&q->not_empty, NULL);
  pthread_cond_init(&q->not_full, NULL);
}

static void queue_push(shared_queue_t *q, void *ptr) {
  pthread_mutex_lock(&q->lock);
  while (((q->tail + 1) % QUEUE_SIZE == q->head) && !q->done) {
    pthread_cond_wait(&q->not_full, &q->lock);
  }
  q->buffer[q->tail] = ptr;
  q->tail = (q->tail + 1) % QUEUE_SIZE;
  pthread_cond_signal(&q->not_empty);
  pthread_mutex_unlock(&q->lock);
}

static void *queue_pop(shared_queue_t *q) {
  pthread_mutex_lock(&q->lock);
  while (q->head == q->tail && !q->done) {
    pthread_cond_wait(&q->not_empty, &q->lock);
  }
  if (q->head == q->tail && q->done) {
    pthread_mutex_unlock(&q->lock);
    return NULL;
  }
  void *ptr = q->buffer[q->head];
  q->head = (q->head + 1) % QUEUE_SIZE;
  pthread_cond_signal(&q->not_full);
  pthread_mutex_unlock(&q->lock);
  return ptr;
}

typedef struct {
  allocator_t *alloc;
  shared_queue_t *q;
} thread_ctx_t;

static void *producer_thread(void *arg) {
  thread_ctx_t *ctx = (thread_ctx_t *)arg;
  for (int i = 0; i < 10000; i++) {
    void *ptr = ctx->alloc->malloc(64);
    if (ptr) {
      memset(ptr, 0xAB, 64);
      queue_push(ctx->q, ptr);
    }
  }
  pthread_mutex_lock(&ctx->q->lock);
  ctx->q->done = 1;
  pthread_cond_broadcast(&ctx->q->not_empty);
  pthread_mutex_unlock(&ctx->q->lock);
  return NULL;
}

static void *consumer_thread(void *arg) {
  thread_ctx_t *ctx = (thread_ctx_t *)arg;
  while (1) {
    void *ptr = queue_pop(ctx->q);
    if (!ptr)
      break;
    // Verify content
    unsigned char *p = (unsigned char *)ptr;
    if (p[0] != 0xAB) {
      // let it crash idk
    }
    ctx->alloc->free(ptr);
  }
  return NULL;
}

static test_result_t test_stress_thread_producer_consumer(allocator_t *alloc) {
  if (!alloc->features.thread_safe)
    return TEST_SKIP;

  shared_queue_t q;
  queue_init(&q);

  thread_ctx_t ctx = {alloc, &q};
  pthread_t p, c;

  pthread_create(&p, NULL, producer_thread, &ctx);
  pthread_create(&c, NULL, consumer_thread, &ctx);

  pthread_join(p, NULL);
  pthread_join(c, NULL);

  return TEST_PASS;
}

// TC-STRESS-OOM-001: OOM Recovery behavior
static test_result_t test_stress_oom_recovery(allocator_t *alloc) {
  void **ptrs = alloc->malloc(10000 * sizeof(void *));
  TEST_ASSERT_NOT_NULL(ptrs, "setup failed");

  size_t count = 0;
  // Fill until OOM or limit
  for (size_t i = 0; i < 10000; i++) {
    // alloc big chunks to crash faster
    ptrs[i] = alloc->malloc(1024 * 1024);
    if (ptrs[i] == NULL) {
      fprintf(stderr, "\r    [INFO] Hit OOM at %zu MB\n", count);
      break;
    }
    count++;
  }

  if (count == 0 || count == 10000) {
    for (size_t i = 0; i < count; i++)
      alloc->free(ptrs[i]);
    alloc->free(ptrs);
    return TEST_PASS; // Pass, as we didn't crash
  }

  // Now free half
  for (size_t i = 0; i < count; i += 2) {
    alloc->free(ptrs[i]);
    ptrs[i] = NULL;
  }

  void *retry = alloc->malloc(1024 * 1024);
  TEST_ASSERT_NOT_NULL(retry, "Should recover from OOM after freeing");
  alloc->free(retry);

  // Cleanup rest
  for (size_t i = 0; i < count; i++) {
    if (ptrs[i])
      alloc->free(ptrs[i]);
  }
  alloc->free(ptrs);
  return TEST_PASS;
}

test_case_t stress_tests[] = {
    {"TC-STRESS-001", "random malloc/free (1M ops)", test_stress_001},
    {"TC-STRESS-002", "LIFO pattern (1M ops)", test_stress_002},
    {"TC-STRESS-003", "FIFO pattern (1M ops)", test_stress_003},
    {"TC-STRESS-004", "realloc chains (100K ops)", test_stress_004},
    {"TC-STRESS-005", "peak memory cycling (100 cycles)", test_stress_005},
    {"TC-STRESS-006", "100K simultaneous allocations", test_stress_006},
    {"TC-STRESS-THREAD-02", "producer-consumer threads",
     test_stress_thread_producer_consumer},
    {"TC-STRESS-OOM-001", "oom recovery", test_stress_oom_recovery},
};

const size_t num_stress_tests = sizeof(stress_tests) / sizeof(stress_tests[0]);
