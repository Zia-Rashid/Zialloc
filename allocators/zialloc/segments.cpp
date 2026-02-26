#include <array>
#include <cstddef>
#include <cstdint>
#include <atomic>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

#include "types.h"
#include "mem.h"
#include "zialloc_memory.hpp"

namespace zialloc::memory {

namespace {

static std::atomic<bool> g_zero_on_free{false};
static std::atomic<bool> g_uaf_check{false};

static constexpr size_t PAGE_LOCK_STRIPES = 2048;
static constexpr size_t MAX_QUEUE_PROBES_PER_ALLOC = 64;
static constexpr size_t MAX_FALLBACK_SCANS_PER_ALLOC = 128;	// max segments that will be checked on slow path
static std::array<std::mutex, PAGE_LOCK_STRIPES> g_page_locks;
static thread_local size_t g_last_alloc_usable = 0;
static std::atomic<uint32_t> g_live_threads{0};

static inline page_kind_t class_for_size(size_t size) {
  if (size <= CHUNK_SM)
    return PAGE_SM;
  if (size <= CHUNK_MD)
    return PAGE_MED;
  if (size <= CHUNK_LG)
    return PAGE_LG;
  return PAGE_XL;
}

static inline size_t page_size_for_kind(page_kind_t kind) {
  return page_kind_size(kind);
}

static inline size_t ceil_pow2_at_least_16(size_t n) {
  if (n <= 16)
    return 16;
  const size_t x = n - 1;
  const unsigned lz =
      static_cast<unsigned>(__builtin_clzll(static_cast<unsigned long long>(x)));
  const unsigned shift = 64U - lz;
  return static_cast<size_t>(1ULL << shift);
}

static inline size_t norm_chunk_req(page_kind_t kind, size_t req) {
  // bucket only small/medium classes to reduce page-sized fragmentation scans
  if (kind != PAGE_SM && kind != PAGE_MED)
    return align_up(req, 16);
  size_t norm = ceil_pow2_at_least_16(req);
  const size_t cap = (kind == PAGE_SM) ? static_cast<size_t>(CHUNK_SM)
                                       : static_cast<size_t>(CHUNK_MD);
  if (norm > cap)
    norm = cap;
  return align_up(norm, 16);
}

static inline std::mutex &page_lock_for(const void *page_like_ptr) {
  const uintptr_t key = reinterpret_cast<uintptr_t>(page_like_ptr) >> 4;
  return g_page_locks[key % PAGE_LOCK_STRIPES];
}

static inline size_t class_index_for_kind(page_kind_t kind) {
  return static_cast<size_t>(kind);
}

class Page;
class Segment;

static constexpr uint32_t CHUNK_MAGIC = 0xC47A110CU;
static constexpr uint64_t XL_MAGIC = 0x584C4F43484B4559ULL; // "XLOCHKEY"

struct ChunkHeader {
  Page *owner;
  uint32_t slot;
  uint32_t magic;
};

struct XLHeader {
  uint64_t 	magic;
  size_t 		mapping_size;
  size_t 		usable_size;
  uint64_t 	reserved;
};

class DeferredRing {
private:
  static constexpr uint32_t CAP = 256;
  static constexpr uint32_t MASK = CAP - 1;

  struct Cell {
    std::atomic<uint32_t> seq;
    void *data;
  };

  std::array<Cell, CAP> cells;
  std::atomic<uint32_t> head;
  std::atomic<uint32_t> tail;

public:
  DeferredRing() : cells(), head(0), tail(0) {
    for (uint32_t i = 0; i < CAP; ++i) {
      cells[i].seq.store(i, std::memory_order_relaxed);
      cells[i].data = nullptr;
    }
  }

  bool push(void *ptr) {
    uint32_t pos = head.load(std::memory_order_relaxed);
    for (;;) {
      Cell &cell = cells[pos & MASK];
      const uint32_t seq = cell.seq.load(std::memory_order_acquire);
      const intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);
      if (diff == 0) {
        if (head.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed,
                                       std::memory_order_relaxed)) {
          cell.data = ptr;
          cell.seq.store(pos + 1, std::memory_order_release);
          return true;
        }
      } else if (diff < 0) {
        return false;
      } else {
        pos = head.load(std::memory_order_relaxed);
      }
    }
  }

  bool pop(void **out) {
    uint32_t pos = tail.load(std::memory_order_relaxed);
    for (;;) {
      Cell &cell = cells[pos & MASK];
      const uint32_t seq = cell.seq.load(std::memory_order_acquire);
      const intptr_t dif = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);
      if (dif == 0) {
        if (tail.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed,
                                       std::memory_order_relaxed)) {
          *out = cell.data;
          cell.seq.store(pos + CAP, std::memory_order_release);
          return true;
        }
      } else if (dif < 0) {
        return false;
      } else {
        pos = tail.load(std::memory_order_relaxed);
      }
    }
  }

  size_t approx_size() const {
    const uint32_t h = head.load(std::memory_order_relaxed);
    const uint32_t t = tail.load(std::memory_order_relaxed);
    return static_cast<size_t>(h - t);
  }
};

class Page {
private:
  Segment *owner_segment;
  size_t owner_segment_idx;
  void *base;
  page_kind_t size_class;
  size_t page_span;
  size_t chunk_stride;
  size_t chunk_usable;
  uint32_t capacity;
  uint32_t used;
  uint32_t first_hint;
  pid_t owner_tid;
  page_status_t status;
  bool initialized;
  std::vector<uint64_t> used_bitmap;
  DeferredRing deferred_frees;

  ChunkHeader *slot_header(uint32_t slot) const {
    return reinterpret_cast<ChunkHeader *>(static_cast<char *>(base) +
                                           static_cast<size_t>(slot) * chunk_stride);
  }

  void *slot_user_ptr(uint32_t slot) const {
    return static_cast<void *>(reinterpret_cast<char *>(slot_header(slot)) + sizeof(ChunkHeader));
  }

  ChunkHeader *user_to_header(void *ptr) const {
    return reinterpret_cast<ChunkHeader *>(static_cast<char *>(ptr) - sizeof(ChunkHeader));
  }

  bool bit_is_set(uint32_t idx) const {
    const uint32_t word = idx >> 6;
    const uint32_t bit = idx & 63U;
    return (used_bitmap[word] & (1ULL << bit)) != 0;
  }

  void bit_set(uint32_t idx) {
    const uint32_t word = idx >> 6;
    const uint32_t bit = idx & 63U;
    used_bitmap[word] |= (1ULL << bit);
  }

  void bit_clear(uint32_t idx) {
    const uint32_t word = idx >> 6;
    const uint32_t bit = idx & 63U;
    used_bitmap[word] &= ~(1ULL << bit);
  }

  bool validate_header(void *ptr, ChunkHeader *hdr) const {
    if (!hdr)
      return false;
    if (hdr->magic != CHUNK_MAGIC)
      return false;
    if (hdr->owner != this)
      return false;
    if (hdr->slot >= capacity)
      return false;
    return slot_user_ptr(hdr->slot) == ptr;
  }

  void drain_deferred_locked(size_t max_to_drain) { 
    size_t drained = 0;
    void *deferred_ptr = nullptr;
    while (drained < max_to_drain && deferred_frees.pop(&deferred_ptr)) {
      page_status_t before = EMPTY;
      page_status_t after = EMPTY;
      (void)free_local(deferred_ptr, nullptr, &before, &after);
      drained++;
    }
  }

public:
  Page()
      : owner_segment(nullptr), owner_segment_idx(0), base(nullptr), size_class(PAGE_SM),
        page_span(0), chunk_stride(0), chunk_usable(0), capacity(0), used(0),
        first_hint(0), owner_tid(0), status(EMPTY), initialized(false), used_bitmap(),
        deferred_frees() {}

  void set_owner_segment(Segment *seg, size_t seg_idx) {
    owner_segment = seg;
    owner_segment_idx = seg_idx;
  }

  bool init(void *page_base, page_kind_t kind, size_t req_sz) {
    if (!page_base || req_sz == 0)
      return false;

    const size_t span = page_size_for_kind(kind);
    size_t stride = 0;
    size_t cap = 0;
    if (kind == PAGE_LG) {
      // keep large class geometry fixed: one chunk per 4MiB page
      // avoids reinitializing metadata on every varying large request
      stride = span;
      cap = 1;
    } else {
      const size_t norm_req = norm_chunk_req(kind, req_sz);
      stride = align_up(norm_req + sizeof(ChunkHeader), 16);
      if (stride == 0)
        return false;
      cap = span / stride;
    }
    if (cap == 0 || cap > UINT32_MAX)
      return false;

    base = page_base;
    size_class = kind;
    page_span = span;
    chunk_stride = stride;
    chunk_usable = stride - sizeof(ChunkHeader);
    capacity = static_cast<uint32_t>(cap);
    used = 0;
    first_hint = 0;
    owner_tid = 0;
    status = EMPTY;
    initialized = true;

    used_bitmap.assign((capacity + 63U) / 64U, 0);
    return true;
  }

  bool retune_if_empty(size_t req_sz) {
    if (!initialized)
      return false;
    if (used != 0)
      return false;
    if (req_sz == 0)
      return false;
    // keep same page base and class - only retune chunk geometry(size)
    return init(base, size_class, req_sz);
  }

  bool is_initialized() const { return initialized; }
  bool can_hold(size_t req) const { return initialized && req <= chunk_usable; }
  bool has_free() const { return initialized && used < capacity; }
  bool is_full() const { return initialized && used == capacity; }

  bool contains_ptr(void *ptr) const {
    if (!initialized || !ptr)
      return false;
    const uintptr_t p = reinterpret_cast<uintptr_t>(ptr);
    const uintptr_t b = reinterpret_cast<uintptr_t>(base);
    return p >= b && p < (b + page_span);
  }

  void *allocate(size_t req, page_status_t *before, page_status_t *after) {
    if (!can_hold(req) || used == capacity)
      return nullptr;

    if (deferred_frees.approx_size() >= 32) {
      drain_deferred_locked(16);
    }

    *before = status;

    const uint32_t start = first_hint;
    const uint32_t words = static_cast<uint32_t>(used_bitmap.size());
    if (words == 0)
      return nullptr;

    uint32_t word_idx = start >> 6; // div by 64 bitmap idx
    const uint32_t start_word = word_idx;
    for (;;) {
      uint64_t word = used_bitmap[word_idx];
      if (~word != 0ULL) {
        uint32_t bit = static_cast<uint32_t>(__builtin_ctzll(~word));
        uint32_t slot = (word_idx << 6) + bit;
        if (slot < capacity) {
          bit_set(slot);
          used++;
          first_hint = slot;
          if (owner_tid == 0)
            owner_tid = current_tid();
          status = (used == capacity) ? FULL : ACTIVE;

          ChunkHeader *hdr = slot_header(slot);
          hdr->owner = this;
          hdr->slot = slot;
          hdr->magic = CHUNK_MAGIC;

          *after = status;
          return slot_user_ptr(slot);
        }
      }
      word_idx = (word_idx + 1) % words;
      if (word_idx == start_word)
        break;
    }

    status = FULL;
    *after = status;
    return nullptr;
  }

  bool free_local(void *ptr, size_t *usable_out, page_status_t *before,
                  page_status_t *after) {
    if (!contains_ptr(ptr))
      return false;

    ChunkHeader *hdr = user_to_header(ptr);
    if (!validate_header(ptr, hdr))
      return false;

    *before = status;
    const uint32_t slot = hdr->slot;
    if (!bit_is_set(slot))
      std::abort();

    if (g_zero_on_free.load(std::memory_order_relaxed)) {
      std::memset(ptr, 0, chunk_usable);
    }

    bit_clear(slot);
    used--;
    if (slot < first_hint)
      first_hint = slot;

    status = (used == 0) ? EMPTY : ACTIVE;
    *after = status;

    if (usable_out)
      *usable_out = chunk_usable;
    return true;
  }

  bool enqueue_deferred_free(void *ptr, size_t *usable_out) {
    if (!contains_ptr(ptr))
      return false;

    ChunkHeader *hdr = user_to_header(ptr);
    if (!validate_header(ptr, hdr))
      return false;

    if (usable_out)
      *usable_out = chunk_usable;
    return deferred_frees.push(ptr);
  }

  size_t usable_size(void *ptr) const {
    if (!contains_ptr(ptr))
      return 0;

    ChunkHeader *hdr = user_to_header(ptr);
    if (!validate_header(ptr, hdr))
      return 0;

    if (g_uaf_check.load(std::memory_order_relaxed)) {
      if (!bit_is_set(hdr->slot))
        std::abort();
    }

    return chunk_usable;
  }

  page_kind_t get_size_class() const { return size_class; }
  page_status_t get_status() const { return status; }
  size_t get_chunk_usable() const { return chunk_usable; }
  pid_t get_owner_tid() const { return owner_tid; }
  uintptr_t get_base_addr() const { return reinterpret_cast<uintptr_t>(base); }
  size_t get_span_size() const { return page_span; }
  size_t get_segment_index() const { return owner_segment_idx; }
  Segment *get_owner_segment() const { return owner_segment; }
};

class Segment {
private:
  void *base;
  page_kind_t size_class;
  size_t page_size;
  size_t page_count;
  std::unique_ptr<Page[]> pages;
  std::atomic<size_t> next_candidate_idx;
  std::atomic<uint32_t> full_pages;
  std::atomic<bool> queued_non_full;
  std::atomic<bool> fixed_chunk_set;
  std::atomic<size_t> fixed_chunk_usable;
  uint64_t key;
  uint64_t canary;

public:
  Segment()
      : base(nullptr), size_class(PAGE_SM), page_size(0), page_count(0), pages(),
        next_candidate_idx(0), full_pages(0), queued_non_full(false),
        fixed_chunk_set(false), fixed_chunk_usable(0), key(0), canary(0) {}

  bool init(void *segment_base, page_kind_t kind, size_t seg_idx) {
    if (!segment_base)
      return false;

    base = segment_base;
    size_class = kind;
    page_size = page_size_for_kind(kind);
    page_count = SEGMENT_SIZE / page_size;
    if (page_count == 0)
      return false;

    pages.reset(new Page[page_count]);
    for (size_t i = 0; i < page_count; ++i) {
      pages[i].set_owner_segment(this, seg_idx);
    }

    next_candidate_idx.store(0, std::memory_order_relaxed);
    full_pages.store(0, std::memory_order_relaxed);
    queued_non_full.store(false, std::memory_order_relaxed);
    fixed_chunk_set.store(false, std::memory_order_relaxed);
    fixed_chunk_usable.store(0, std::memory_order_relaxed);

    key = generate_canary();
    canary = key;
    return true;
  }

  page_kind_t get_size_class() const { return size_class; }
  bool check_canary(uint64_t expected) const { return canary == expected; }
  uint64_t get_key() const { return key; }
  size_t num_pages() const { return page_count; }

  bool contains(void *ptr) const {
    const uintptr_t b = reinterpret_cast<uintptr_t>(base);
    const uintptr_t p = reinterpret_cast<uintptr_t>(ptr);
    return p >= b && p < (b + SEGMENT_SIZE);
  }

  bool has_free_pages() const {
    return full_pages.load(std::memory_order_relaxed) < page_count;
  }

  bool can_hold_request(size_t req) const {
    if (size_class == PAGE_LG)
      return true;
    if (!fixed_chunk_set.load(std::memory_order_relaxed))
      return true;
    return req <= fixed_chunk_usable.load(std::memory_order_relaxed);
  }

  bool try_mark_enqueued() {
    bool expected = false;
    return queued_non_full.compare_exchange_strong(
        expected, true, std::memory_order_acq_rel, std::memory_order_relaxed);
  }

  void clear_enqueued() { queued_non_full.store(false, std::memory_order_release); }

  void *allocate(size_t req, Page **page_out) {
    if (!page_out)
      return nullptr;
    if (!can_hold_request(req))
      return nullptr;
    const bool mt = g_live_threads.load(std::memory_order_relaxed) > 1;

    const size_t start = next_candidate_idx.load(std::memory_order_relaxed) % page_count;
    for (size_t step = 0; step < page_count; ++step) {
      const size_t idx = (start + step) % page_count;
      Page &page = pages[idx];
      void *out = nullptr;
      page_status_t before = EMPTY;
      page_status_t after = EMPTY;
      if (mt) {
        std::lock_guard<std::mutex> lk(page_lock_for(&page));
        const size_t target_req =
            (size_class != PAGE_LG && fixed_chunk_set.load(std::memory_order_relaxed))
                ? fixed_chunk_usable.load(std::memory_order_relaxed)
                : req;
        if (!page.is_initialized()) {
          void *page_base = static_cast<void *>(static_cast<char *>(base) + idx * page_size);
          if (!page.init(page_base, size_class, target_req))
            continue;
          if (size_class != PAGE_LG &&
              !fixed_chunk_set.load(std::memory_order_relaxed)) {
            fixed_chunk_usable.store(page.get_chunk_usable(), std::memory_order_relaxed);
            fixed_chunk_set.store(true, std::memory_order_relaxed);
          }
        }
        if (!page.can_hold(req)) {
          if (size_class != PAGE_LG || !page.retune_if_empty(req) || !page.can_hold(req))
            continue;
        }

        out = page.allocate(req, &before, &after); // dispatch to page lvl alloc
      } else {
        const size_t target_req =
            (size_class != PAGE_LG && fixed_chunk_set.load(std::memory_order_relaxed))
                ? fixed_chunk_usable.load(std::memory_order_relaxed)
                : req;
        if (!page.is_initialized()) {
          void *page_base = static_cast<void *>(static_cast<char *>(base) + idx * page_size);
          if (!page.init(page_base, size_class, target_req))
            continue;
          if (size_class != PAGE_LG &&
              !fixed_chunk_set.load(std::memory_order_relaxed)) {
            fixed_chunk_usable.store(page.get_chunk_usable(), std::memory_order_relaxed);
            fixed_chunk_set.store(true, std::memory_order_relaxed);
          }
        }
        if (!page.can_hold(req)) {
          if (size_class != PAGE_LG || !page.retune_if_empty(req) || !page.can_hold(req))
            continue;
        }

        out = page.allocate(req, &before, &after);
      }

      if (!out)
        continue;

      if (before != FULL && after == FULL) {
        full_pages.fetch_add(1, std::memory_order_relaxed);
      }

      next_candidate_idx.store((after == FULL) ? ((idx + 1) % page_count) : idx,
                               std::memory_order_relaxed);
      *page_out = &page;
      return out;
    }

    return nullptr;
  }

  bool free_on_page(Page *page, void *ptr, size_t *usable_out, page_status_t *before,
                    page_status_t *after) {
    if (!page)
      return false;

    const bool mt = g_live_threads.load(std::memory_order_relaxed) > 1;
    bool ok = false;
    if (mt) {
      std::lock_guard<std::mutex> lk(page_lock_for(page));
      ok = page->free_local(ptr, usable_out, before, after);
    } else {
      ok = page->free_local(ptr, usable_out, before, after);
    }
    if (!ok)
      return false;

    if (*before == FULL && *after != FULL) {
      full_pages.fetch_sub(1, std::memory_order_relaxed);
    }
    return true;
  }
};

typedef struct tc_page_s {
  Page *loc;
  page_kind_t kind;
  size_t size;
  void *freelist;
} tc_page_t;

class ThreadCache {
private:
  static std::atomic<uint32_t> live_threads;
  pid_t tid;
  bool is_active;
  std::vector<tc_page_t *> pages;
  Page *cached_pages[3];
  uintptr_t cached_page_bases[3];
  uintptr_t cached_page_ends[3];
  size_t preferred_seg_idx[3];
  bool preferred_seg_valid[3];

public:
  ThreadCache()
      : tid(current_tid()), is_active(true), pages(),
        cached_pages{nullptr, nullptr, nullptr}, cached_page_bases{0, 0, 0},
        cached_page_ends{0, 0, 0}, preferred_seg_idx{0, 0, 0},
        preferred_seg_valid{false, false, false} {
    live_threads.fetch_add(1, std::memory_order_relaxed);
    g_live_threads.fetch_add(1, std::memory_order_relaxed);
  }

  ~ThreadCache() {
    live_threads.fetch_sub(1, std::memory_order_relaxed);
    g_live_threads.fetch_sub(1, std::memory_order_relaxed);
  }

  static ThreadCache *current() {
    static thread_local ThreadCache instance;
    return &instance;
  }

  static bool is_multi_threaded() {
    return live_threads.load(std::memory_order_relaxed) > 1;
  }

  pid_t get_tid() const { return tid; }
  bool get_active() const { return is_active; }

  Page *get_cached_page(page_kind_t kind) const {
    if (kind > PAGE_LG)
      return nullptr;
    return cached_pages[class_index_for_kind(kind)];
  }

  void cache_page(page_kind_t kind, Page *page, uintptr_t page_base, size_t page_size) {
    if (kind > PAGE_LG)
      return;
    const size_t idx = class_index_for_kind(kind);
    cached_pages[idx] = page;
    cached_page_bases[idx] = page_base;
    cached_page_ends[idx] = page_base + page_size;
  }

  void clear_cached_page(page_kind_t kind, Page *page) {
    if (kind > PAGE_LG)
      return;
    const size_t idx = class_index_for_kind(kind);
    if (cached_pages[idx] == page) {
      cached_pages[idx] = nullptr;
      cached_page_bases[idx] = 0;
      cached_page_ends[idx] = 0;
    }
  }

  bool get_preferred_segment(page_kind_t kind, size_t *idx_out) const {
    if (!idx_out || kind > PAGE_LG)
      return false;
    const size_t idx = class_index_for_kind(kind);
    if (!preferred_seg_valid[idx])
      return false;
    *idx_out = preferred_seg_idx[idx];
    return true;
  }

  void set_preferred_segment(page_kind_t kind, size_t seg_idx) {
    if (kind > PAGE_LG)
      return;
    const size_t idx = class_index_for_kind(kind);
    preferred_seg_idx[idx] = seg_idx;
    preferred_seg_valid[idx] = true;
  }

};

std::atomic<uint32_t> ThreadCache::live_threads{0};

struct ClassShard {
  std::mutex 					mu;
  std::vector<size_t> segments;
  std::deque<size_t> 	non_full_segments;
};


class HeapState {
private:
  memid_t memid;
  void *base;
  size_t reserved_size;
  uint32_t num_segments;
  std::vector<std::unique_ptr<Segment>> layout;
  std::vector<segment_kind_t> seg_kind;
  std::vector<void *> seg_bases;
  std::vector<page_kind_t> seg_page_kind;
  memkind_t mem_kind;
  uint64_t canary;
  size_t reserved_cursor;
  std::mutex heap_mu;
  std::array<ClassShard, 3> class_shards;

  ClassShard &shard_for(page_kind_t kind) {
    return class_shards[class_index_for_kind(kind)];
  }

  void enqueue_non_full_segment(page_kind_t kind, size_t seg_idx) {
    if (seg_idx >= layout.size())
      return;
    Segment *seg = layout[seg_idx].get();
    if (!seg || !seg->has_free_pages())
      return;
    if (!seg->try_mark_enqueued())
      return;

    ClassShard &shard = shard_for(kind);
    std::lock_guard<std::mutex> lk(shard.mu);
    shard.non_full_segments.push_back(seg_idx);
  }

  bool add_segment_nolock(void *segment_base, segment_kind_t kind, page_kind_t page_kind) {
    if (!segment_base)
      return false;

    const size_t idx = layout.size();
    auto seg = std::make_unique<Segment>();
    if (!seg->init(segment_base, page_kind, idx))
      return false;

    layout.push_back(std::move(seg));
    seg_kind.push_back(kind);
    seg_bases.push_back(segment_base);
    seg_page_kind.push_back(page_kind);
    num_segments = static_cast<uint32_t>(layout.size());

    ClassShard &shard = shard_for(page_kind);
    {
      std::lock_guard<std::mutex> lk(shard.mu);
      shard.segments.push_back(idx);
    }
    enqueue_non_full_segment(page_kind, idx);

    if (!base || reinterpret_cast<uintptr_t>(segment_base) < reinterpret_cast<uintptr_t>(base))
      base = segment_base;

    return true;
  }

  bool add_segment_from_reserved_nolock(segment_kind_t kind, page_kind_t page_kind) {
    if (!base || reserved_size == 0)
      return false;
    if (reserved_cursor + SEGMENT_SIZE > reserved_size)
      return false;

    void *seg_base = static_cast<void *>(static_cast<char *>(base) + reserved_cursor);
    if (!commit_region(seg_base, SEGMENT_SIZE))
      return false;

    reserved_cursor += SEGMENT_SIZE;
    return add_segment_nolock(seg_base, kind, page_kind);
  }

  void *alloc_xl(size_t size) {
    if (size >= (SIZE_MAX - 4096))
      return nullptr;
    if (size > HEAP_RESERVED_DEFAULT)
      return nullptr;

    const size_t usable = align_up(size, 16);
    const size_t map_size = align_up(usable + sizeof(XLHeader), 4096);
    void *raw = alloc_segment(map_size);
    if (!raw)
      return nullptr;

    auto *hdr = static_cast<XLHeader *>(raw);
    hdr->magic = XL_MAGIC;
    hdr->mapping_size = map_size;
    hdr->usable_size = map_size - sizeof(XLHeader);
    hdr->reserved = 0;
    g_last_alloc_usable = hdr->usable_size;

    return static_cast<void *>(reinterpret_cast<char *>(raw) + sizeof(XLHeader));
  }

  bool free_xl(void *ptr, size_t *usable_out) {
    if (!ptr)
      return false;
    auto *hdr = reinterpret_cast<XLHeader *>(static_cast<char *>(ptr) - sizeof(XLHeader));
    if (hdr->magic != XL_MAGIC)
      return false;

    if (g_zero_on_free.load(std::memory_order_relaxed)) {
      std::memset(ptr, 0, hdr->usable_size);
    }
    if (usable_out)
      *usable_out = hdr->usable_size;

    free_segment(hdr, hdr->mapping_size);
    return true;
  }

  size_t usable_xl(void *ptr) {
    if (!ptr)
      return 0;
    auto *hdr = reinterpret_cast<XLHeader *>(static_cast<char *>(ptr) - sizeof(XLHeader));
    if (hdr->magic != XL_MAGIC)
      return 0;
    return hdr->usable_size;
  }

public:
  HeapState()
      : memid(), base(nullptr), reserved_size(0), num_segments(0), layout(),
        seg_kind(), seg_bases(), seg_page_kind(), mem_kind(MEM_NONE), canary(0),
        reserved_cursor(0), heap_mu(), class_shards() {}

  static HeapState &instance() {
    static HeapState heap;
    return heap;
  }

  bool init_reserved(void *reserved_base, size_t size) {
    if (!reserved_base || size == 0)
      return false;

    std::lock_guard<std::mutex> lk(heap_mu);
    base = reserved_base;
    reserved_size = size;
    reserved_cursor = 0;
    canary = generate_canary();
    mem_kind = MEM_OS;

    const size_t cap = size / SEGMENT_SIZE;
    layout.reserve(cap);
    seg_kind.reserve(cap);
    seg_bases.reserve(cap);
    seg_page_kind.reserve(cap);

    for (ClassShard &shard : class_shards) {
      std::lock_guard<std::mutex> shard_lk(shard.mu);
      shard.segments.clear();
      shard.non_full_segments.clear();
    }

    return true;
  }

  bool add_segment(void *segment_base, segment_kind_t kind, page_kind_t page_kind) {
    std::lock_guard<std::mutex> lk(heap_mu);
    return add_segment_nolock(segment_base, kind, page_kind);
  }

  bool add_segment_from_reserved(segment_kind_t kind, page_kind_t page_kind) {
    std::lock_guard<std::mutex> lk(heap_mu);
    return add_segment_from_reserved_nolock(kind, page_kind);
  }

  void *allocate(size_t size) {
    g_last_alloc_usable = 0;
    ThreadCache *tc = ThreadCache::current();
    const bool mt = ThreadCache::is_multi_threaded();

    page_kind_t kind = class_for_size(size);
    if (kind == PAGE_XL) {
      // goto xl path?
      const size_t large_fit_limit = LARGE_PAGE_SIZE - sizeof(ChunkHeader);
      if (size <= large_fit_limit) {
        kind = PAGE_LG;
      } else {
        return alloc_xl(size);
      }
    }

    const size_t need = align_up(size, 16);

    // ideal path - try thread-local cached page first
    if (tc->get_active()) {
      Page *cached = tc->get_cached_page(kind);
      if (cached) {
        void *fast = nullptr;
        page_status_t before = EMPTY;
        page_status_t after = EMPTY;
        if (mt) {
          std::lock_guard<std::mutex> lk(page_lock_for(cached));
          if (!cached->is_initialized()) {
            tc->clear_cached_page(kind, cached);
          } else {
            fast = cached->allocate(need, &before, &after);
          }
        } else {
          if (cached->is_initialized())
            fast = cached->allocate(need, &before, &after);
          else
            tc->clear_cached_page(kind, cached);
        }
        if (fast) {
          g_last_alloc_usable = cached->get_chunk_usable();
          return fast;
        }
      }
    }

    auto try_segment = [&](size_t seg_idx) -> void * {
      if (seg_idx >= layout.size())
        return nullptr;
      Segment *seg = layout[seg_idx].get();
      if (!seg || seg->get_size_class() != kind)
        return nullptr;
      if (!seg->can_hold_request(need))
        return nullptr;

      Page *page = nullptr;
      void *ptr = seg->allocate(need, &page);
      if (seg->has_free_pages() && seg->can_hold_request(need))
        enqueue_non_full_segment(kind, seg_idx);

      if (!ptr) {
        return nullptr;
      }

      if (tc->get_active()) {
        tc->set_preferred_segment(kind, seg_idx);
        if (page)
          tc->cache_page(kind, page, page->get_base_addr(), page->get_span_size());
      }
      if (page)
        g_last_alloc_usable = page->get_chunk_usable();
      return ptr;
    };

    if (tc->get_active()) {
      size_t preferred = 0;
      if (tc->get_preferred_segment(kind, &preferred)) {
        if (void *ptr = try_segment(preferred))
          return ptr;
      }
    }

    // shard queue of known non-full segments
    {
      ClassShard &shard = shard_for(kind);
      size_t probes = 0;
      while (probes < MAX_QUEUE_PROBES_PER_ALLOC) {
        size_t idx = SIZE_MAX;
        {
          std::lock_guard<std::mutex> lk(shard.mu);
          if (shard.non_full_segments.empty())
            break;
          idx = shard.non_full_segments.front();
          shard.non_full_segments.pop_front();
        }
        probes++;

        if (idx >= layout.size())
          continue;
        Segment *seg = layout[idx].get();
        if (!seg || seg->get_size_class() != kind)
          continue;
        seg->clear_enqueued();

        if (void *ptr = try_segment(idx))
          return ptr;
      }
    }

    // snapshot all segments in class and try a subset
    std::vector<size_t> candidates;
    {
      ClassShard &shard = shard_for(kind);
      std::lock_guard<std::mutex> lk(shard.mu);
      candidates = shard.segments;
    }

    size_t fallback_scans = 0;
    for (size_t idx : candidates) {
      if (fallback_scans >= MAX_FALLBACK_SCANS_PER_ALLOC)
        break;
      fallback_scans++;
      if (void *ptr = try_segment(idx))
        return ptr;
    }

    // grow from reserved heap (ideal) instead of mmaping more mem to expand.
    {
      std::lock_guard<std::mutex> lk(heap_mu);
      if (add_segment_from_reserved_nolock(SEGMENT_NORM, kind)) {
        return try_segment(layout.size() - 1);
      }
    }

    void *seg_mem = alloc_segment(SEGMENT_SIZE);
    if (!seg_mem)
      return nullptr;

    {
      std::lock_guard<std::mutex> lk(heap_mu);
      if (!add_segment_nolock(seg_mem, SEGMENT_NORM, kind)) {
        free_segment(seg_mem, SEGMENT_SIZE);
        return nullptr;
      }
      return try_segment(layout.size() - 1);
    }
  }

  bool free_ptr(void *ptr, size_t *usable_out) {
    if (!ptr)
      return true;

    ThreadCache *tc = ThreadCache::current();

    ChunkHeader *chdr = reinterpret_cast<ChunkHeader *>(
        static_cast<char *>(ptr) - sizeof(ChunkHeader));
    if (chdr->magic == CHUNK_MAGIC && chdr->owner) {
      Page *page = chdr->owner;
      Segment *seg = page->get_owner_segment();
      if (!seg)
        return false;

      const page_kind_t kind = page->get_size_class();
      const pid_t owner_tid = page->get_owner_tid();
      const bool remote_owner = (owner_tid != 0 && owner_tid != tc->get_tid());

      bool ok = false;
      page_status_t before = EMPTY;
      page_status_t after = EMPTY;

      if (remote_owner) {
        ok = page->enqueue_deferred_free(ptr, usable_out);
        if (!ok) {
          ok = seg->free_on_page(page, ptr, usable_out, &before, &after);
        }
      } else {
        ok = seg->free_on_page(page, ptr, usable_out, &before, &after);
      }

      if (!ok)
        std::abort();

      if (!remote_owner && before == FULL && after != FULL) {
        enqueue_non_full_segment(kind, page->get_segment_index());
      }

      if (tc->get_active()) {
        tc->cache_page(kind, page, page->get_base_addr(), page->get_span_size());
        if (page->get_status() == EMPTY)
          tc->clear_cached_page(kind, page);
      }

      return true;
    }

    if (free_xl(ptr, usable_out))
      return true;

    return false;
  }

  size_t usable_size(void *ptr) {
    if (!ptr)
      return 0;

    ChunkHeader *chdr = reinterpret_cast<ChunkHeader *>(
        static_cast<char *>(ptr) - sizeof(ChunkHeader));
    if (chdr->magic == CHUNK_MAGIC && chdr->owner)
      return chdr->owner->usable_size(ptr);

    return usable_xl(ptr);
  }

  std::vector<segment_kind_t> get_segment_kinds() { return seg_kind; }
  uint32_t get_num_segments() { return num_segments; }

  bool is_corrupted() {
    if (canary == 0)
      return true;

    for (const auto &seg : layout) {
      if (!seg || !seg->check_canary(seg->get_key()))
        return true;
    }
    return false;
  }

  bool validate() {
    if (is_corrupted())
      return false;

    for (const auto &seg : layout) {
      if (!seg || seg->num_pages() == 0)
        return false;
    }
    return true;
  }

  void clear_metadata() {
    std::lock_guard<std::mutex> lk(heap_mu);
    if (base && reserved_size > 0) {
      free_segment(base, reserved_size);
    } else {
      for (void *seg : seg_bases)
        free_segment(seg, SEGMENT_SIZE);
    }

    layout.clear();
    seg_kind.clear();
    seg_bases.clear();
    seg_page_kind.clear();

    for (ClassShard &shard : class_shards) {
      std::lock_guard<std::mutex> shard_lk(shard.mu);
      shard.segments.clear();
      shard.non_full_segments.clear();
    }

    base = nullptr;
    reserved_size = 0;
    num_segments = 0;
    canary = 0;
    mem_kind = MEM_NONE;
    reserved_cursor = 0;
  }
};

} // namespace

bool heap_register_segment(void *segment_base) {
  return HeapState::instance().add_segment(segment_base, SEGMENT_NORM, PAGE_SM);
}

void heap_clear_metadata() { HeapState::instance().clear_metadata(); }

bool heap_init_reserved(void *reserved_base, size_t size) {
  return HeapState::instance().init_reserved(reserved_base, size);
}

bool heap_add_segment_from_reserved(segment_kind_t kind) {
  return HeapState::instance().add_segment_from_reserved(kind, PAGE_SM);
}

bool heap_add_segment_for_class(page_kind_t kind) {
  return HeapState::instance().add_segment_from_reserved(SEGMENT_NORM, kind);
}

void *heap_alloc(size_t size) { return HeapState::instance().allocate(size); }

size_t heap_last_alloc_usable() { return g_last_alloc_usable; }

size_t heap_usable_size(void *ptr) { return HeapState::instance().usable_size(ptr); }

bool free_dispatch_with_size(void *ptr, size_t *usable_size) {
  return HeapState::instance().free_ptr(ptr, usable_size);
}

void set_zero_on_free_enabled(bool enabled) {
  g_zero_on_free.store(enabled, std::memory_order_relaxed);
}

void set_uaf_check_enabled(bool enabled) {
  g_uaf_check.store(enabled, std::memory_order_relaxed);
}

bool heap_validate() { return HeapState::instance().validate(); }

} // namespace zialloc::memory
