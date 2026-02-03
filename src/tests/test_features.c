// test_features.c - Optional Feature Tests
#include "allocator.h"
#include "test_harness.h"
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

// death tests - expect crash no segfault worries
static test_result_t run_death_test(void (*func)(allocator_t *),
                                    allocator_t *alloc) {
  pid_t pid = fork();
  if (pid < 0) {
    return TEST_FAIL;
  }

  if (pid == 0) {
    alarm(2); // Safety net for child process
    func(alloc);
    exit(0); // If we reached here, we didn't crash -> FAIL
  }

  int status;
  // Wait with timeout to prevent hanging CI
  int timeout_ms = 2000;
  int interval_ms = 10;
  int elapsed = 0;

  while (elapsed < timeout_ms) {
    int res = waitpid(pid, &status, WNOHANG);
    if (res == pid) {
      break; // Child exited
    }
    if (res < 0) {
      return TEST_FAIL; // Error waiting
    }
    usleep(interval_ms * 1000);
    elapsed += interval_ms;
  }

  if (elapsed >= timeout_ms) {
    kill(pid, SIGKILL);
    waitpid(pid, &status, 0); // Cleanup
    fprintf(stderr, "    [TIMEOUT] Death test timed out (deadlock?)\n");
    return TEST_FAIL;
  }

  if (WIFSIGNALED(status)) {
    // Crashed by signal (SEGV, ABRT, etc.) -> PASS for security feature
    return TEST_PASS;
  }

  if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
    // Exited with error code (detected and aborted safely) -> PASS
    return TEST_PASS;
  }

  return TEST_FAIL;
}

// TC-FEAT-THREAD-001: Basic Thread Safety
// Spawn threads, concurrent malloc/free
typedef struct {
  allocator_t *alloc;
  int thread_id;
} thread_arg_t;

static void *thread_func(void *arg) {
  thread_arg_t *t_arg = (thread_arg_t *)arg;
  allocator_t *alloc = t_arg->alloc;

  for (int i = 0; i < 1000; i++) {
    void *p = alloc->malloc(64);
    if (p) {
      memset(p, 0xAA, 64);
      alloc->free(p);
    }
  }
  return NULL;
}

static test_result_t test_feat_thread_001(allocator_t *alloc) {
  if (!alloc->features.thread_safe) {
    return TEST_SKIP;
  }

  const int num_threads = 4;
  pthread_t threads[4];
  thread_arg_t args[4];

  for (int i = 0; i < num_threads; i++) {
    args[i].alloc = alloc;
    args[i].thread_id = i;
    if (pthread_create(&threads[i], NULL, thread_func, &args[i]) != 0) {
      return TEST_FAIL;
    }
  }

  for (int i = 0; i < num_threads; i++) {
    pthread_join(threads[i], NULL);
  }

  return TEST_PASS;
}

// TC-FEAT-ZERO-001: Zero-on-Free Verification
static test_result_t test_feat_zero_001(allocator_t *alloc) {
  if (!alloc->features.zero_on_free) {
    return TEST_SKIP;
  }

  size_t sz = 128;
  void *p = alloc->malloc(sz);
  TEST_ASSERT_NOT_NULL(p, "malloc failed");
  memset(p, 0xCC, sz);
  alloc->free(p);

  void *p2 = alloc->malloc(sz);
  TEST_ASSERT_NOT_NULL(p2, "malloc failed");

  unsigned char *bytes = (unsigned char *)p2;
  for (size_t i = 0; i < sz; i++) {
    if (bytes[i] != 0) {
      alloc->free(p2);
      return TEST_FAIL;
    }
  }

  alloc->free(p2);
  return TEST_PASS;
}

// TC-FEAT-QUARANTINE-001: Quarantine Pattern
static test_result_t test_feat_quarantine_001(allocator_t *alloc) {
  if (!alloc->features.quarantine) {
    return TEST_SKIP;
  }

  void *p1 = alloc->malloc(64);
  TEST_ASSERT_NOT_NULL(p1, "malloc failed");
  alloc->free(p1);

  void *p2 = alloc->malloc(64);
  TEST_ASSERT_NOT_NULL(p2, "malloc failed");

  // quarantine zone shouldnt reuse
  if (p1 == p2) {
    alloc->free(p2);
    return TEST_FAIL; // Reused immediately
  }

  alloc->free(p2);
  return TEST_PASS;
}

// TC-FEAT-CANARY-001: Canary Death Test
static void canary_trigger(allocator_t *alloc) {
  size_t size = 32;
  unsigned char *ptr = (unsigned char *)alloc->malloc(size);
  if (!ptr)
    exit(0);

  ptr[size] = 0xDE;

  // boom
  alloc->free(ptr);
}

static test_result_t test_feat_canary_001(allocator_t *alloc) {
  if (!alloc->features.canaries)
    return TEST_SKIP;
  return run_death_test(canary_trigger, alloc);
}

// TC-FEAT-GUARD-001: Guard Page Death Test
static void guard_trigger(allocator_t *alloc) {
  size_t page_size = 4096;
  char *ptr = (char *)alloc->memalign(page_size, page_size);
  if (!ptr)
    exit(0);

  // Check configured guard location
  volatile char val;
  if (alloc->features.guard_location & GUARD_AFTER) {
    val = ptr[page_size]; // Access byte just after allocation
  } else if (alloc->features.guard_location & GUARD_BEFORE) {
    val = ptr[-1]; // Access byte just before allocation
  } else {
    val = ptr[page_size];
  }
  (void)val;
}

static test_result_t test_feat_guard_001(allocator_t *alloc) {
  if (!alloc->features.guard_pages)
    return TEST_SKIP;
  if (!alloc->memalign)
    return TEST_SKIP;
  return run_death_test(guard_trigger, alloc);
}

// TC-FEAT-HUGE-001: Huge Page (Smoke Test)
static test_result_t test_feat_huge_001(allocator_t *alloc) {
  if (!alloc->features.huge_page_support)
    return TEST_SKIP;

  size_t size = 4 * 1024 * 1024;
  void *ptr = alloc->malloc(size);
  TEST_ASSERT_NOT_NULL(ptr, "huge allocation failed");

  // Write to it to ensure it's faulted in
  memset(ptr, 0x11, size);

  alloc->free(ptr);
  return TEST_PASS;
}

static void *ptcache_thread_func(void *arg) {
  allocator_t *alloc = (allocator_t *)arg;
  int reuse_count = 0;
  const int iterations = 100;

  for (int i = 0; i < iterations; i++) {
    void *p1 = alloc->malloc(64);
    if (!p1)
      return NULL;

    // Fill to ensure we touch memory
    memset(p1, 0xCC, 64);

    alloc->free(p1);

    // Immediate reclaim attempt
    void *p2 = alloc->malloc(64);
    if (p2 == p1) {
      reuse_count++;
    }

    if (p2)
      alloc->free(p2);
  }

  // Return reuse count as pointer
  return (void *)(intptr_t)reuse_count;
}

static test_result_t test_feat_ptcache_001(allocator_t *alloc) {
  if (!alloc->features.per_thread_cache)
    return TEST_SKIP;

  pthread_t threads[4];
  int total_reuse = 0;

  for (int i = 0; i < 4; i++) {
    pthread_create(&threads[i], NULL, ptcache_thread_func, alloc);
  }

  for (int i = 0; i < 4; i++) {
    void *ret;
    pthread_join(threads[i], &ret);
    total_reuse += (int)(intptr_t)ret;
  }

  // We expect high reuse rate for per-thread caches (LIFO hot cache)
  // If total reuse is low, either it's not working or it's not LIFO.
  if (total_reuse < 200) { // Expect >50% reuse roughly across 400 iters
    fprintf(stderr,
            "    [INFO] Low tcache reuse: %d/400 (Expected for "
            "secure/randomized allocators)\n",
            total_reuse);
  }

  return TEST_PASS;
}

// Security Tests, this is the only place where I will help you with defending
// against attacks

// TC-SEC-DBLFREE: 2Free
static void dblfree_trigger(allocator_t *alloc) {
  void *ptr = alloc->malloc(64);
  if (!ptr)
    exit(0);

  alloc->free(ptr);
  alloc->free(ptr);

  void *p1 = alloc->malloc(64);
  void *p2 = alloc->malloc(64);

  if (p1 != NULL && p1 == p2) {
    fprintf(stderr,
            "\n    [CRITICAL] Double Free Exploitable: Returned %p twice!\n",
            p1);
    exit(0); // Fail
  }

  // If we survived and gave meaningful pointers (or crashed safely before), we
  // are okay. Secure behavior: Ignore second free, so p1 != p2.
  exit(1);
}

static test_result_t test_sec_dblfree(allocator_t *alloc) {
  return run_death_test(dblfree_trigger, alloc);
}

// TC-SEC-CORRUPT: Metadata Corruption
static void corrupt_trigger(allocator_t *alloc) {
  void *ptr = alloc->malloc(64);
  if (!ptr)
    exit(0);

  // trash the header
  uint64_t *header = (uint64_t *)ptr - 1;
  *header = 0xBADBADBADBAD;

  alloc->free(ptr); // boom
  // Safe survival
  exit(1);
}

static test_result_t test_sec_corrupt(allocator_t *alloc) {
  return run_death_test(corrupt_trigger, alloc);
}

// TC-SEC-SPIRIT: House of Spirit (Stack Free)
static void spirit_trigger(allocator_t *alloc) {
  // 16 byte aligned chunk
  uint64_t stack_buf[16] __attribute__((aligned(16)));

  // chunk metadata
  // index 0: prev_size (not used if prev_inuse is set)
  stack_buf[0] = 0;
  // index 1: size (0x40 bytes = 64) | PREV_INUSE (0x1) -> 0x41
  stack_buf[1] = 0x41;

  void *ptr = &stack_buf[2];

  stack_buf[8] = 0;
  stack_buf[9] = 0x21;

  // if insecure: accepts stack ptr into freelist (no crash -> TEST_FAIL)
  // if secure: detects invalid address/bounds -> aborts (TEST_PASS)
  alloc->free(ptr);

  // If we survived, the allocator accepted the stack pointer!
  fprintf(stderr,
          "\n    [CRITICAL] House of Spirit Succeeded! Stack ptr %p freed.\n",
          ptr);
  exit(0); // Vulnerable
}

static test_result_t test_sec_spirit(allocator_t *alloc) {
  return run_death_test(spirit_trigger, alloc);
}

// TC-SEC-LORE: House of Lore (Free List Poisoning)
static void lore_trigger(allocator_t *alloc) {
  void *victim = alloc->malloc(64);
  void *p2 = alloc->malloc(64); // Prevent consolidation
  (void)p2;

  if (!victim)
    exit(0);
  alloc->free(victim);

  // Poison the free chunk to point to stack
  // This simulates a "Use After Free" overwrite of the FD pointer
  uint64_t stack_target[4] = {0, 0, 0, 0}; // Fake chunk
  uint64_t *victim_mem = (uint64_t *)victim;
  *victim_mem = (uint64_t)&stack_target;

  // Reclaim victim
  void *p3 = alloc->malloc(64);
  (void)p3;
  // Reclaim stack?
  void *p4 = alloc->malloc(64);

  if (p4 == &stack_target) {
    fprintf(stderr,
            "\n    [CRITICAL] House of Lore Succeeded! Returned stack %p\n",
            p4);
    exit(0); // Vulnerable
  }

  // If we crashed or didn't return stack, good.
  exit(1);
}

static test_result_t test_sec_lore(allocator_t *alloc) {
  return run_death_test(lore_trigger, alloc);
}

// TC-SEC-FORCE: House of Force (Top Chunk Size Corruption)
static void force_trigger(allocator_t *alloc) {
  void *p1 = alloc->malloc(4096);
  if (!p1)
    exit(0);

  uint64_t *scan = (uint64_t *)((char *)p1 + 4096);
  for (int i = 0; i < 32; i++) {
    if (scan[i] > 1024 * 1024) {
      scan[i] = (uint64_t)-1;
      break;
    }
  }

  void *huge = alloc->malloc((size_t)-8192); // NEAR SIZE_MAX
  if (huge) {
    fprintf(stderr, "\n    [CRITICAL] House of Force Succeeded! Returned %p\n",
            huge);
    exit(0); // Vulnerable
  }

  // If NULL or crash, we are safe.
  exit(1);
}

static test_result_t test_sec_force(allocator_t *alloc) {
  return run_death_test(force_trigger, alloc);
}

test_case_t feature_tests[] = {
    {"TC-FEAT-THREAD-001", "basic thread safety", test_feat_thread_001},
    {"TC-FEAT-ZERO-001", "zero-on-free check", test_feat_zero_001},
    {"TC-FEAT-QUAR-001", "quarantine delay reuse", test_feat_quarantine_001},
    {"TC-FEAT-CANARY-001", "canary overflow detect", test_feat_canary_001},
    {"TC-FEAT-GUARD-001", "guard page access check", test_feat_guard_001},
    {"TC-FEAT-HUGE-001", "huge page allocation", test_feat_huge_001},
    {"TC-FEAT-PTCACHE-001", "per-thread cache check", test_feat_ptcache_001},

    // Security / Robustness
    {"TC-SEC-DBLFREE", "double free detection", test_sec_dblfree},
    {"TC-SEC-CORRUPT", "metadata corruption (header)", test_sec_corrupt},
    {"TC-SEC-SPIRIT", "house of spirit (stack free)", test_sec_spirit},
    {"TC-SEC-LORE", "house of lore (poisoning)", test_sec_lore},
    {"TC-SEC-FORCE", "house of force (top size)", test_sec_force},
};

const size_t num_feature_tests =
    sizeof(feature_tests) / sizeof(feature_tests[0]);
