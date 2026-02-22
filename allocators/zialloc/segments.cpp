#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <atomic>
#include <mutex>
#include <new>
#include <unordered_map>
#include <vector>

#include "types.h"
#include "mem.h"
#include "zialloc_memory.hpp"

namespace zialloc::memory {

namespace {

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

struct PageRuntime {
  void *base;
  size_t chunk_size;
  size_t capacity;
  size_t used;
  std::vector<Chunk> chunks;
  std::vector<uint32_t> freelist;
  bool initialized;

  PageRuntime()
      : base(nullptr), chunk_size(0), capacity(0), used(0), initialized(false) {}
};

struct SegmentRuntime {
  void *base;
  page_kind_t page_kind;
  size_t page_size;
  size_t page_count;
  std::vector<PageRuntime> pages;

  SegmentRuntime()
      : base(nullptr), page_kind(PAGE_SM), page_size(0), page_count(0), pages() {}
};

} // namespace

class Page;
class Segment;
class Heap;

class Chunk {
	private:
		Page* page;
		uint32_t data_size : 31;
		uint32_t in_use    : 1;
		void* data;

	public:
		Chunk() : page(nullptr), data_size(0), in_use(0), data(nullptr) {}
		Chunk(Page* owning_page, void* data_ptr, size_t size)
			: page(owning_page), data_size(static_cast<uint32_t>(size)), in_use(0), data(data_ptr) {}

		Page* get_page() const { return page; }
		bool is_in_use() const { return in_use != 0; }
		void* get_data() const { return data; }
		size_t get_data_size() const { return data_size; }
		void mark_used() { in_use = 1; }
		void mark_free() { in_use = 0; }
};

class GuardChunk {
	private:
		int64_t pattern;

	public:
		GuardChunk() : pattern(0) {}
		explicit GuardChunk(int64_t val) : pattern(val) {}
		void set(int64_t val) { pattern = val; }
		bool integrity_check(int64_t expected) const {
			INTEGRITY_CHECK(pattern == expected, "guard mismatch");
			return true;
		}
};

typedef struct tc_page_s {
  Page*       loc;
  page_kind_t kind;
  size_t      size;
  void*       freelist;
} tc_page_t;

class ThreadCache {
	private:
		static std::atomic<uint32_t> live_threads;
		pid_t tid;
		bool is_active;
		std::vector<tc_page_t*> pages;
		Page* cached_pages[3];
		uintptr_t cached_page_bases[3];
		uintptr_t cached_page_ends[3];

	public:
		ThreadCache()
				: tid(current_tid()), is_active(true), pages(),
					cached_pages{nullptr, nullptr, nullptr},
					cached_page_bases{0, 0, 0},
					cached_page_ends{0, 0, 0} {
			live_threads.fetch_add(1, std::memory_order_relaxed);
		}
		~ThreadCache() {
			live_threads.fetch_sub(1, std::memory_order_relaxed);
		}
		static ThreadCache* current() {
			static thread_local ThreadCache instance;
			return &instance;
		}
		static bool is_multi_threaded() {
			return live_threads.load(std::memory_order_relaxed) > 1;
		}
		pid_t get_tid() const { return tid; }
		bool get_active() const { return is_active; }
		void set_active(bool active) { is_active = active; }
		void add_page(tc_page_t* page) { pages.push_back(page); }
		std::vector<tc_page_t*>& get_pages() { return pages; }

		Page* get_cached_page(page_kind_t kind) const {
			if (kind > PAGE_LG)
				return nullptr;
			return cached_pages[static_cast<size_t>(kind)];
		}

		void cache_page(page_kind_t kind, Page* page, uintptr_t page_base, size_t page_size) {
			if (kind > PAGE_LG)
				return;
			size_t idx = static_cast<size_t>(kind);
			cached_pages[idx] = page;
			cached_page_bases[idx] = page_base;
			cached_page_ends[idx] = page_base + page_size;
		}

		void clear_cached_page(page_kind_t kind, Page* page) {
			if (kind > PAGE_LG)
				return;
			size_t idx = static_cast<size_t>(kind);
			if (cached_pages[idx] == page) {
				cached_pages[idx] = nullptr;
				cached_page_bases[idx] = 0;
				cached_page_ends[idx] = 0;
			}
		}

		bool lookup_cached_for_ptr(void* ptr, page_kind_t* kind_out, Page** page_out) const {
			if (!ptr || !kind_out || !page_out)
				return false;
			const uintptr_t p = reinterpret_cast<uintptr_t>(ptr);
			for (int k = PAGE_SM; k <= PAGE_LG; ++k) {
				const size_t idx = static_cast<size_t>(k);
				Page* page = cached_pages[idx];
				if (page && p >= cached_page_bases[idx] && p < cached_page_ends[idx]) {
					*kind_out = static_cast<page_kind_t>(k);
					*page_out = page;
					return true;
				}
			}
			return false;
		}
};

std::atomic<uint32_t> ThreadCache::live_threads{0};

class Page {
	private:
		ThreadCache* owner_tc;
		page_kind_t size_class;
		uint32_t slot_count;
		uint8_t is_committed : 1;
		uint16_t num_committed;
		uint16_t reserved;
		uint16_t capacity;
		bool use_bitmap;
		std::vector<Chunk*> freelist;
		uint16_t used;
		size_t chunk_sz;
		Chunk* page_start;
		GuardChunk front_guard;
		GuardChunk back_guard;
		page_status_t status;
		uint64_t prng_state;
		PageRuntime runtime;

	public:
		Page()
				: owner_tc(nullptr), size_class(PAGE_SM), slot_count(0), is_committed(0),
					num_committed(0), reserved(0), capacity(0), use_bitmap(true),
					freelist(), used(0), chunk_sz(0), page_start(nullptr),
					front_guard(), back_guard(), status(EMPTY), prng_state(0), runtime() {}

		bool init(void* base, page_kind_t kind, size_t chunk_size, int64_t canary) {
			if (!base || chunk_size == 0)
				return false;

			runtime.base = base;
			runtime.chunk_size = align_up(chunk_size, 16);
			runtime.capacity = page_size_for_kind(kind) / runtime.chunk_size;
			runtime.used = 0;
			runtime.initialized = runtime.capacity > 0;
			if (!runtime.initialized)
				return false;

			owner_tc = nullptr;
			size_class = kind;
			slot_count = static_cast<uint32_t>(runtime.capacity);
			is_committed = 1;
			num_committed = static_cast<uint16_t>(runtime.capacity);
			reserved = static_cast<uint16_t>(runtime.capacity);
			capacity = static_cast<uint16_t>(runtime.capacity);
			use_bitmap = true;
			used = 0;
			chunk_sz = runtime.chunk_size;
			status = EMPTY;
			prng_state = generate_canary();
			front_guard.set(canary);
			back_guard.set(canary);

			runtime.chunks.clear();
			runtime.freelist.clear();
			runtime.chunks.reserve(runtime.capacity);
			runtime.freelist.reserve(runtime.capacity);
			freelist.clear();
			freelist.reserve(runtime.capacity);

			for (size_t i = 0; i < runtime.capacity; ++i) {
				void* ptr = static_cast<void*>(static_cast<char*>(runtime.base) + i * runtime.chunk_size);
				runtime.chunks.emplace_back(this, ptr, runtime.chunk_size);
				runtime.freelist.push_back(static_cast<uint32_t>(i));
				freelist.push_back(&runtime.chunks.back());
			}
			page_start = runtime.chunks.empty() ? nullptr : &runtime.chunks.front();
			return true;
		}

		bool is_initialized() const { return runtime.initialized; }
		bool contains(void* ptr) const {
			if (!runtime.initialized || !ptr)
				return false;
			uintptr_t p = reinterpret_cast<uintptr_t>(ptr);
			uintptr_t b = reinterpret_cast<uintptr_t>(runtime.base);
			uintptr_t e = b + page_size_for_kind(size_class);
			return p >= b && p < e;
		}

		bool can_hold(size_t req) const {
			return runtime.initialized && req <= runtime.chunk_size;
		}

		void* allocate(size_t req) {
			if (!can_hold(req) || runtime.freelist.empty())
				return nullptr;
			uint32_t idx = runtime.freelist.back();
			runtime.freelist.pop_back();
			if (idx >= runtime.chunks.size())
				return nullptr;
			Chunk* c = &runtime.chunks[idx];
			c->mark_used();
			used = static_cast<uint16_t>(runtime.used + 1);
			runtime.used++;
			status = (runtime.used == runtime.capacity) ? FULL : ACTIVE;
			return c->get_data();
		}

		bool free_ptr(void* ptr, size_t* usable_out) {
			if (!contains(ptr))
				return false;
			uintptr_t off = reinterpret_cast<uintptr_t>(ptr) - reinterpret_cast<uintptr_t>(runtime.base);
			size_t idx = off / runtime.chunk_size;
			if (idx >= runtime.chunks.size())
				return false;
			Chunk& c = runtime.chunks[idx];
			if (!c.is_in_use())
				std::abort();
			c.mark_free();
			runtime.freelist.push_back(static_cast<uint32_t>(idx));
			runtime.used--;
			used = static_cast<uint16_t>(runtime.used);
			if (usable_out)
				*usable_out = runtime.chunk_size;
			status = (runtime.used == 0) ? EMPTY : ACTIVE;
			return true;
		}

		size_t usable_size(void* ptr) const {
			if (!contains(ptr))
				return 0;
			return runtime.chunk_size;
		}

		page_status_t get_status() const { return status; }
		page_kind_t get_size_class() const { return size_class; }
		size_t get_chunk_size() const { return runtime.chunk_size; }
		uintptr_t get_base_addr() const { return reinterpret_cast<uintptr_t>(runtime.base); }
		size_t get_span_size() const { return page_size_for_kind(size_class); }
		uint16_t get_used() const { return used; }
		void* find_space() { return allocate(runtime.chunk_size); }
};

class Segment {
	private:
		page_kind_t size_class;
		std::vector<Page> slots;
		uint64_t active_bm;
		uint64_t canary;
		uint64_t key;
		SegmentRuntime runtime;

	public:
		Segment() : size_class(PAGE_SM), slots(), active_bm(0), canary(0), key(0), runtime() {}
		~Segment() = default;

		bool init(void* base, page_kind_t kind) {
			if (!base)
				return false;
			runtime.base = base;
			runtime.page_kind = kind;
			runtime.page_size = page_size_for_kind(kind);
			runtime.page_count = SEGMENT_SIZE / runtime.page_size;
			runtime.pages.assign(runtime.page_count, PageRuntime{});

			size_class = kind;
			active_bm = 0;
			key = generate_canary();
			canary = key;
			slots.clear();
			slots.reserve(runtime.page_count);
			for (size_t i = 0; i < runtime.page_count; ++i) {
				slots.emplace_back();
			}
			return true;
		}

		page_kind_t get_size_class() const { return size_class; }
		uint64_t get_key() const { return key; }
		bool check_canary(uint64_t expected) const { return canary == expected; }
		size_t num_pages() const { return slots.size(); }

		bool contains(void* ptr) const {
			uintptr_t b = reinterpret_cast<uintptr_t>(runtime.base);
			uintptr_t p = reinterpret_cast<uintptr_t>(ptr);
			return p >= b && p < (b + SEGMENT_SIZE);
		}

		Page* find_page_for(void* ptr) {
			if (!contains(ptr))
				return nullptr;
			uintptr_t b = reinterpret_cast<uintptr_t>(runtime.base);
			uintptr_t p = reinterpret_cast<uintptr_t>(ptr);
			size_t idx = (p - b) / runtime.page_size;
			if (idx >= slots.size())
				return nullptr;
			return &slots[idx];
		}

		void* allocate(size_t req, int64_t guard_canary) {
			for (size_t i = 0; i < slots.size(); ++i) {
				Page& page = slots[i];
				if (!page.is_initialized()) {
					void* pbase = static_cast<void*>(static_cast<char*>(runtime.base) + (i * runtime.page_size));
					if (!page.init(pbase, size_class, req, guard_canary))
						continue;
				}
				if (!page.can_hold(req))
					continue;
				void* out = page.allocate(req);
				if (!out)
					continue;
				active_bm |= (1ULL << (i % 64));
				return out;
			}
			return nullptr;
		}

		bool free_ptr(void* ptr, size_t* usable_out) {
			Page* page = find_page_for(ptr);
			if (!page)
				return false;
			bool ok = page->free_ptr(ptr, usable_out);
			if (!ok)
				return false;
			if (page->get_status() == EMPTY) {
				uintptr_t b = reinterpret_cast<uintptr_t>(runtime.base);
				uintptr_t p = reinterpret_cast<uintptr_t>(ptr);
				size_t idx = (p - b) / runtime.page_size;
				active_bm &= ~(1ULL << (idx % 64));
			}
			return true;
		}

		size_t usable_size(void* ptr) {
			Page* page = find_page_for(ptr);
			if (!page)
				return 0;
			return page->usable_size(ptr);
		}

		bool is_fully_empty() const {
			for (const Page& p : slots) {
				if (p.is_initialized() && p.get_status() != EMPTY)
					return false;
			}
			return true;
		}
};

class Heap {
private:
  memid_t memid;
  void* base;
  size_t reserved_size;
  uint32_t num_segments;
  std::vector<Segment> layout;
  std::vector<segment_kind_t> seg_kind;
  std::vector<void*> seg_bases;
  std::vector<page_kind_t> seg_page_kind;
  std::unordered_map<uintptr_t, size_t> seg_index_by_base;
  std::vector<void*> xl_ptrs;
  std::vector<size_t> xl_sizes;
  memkind_t mem_kind;
  uint64_t canary;
  size_t reserved_cursor;
  int64_t guard_seed;
  std::mutex op_mu;
  std::mutex meta_mu;

  bool add_segment_nolock(void* segment_base, segment_kind_t kind, page_kind_t page_kind) {
    if (!segment_base)
      return false;
    layout.emplace_back();
    if (!layout.back().init(segment_base, page_kind)) {
      layout.pop_back();
      return false;
    }
    seg_kind.push_back(kind);
    seg_bases.push_back(segment_base);
    seg_page_kind.push_back(page_kind);
    seg_index_by_base[reinterpret_cast<uintptr_t>(segment_base)] = layout.size() - 1;
    num_segments = static_cast<uint32_t>(layout.size());
    if (!base || reinterpret_cast<uintptr_t>(segment_base) < reinterpret_cast<uintptr_t>(base))
      base = segment_base;
    return true;
  }

  bool add_segment_from_reserved_nolock(segment_kind_t kind, page_kind_t page_kind) {
    if (!base || reserved_size == 0)
      return false;
    if (reserved_cursor + SEGMENT_SIZE > reserved_size)
      return false;
    void* seg_base = static_cast<void*>(static_cast<char*>(base) + reserved_cursor);
    if (!commit_region(seg_base, SEGMENT_SIZE))
      return false;
    reserved_cursor += SEGMENT_SIZE;
    return add_segment_nolock(seg_base, kind, page_kind);
  }

  Heap()
      : memid(), base(nullptr), reserved_size(0), num_segments(0), layout(),
        seg_kind(), seg_bases(), seg_page_kind(), seg_index_by_base(),
        xl_ptrs(), xl_sizes(),
        mem_kind(MEM_NONE), canary(0),
        reserved_cursor(0), guard_seed(0) {}
  ~Heap() = default;

public:
  static Heap& instance() {
    static Heap heap;
    return heap;
  }

  bool init_reserved(void* reserved_base, size_t size) {
    if (!reserved_base || size == 0)
      return false;
    std::scoped_lock lk(op_mu, meta_mu);
    base = reserved_base;
    reserved_size = size;
    reserved_cursor = 0;
    canary = generate_canary();
    guard_seed = static_cast<int64_t>(canary);
    mem_kind = MEM_OS;
    size_t cap = size / SEGMENT_SIZE;
    layout.reserve(cap);
    seg_kind.reserve(cap);
    seg_bases.reserve(cap);
    seg_page_kind.reserve(cap);
    seg_index_by_base.clear();
    xl_ptrs.clear();
    xl_sizes.clear();
    return true;
  }

  bool add_segment(void* segment_base, segment_kind_t kind, page_kind_t page_kind) {
    std::scoped_lock lk(op_mu, meta_mu);
    return add_segment_nolock(segment_base, kind, page_kind);
  }

  bool add_segment_from_reserved(segment_kind_t kind, page_kind_t page_kind) {
    std::scoped_lock lk(op_mu, meta_mu);
    return add_segment_from_reserved_nolock(kind, page_kind);
  }

  Segment* find_segment_for(void* ptr) {
    if (!ptr)
      return nullptr;
    uintptr_t masked = reinterpret_cast<uintptr_t>(ptr) & ~SEGMENT_MASK;
    auto it = seg_index_by_base.find(masked);
    if (it != seg_index_by_base.end()) {
      const size_t idx = it->second;
      if (idx < layout.size())
        return &layout[idx];
    }
    for (size_t i = 0; i < layout.size(); ++i) {
      if (layout[i].contains(ptr))
        return &layout[i];
    }
    return nullptr;
  }

  Segment* get_or_create_segment(page_kind_t kind) {
    {
      std::lock_guard<std::mutex> lk(op_mu);
      for (size_t i = 0; i < layout.size(); ++i) {
        if (seg_page_kind[i] == kind)
          return &layout[i];
      }
    }
    {
      std::scoped_lock lk(op_mu, meta_mu);
      if (add_segment_from_reserved_nolock(SEGMENT_NORM, kind))
        return &layout.back();
    }
    void* seg = alloc_segment(SEGMENT_SIZE);
    if (!seg)
      return nullptr;
    {
      std::scoped_lock lk(op_mu, meta_mu);
      if (!add_segment_nolock(seg, SEGMENT_NORM, kind)) {
        free_segment(seg, SEGMENT_SIZE);
        return nullptr;
      }
      return &layout.back();
    }
  }

  void* allocate(size_t size) {
    ThreadCache* tc = ThreadCache::current();
    const bool mt = ThreadCache::is_multi_threaded();
    page_kind_t kind = class_for_size(size);
    if (kind == PAGE_XL) {
      std::scoped_lock lk(op_mu, meta_mu);
      if (size >= (SIZE_MAX - 4096))
        return nullptr;
      if (size > HEAP_RESERVED_DEFAULT)
        return nullptr;
      size_t need = align_up(size, 4096);
      void* ptr = alloc_segment(need);
      if (!ptr)
        return nullptr;
      xl_ptrs.push_back(ptr);
      xl_sizes.push_back(need);
      return ptr;
    }
    const size_t need = align_up(size, 16);
    if (tc->get_active()) {
      Page* cached = tc->get_cached_page(kind);
      if (cached) {
        void* fast = nullptr;
        if (mt) {
          std::lock_guard<std::mutex> lk(op_mu);
          fast = cached->allocate(need);
        } else {
          fast = cached->allocate(need);
        }
        if (fast)
          return fast;
      }
    }

    {
      std::lock_guard<std::mutex> lk(op_mu);
      for (size_t i = 0; i < layout.size(); ++i) {
        if (seg_page_kind[i] != kind)
          continue;
        void* ptr = layout[i].allocate(need, guard_seed);
        if (ptr) {
          Page* page = layout[i].find_page_for(ptr);
          if (page) {
            tc->cache_page(kind, page, page->get_base_addr(), page->get_span_size());
          }
          return ptr;
        }
      }
    }

    {
      std::scoped_lock lk(op_mu, meta_mu);
      if (add_segment_from_reserved_nolock(SEGMENT_NORM, kind)) {
        void* ptr = layout.back().allocate(need, guard_seed);
        if (ptr) {
          Page* page = layout.back().find_page_for(ptr);
          if (page) {
            tc->cache_page(kind, page, page->get_base_addr(), page->get_span_size());
          }
        }
        return ptr;
      }
    }

    void* seg_mem = alloc_segment(SEGMENT_SIZE);
    if (!seg_mem)
      return nullptr;
    {
      std::scoped_lock lk(op_mu, meta_mu);
      if (!add_segment_nolock(seg_mem, SEGMENT_NORM, kind)) {
        free_segment(seg_mem, SEGMENT_SIZE);
        return nullptr;
      }
      void* ptr = layout.back().allocate(need, guard_seed);
      if (ptr) {
        Page* page = layout.back().find_page_for(ptr);
        if (page) {
          tc->cache_page(kind, page, page->get_base_addr(), page->get_span_size());
        }
      }
      return ptr;
    }
  }

  bool free_ptr(void* ptr, size_t* usable_out) {
    if (!ptr)
      return true;
    ThreadCache* tc = ThreadCache::current();
    const bool mt = ThreadCache::is_multi_threaded();
    if (tc->get_active()) {
      page_kind_t kind = PAGE_SM;
      Page* cached = nullptr;
      if (tc->lookup_cached_for_ptr(ptr, &kind, &cached) && cached) {
        bool ok = false;
        if (mt) {
          std::lock_guard<std::mutex> lk(op_mu);
          ok = cached->free_ptr(ptr, usable_out);
        } else {
          ok = cached->free_ptr(ptr, usable_out);
        }
        if (ok) {
          if (cached->get_status() == EMPTY) {
            tc->clear_cached_page(kind, cached);
          }
          return true;
        }
      }
    }

    std::lock_guard<std::mutex> lk(op_mu);
    Segment* seg = find_segment_for(ptr);
    if (seg) {
      if (!seg->free_ptr(ptr, usable_out))
        std::abort();
      if (tc->get_active()) {
        Page* page = seg->find_page_for(ptr);
        if (page) {
          const page_kind_t kind = page->get_size_class();
          tc->cache_page(kind, page, page->get_base_addr(), page->get_span_size());
          if (page->get_status() == EMPTY) {
            tc->clear_cached_page(kind, page);
          }
        }
      }
      return true;
    }
    for (size_t i = 0; i < xl_ptrs.size(); ++i) {
      if (xl_ptrs[i] == ptr) {
        size_t sz = xl_sizes[i];
        free_segment(ptr, sz);
        if (usable_out)
          *usable_out = sz;
        std::lock_guard<std::mutex> lk_meta(meta_mu);
        xl_ptrs.erase(xl_ptrs.begin() + static_cast<std::ptrdiff_t>(i));
        xl_sizes.erase(xl_sizes.begin() + static_cast<std::ptrdiff_t>(i));
        return true;
      }
    }
    return false;
  }

  size_t usable_size(void* ptr) {
    std::lock_guard<std::mutex> lk(op_mu);
    Segment* seg = find_segment_for(ptr);
    if (seg)
      return seg->usable_size(ptr);
    for (size_t i = 0; i < xl_ptrs.size(); ++i) {
      if (xl_ptrs[i] == ptr)
        return xl_sizes[i];
    }
    return 0;
  }

  std::vector<segment_kind_t> get_segment_kinds() { return seg_kind; }
  uint32_t get_num_segments() { return num_segments; }

  bool is_corrupted() {
    if (canary == 0)
      return true;
    for (size_t i = 0; i < layout.size(); ++i) {
      if (!layout[i].check_canary(layout[i].get_key()))
        return true;
    }
    return false;
  }

  bool validate() {
    if (is_corrupted())
      return false;
    for (const Segment& seg : layout) {
      if (seg.num_pages() == 0)
        return false;
    }
    return true;
  }

  void clear_metadata() {
    std::scoped_lock lk(op_mu, meta_mu);
    if (base && reserved_size > 0) {
      free_segment(base, reserved_size);
    } else {
      for (void* seg : seg_bases)
        free_segment(seg, SEGMENT_SIZE);
    }
    for (size_t i = 0; i < xl_ptrs.size(); ++i)
      free_segment(xl_ptrs[i], xl_sizes[i]);
    layout.clear();
    seg_kind.clear();
    seg_bases.clear();
    seg_page_kind.clear();
    seg_index_by_base.clear();
    xl_ptrs.clear();
    xl_sizes.clear();
    base = nullptr;
    reserved_size = 0;
    num_segments = 0;
    canary = 0;
    mem_kind = MEM_NONE;
    reserved_cursor = 0;
    guard_seed = 0;
  }
};

bool heap_register_segment(void* segment_base) {
  return Heap::instance().add_segment(segment_base, SEGMENT_NORM, PAGE_SM);
}

void heap_clear_metadata() {
  Heap::instance().clear_metadata();
}

bool heap_init_reserved(void* reserved_base, size_t size) {
  return Heap::instance().init_reserved(reserved_base, size);
}

bool heap_add_segment_from_reserved(segment_kind_t kind) {
  return Heap::instance().add_segment_from_reserved(kind, PAGE_SM);
}

bool heap_add_segment_for_class(page_kind_t kind) {
  return Heap::instance().add_segment_from_reserved(SEGMENT_NORM, kind);
}

void* heap_alloc(size_t size) {
  return Heap::instance().allocate(size);
}

size_t heap_usable_size(void* ptr) {
  return Heap::instance().usable_size(ptr);
}

bool free_dispatch_with_size(void* ptr, size_t* usable_size) {
  return Heap::instance().free_ptr(ptr, usable_size);
}

bool heap_validate() {
  return Heap::instance().validate();
}

} // namespace zialloc::memory
