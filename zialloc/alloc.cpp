#include "allocator.h"
#include "mem.h"
#include "types.h"
#include "zialloc_memory.hpp"

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <cstdio>
#include <cstring>

static bool g_initialized = false;
static allocator_stats_t g_stats{};
static std::atomic<uint64_t> g_alloc_count{0};
static std::atomic<uint64_t> g_free_count{0};
static std::atomic<uint64_t> g_realloc_count{0};
static std::atomic<size_t> g_bytes_allocated{0};
static std::atomic<int64_t> g_bytes_in_use{0};

namespace {
struct LocalStatsBatch {
  uint64_t alloc_count;
  uint64_t free_count;
  uint64_t realloc_count;
  size_t bytes_allocated;
  int64_t bytes_in_use_delta;
  uint32_t ops;
};

static thread_local LocalStatsBatch g_local_stats{0, 0, 0, 0, 0, 0};
static constexpr uint32_t STATS_FLUSH_INTERVAL = 1024;

static inline void flush_local_stats_batch() {
  if (g_local_stats.alloc_count) {
    g_alloc_count.fetch_add(g_local_stats.alloc_count, std::memory_order_relaxed);
    g_local_stats.alloc_count = 0;
  }
  if (g_local_stats.free_count) {
    g_free_count.fetch_add(g_local_stats.free_count, std::memory_order_relaxed);
    g_local_stats.free_count = 0;
  }
  if (g_local_stats.realloc_count) {
    g_realloc_count.fetch_add(g_local_stats.realloc_count, std::memory_order_relaxed);
    g_local_stats.realloc_count = 0;
  }
  if (g_local_stats.bytes_allocated) {
    g_bytes_allocated.fetch_add(g_local_stats.bytes_allocated, std::memory_order_relaxed);
    g_local_stats.bytes_allocated = 0;
  }
  if (g_local_stats.bytes_in_use_delta != 0) {
    g_bytes_in_use.fetch_add(g_local_stats.bytes_in_use_delta, std::memory_order_relaxed);
    g_local_stats.bytes_in_use_delta = 0;
  }
  g_local_stats.ops = 0;
}

static inline void maybe_flush_local_stats_batch() {
  g_local_stats.ops++;
  if (g_local_stats.ops >= STATS_FLUSH_INTERVAL) {
    flush_local_stats_batch();
  }
}
} // namespace

static int zialloc_init(void);
static void zialloc_teardown(void);
static void zialloc_print_stats(void);
static bool zialloc_get_stats(allocator_stats_t *stats);
static bool zialloc_validate_heap(void);
static size_t zialloc_usable_size(void *ptr);
static allocator_stats_t zialloc_snapshot_stats(void);

namespace zialloc {

class Allocator {
public:
  static Allocator &instance() {
    static Allocator alloc;
    return alloc;
  }

  void *malloc(size_t size);
  void free(void *ptr);
  void *realloc(void *ptr, size_t size);
  void *calloc(size_t nmemb, size_t size);

private:
  Allocator() = default;
  ~Allocator() = default;
};

void *Allocator::malloc(size_t size) {
  if (size == 0)
    return nullptr;
  if (size >= (SIZE_MAX - 4096))
    return nullptr;
  if (size > HEAP_RESERVED_DEFAULT)
    return nullptr;
  if (!g_initialized && zialloc_init() != 0)
    return nullptr;

  void *ptr = memory::heap_alloc(size);
  if (!ptr)
    return nullptr;

  size_t usable = memory::heap_last_alloc_usable();
  if (usable == 0) {
    usable = memory::heap_usable_size(ptr);
  }
  g_local_stats.alloc_count++;
  g_local_stats.bytes_allocated += size;
  g_local_stats.bytes_in_use_delta += static_cast<int64_t>(usable);
  maybe_flush_local_stats_batch();
  return ptr;
}

void Allocator::free(void *ptr) {
  if (!ptr)
    return;
  IS_HEAP_INITIALIZED(g_initialized);

  size_t usable = 0;
  if (!memory::free_dispatch_with_size(ptr, &usable))
    std::abort();

  g_local_stats.free_count++;
  g_local_stats.bytes_in_use_delta -= static_cast<int64_t>(usable);
  maybe_flush_local_stats_batch();
}

void *Allocator::realloc(void *ptr, size_t size) {
  if (ptr == nullptr)
    return malloc(size);
  IS_HEAP_INITIALIZED(g_initialized);
  if (size == 0) {
    free(ptr);
    return nullptr;
  }

  size_t old_usable = memory::heap_usable_size(ptr);
  if (old_usable >= size) {
    g_local_stats.realloc_count++;
    maybe_flush_local_stats_batch();
    return ptr;
  }

  void *new_ptr = malloc(size);
  if (!new_ptr)
    return nullptr;

  std::memcpy(new_ptr, ptr, old_usable);
  free(ptr);
  g_local_stats.realloc_count++;
  maybe_flush_local_stats_batch();
  return new_ptr;
}

void *Allocator::calloc(size_t nmemb, size_t size) {
  if (nmemb != 0 && size > SIZE_MAX / nmemb)
    return nullptr;
  size_t total = nmemb * size;
  void *ptr = malloc(total);
  if (!ptr)
    return nullptr;
  std::memset(ptr, 0, total);
  return ptr;
}

} // namespace zialloc

static void *zialloc_malloc(size_t size) {
  return zialloc::Allocator::instance().malloc(size);
}

static void zialloc_free(void *ptr) {
  zialloc::Allocator::instance().free(ptr);
}

static void *zialloc_realloc(void *ptr, size_t size) {
  return zialloc::Allocator::instance().realloc(ptr, size);
}

static void *zialloc_calloc(size_t nmemb, size_t size) {
  return zialloc::Allocator::instance().calloc(nmemb, size);
}

static size_t zialloc_usable_size(void *ptr) {
  return zialloc::memory::heap_usable_size(ptr);
}

static allocator_stats_t zialloc_snapshot_stats(void) {
  flush_local_stats_batch();
  allocator_stats_t snapshot = g_stats;
  snapshot.alloc_count = g_alloc_count.load(std::memory_order_relaxed);
  snapshot.free_count = g_free_count.load(std::memory_order_relaxed);
  snapshot.realloc_count = g_realloc_count.load(std::memory_order_relaxed);
  snapshot.bytes_allocated = g_bytes_allocated.load(std::memory_order_relaxed);
  const int64_t in_use = g_bytes_in_use.load(std::memory_order_relaxed);
  snapshot.bytes_in_use = in_use > 0 ? static_cast<size_t>(in_use) : 0;
  return snapshot;
}

static void zialloc_print_stats(void) {
  allocator_stats_t snapshot = zialloc_snapshot_stats();
  printf("  Allocations:   %lu\n", (unsigned long)snapshot.alloc_count);
  printf("  Frees:         %lu\n", (unsigned long)snapshot.free_count);
  printf("  Reallocs:      %lu\n", (unsigned long)snapshot.realloc_count);
  printf("  Bytes in use:  %zu\n", snapshot.bytes_in_use);
  printf("  Bytes mapped:  %zu\n", snapshot.bytes_mapped);
  printf("  mmap calls:    %lu\n", (unsigned long)snapshot.mmap_count);
  printf("  munmap calls:  %lu\n", (unsigned long)snapshot.munmap_count);
}

static bool zialloc_get_stats(allocator_stats_t *stats) {
  if (!stats)
    return false;
  *stats = zialloc_snapshot_stats();
  return true;
}

static bool zialloc_validate_heap(void) {
  return zialloc::memory::heap_validate();
}

static int zialloc_init(void) {
  if (g_initialized)
    return 0;

  std::memset(&g_stats, 0, sizeof(g_stats));
  g_alloc_count.store(0, std::memory_order_relaxed);
  g_free_count.store(0, std::memory_order_relaxed);
  g_realloc_count.store(0, std::memory_order_relaxed);
  g_bytes_allocated.store(0, std::memory_order_relaxed);
  g_bytes_in_use.store(0, std::memory_order_relaxed);
  g_local_stats = {0, 0, 0, 0, 0, 0};

  const size_t heap_reserved_size = HEAP_RESERVED_DEFAULT;
  void *reserved_base = zialloc::memory::reserve_region(heap_reserved_size);
  if (!reserved_base)
    return -1;

  if (!zialloc::memory::heap_init_reserved(reserved_base, heap_reserved_size))
    return -1;

  // feature toggle: zero-on-free check, default disabled for speed
  zialloc::memory::set_zero_on_free_enabled(false);
  zialloc::memory::set_uaf_check_enabled(false);

  // keep one small/medium/large segment active from start
  if (!zialloc::memory::heap_add_segment_for_class(PAGE_SM))
    return -1;
  if (!zialloc::memory::heap_add_segment_for_class(PAGE_MED))
    return -1;
  if (!zialloc::memory::heap_add_segment_for_class(PAGE_LG))
    return -1;

  g_initialized = true;
  return 0;
}

static void zialloc_teardown(void) {
  if (!g_initialized)
    return;
  zialloc::memory::heap_clear_metadata();
  zialloc::memory::set_zero_on_free_enabled(false);
  zialloc::memory::set_uaf_check_enabled(false);
  std::memset(&g_stats, 0, sizeof(g_stats));
  g_alloc_count.store(0, std::memory_order_relaxed);
  g_free_count.store(0, std::memory_order_relaxed);
  g_realloc_count.store(0, std::memory_order_relaxed);
  g_bytes_allocated.store(0, std::memory_order_relaxed);
  g_bytes_in_use.store(0, std::memory_order_relaxed);
  g_local_stats = {0, 0, 0, 0, 0, 0};
  g_initialized = false;
}

allocator_t zialloc_allocator = {
    .malloc = zialloc_malloc,
    .free = zialloc_free,
    .realloc = zialloc_realloc,
    .calloc = zialloc_calloc,
    .memalign = NULL,
    .aligned_alloc = NULL,
    .usable_size = zialloc_usable_size,
    .free_sized = NULL,
    .realloc_array = NULL,
    .bulk_free = NULL,
    .print_stats = zialloc_print_stats,
    .validate_heap = zialloc_validate_heap,
    .get_stats = zialloc_get_stats,
    .init = zialloc_init,
    .teardown = zialloc_teardown,
    .name = "Zialloc",
    .author = "ZiaRashid",
    .version = "1.0.0",
    .description = "custom memory allocator",
    .memory_backend = "mmap",
    .features =
        {
            .thread_safe = true,
            .per_thread_cache = true,
            .huge_page_support = false,
            .guard_pages = false,
            .guard_location = GUARD_NONE,
            .canaries = false,
            .quarantine = false,
            .zero_on_free = false,
            .min_alignment = 16,
            .max_alignment = 16,
        },
};

extern "C" allocator_t *get_test_allocator(void) { return &zialloc_allocator; }
extern "C" allocator_t *get_bench_allocator(void) { return &zialloc_allocator; }
  
