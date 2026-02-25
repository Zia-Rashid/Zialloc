# Zialloc Design

#### DISCLAIMER: I'll continue working on this project, so there is no promise that this Design Doc is up-to-date regarding security features and execution path. Layout won't change though.

## Size Model
Zialloc uses fixed size classes for regular allocations plus a direct-mapped XL path.

- Reserved heap virtual address space (default): 2GiB
- Segment size / alignment: 4MiB
- Page classes: small (64KiB), medium (512KiB), large (4MiB), XL (direct OS mapping)
- Chunk size thresholds used for class selection: `0x7800` (small), `0x3C000` (medium), `0x1FFF00` (large), above-threshold is XL

Regular (non-XL) chunk sizes are aligned to 16 bytes before placement.

## Heap Layout
At init, Zialloc reserves a large virtual region (w/ `reserve_region`) and commits segments from it on demand (w/ `commit_region`). It immediately seeds one segment each for small, medium, and large classes.

High-level layout:

```text
[ Reserved Heap VA Space (2GiB/PROT_NONE default) ]
|---- Segment 0 (4MiB, SMALL) ----|
|---- Segment 1 (4MiB, MEDIUM) ---|
|---- Segment 2 (4MiB, LARGE) ----|
|---- ... additional committed segments ...|
```

Each segment is also classed (all its pages are one page kind):

```text
Segment (4MiB)
| Page 0 | Page 1 | ... | Page N |
```

Each page is subdivided into fixed-size chunks for that page instance:

```text
Page
| Chunk 0 | Chunk 1 | Chunk 2 | ... | Chunk K |
```

XL allocations bypass the segment/page system and use directly mapped regions that are tracked in `xl_allocs`.

## Allocation Workflow
Allocation starts through allocator API wrappers and then `Heap::allocate(size)`.

Main behavior:
- `malloc/calloc/realloc` validate size and ensure heap initialization.
- Requested size is classed: small/medium/large/XL.
- For non-XL classes, allocator tries the fastest path first and progresses to the slowest: thread cached page, preferred segment, existing matching-class segments, reserved-space segment growth, then fresh OS-mapped segment.
- For XL class, map aligned memory directly and record allocation size in `xl_allocs`.

Allocation flow (simplified):

```text
malloc(size)
  |
  +--> init heap if needed
  |
  +--> class_for_size(size)
         |
         +--> XL ? -------- yes ---> map XL region -> track in xl_allocs -> return
         | no
         v
      align size to 16
         |
         +--> try thread cached page
         |      |
         |      +--> success -> return
         |
         +--> try thread preferred segment
         |      |
         |      +--> success -> cache page -> return
         |
         +--> scan matching-class segments
         |      |
         |      +--> success -> set preferred segment/cache page -> return
         |
         +--> add segment from reserved region
         |      |
         |      +--> success -> allocate -> cache page -> return
         |
         +--> mmap new segment -> add -> allocate -> return/null
```

## Free Workflow
Free enters through `free()` and dispatches to `Heap::free_ptr(ptr, usable_out)`.

Main behavior:
- Null free is ignored.
- If pointer falls in a cached page range, free is attempted there first.
- If freeing thread differs from page owner thread, pointer is enqueued to deferred frees for that page (later drained on allocation).
- Otherwise pointer is freed directly to page freelist.
- If fast cached lookup misses, heap finds owning segment/page and performs same-thread free or deferred enqueue.
- If pointer belongs to `xl_allocs`, allocator optionally zeroes, unmaps, and erases tracking entry.
- Invalid/untracked pointers fail and may abort via integrity checks.

Free flow (simplified):

```text
free(ptr)
  |
  +--> ptr == null ? -> return
  |
  +--> cached page lookup by pointer range
  |      |
  |      +--> hit:
  |            owner thread differs?
  |              | yes -> enqueue deferred free -> return
  |              | no  -> direct free to page freelist -> return
  |
  +--> find segment/page in heap metadata
  |      |
  |      +--> found:
  |            owner thread differs?
  |              | yes -> enqueue deferred free -> return
  |              | no  -> direct free -> return
  |
  +--> lookup in xl_allocs
         |
         +--> found -> memset(0)(?) -> munmap -> erase -> return
         |
         +--> not found -> invalid pointer path (failure/abort)
```

## Optimizations

Implemented/perf-relevant mechanisms:
- Thread-local cache object with per-class cached page pointers
- Thread-local preferred segment index per class to reduce global scans
- Lock striping for pages and segments to reduce mutex hotspotting
- Deferred cross-thread frees to avoid remote thread mutating an owner page directly
- Deferred free draining heuristics (drain chunks in batches when pressure is high)
- Pre-reserved virtual heap and ondemand segment committing to reduce repeated full mappings
- Batched statistics updates using thread-local accumulation and periodic atomic flush

- Security features are toggle-able to improve benchmarking speed. By default they are OFF so turn them on if using this as a general purpose allocator

## Security Strategy
Zialloc uses active integrity checks and additional hardening hooks that can be enabled or expanded

Current security controls and checks:
- Pointer containment and ownership checks before page free operations
- Abort-on-corruption paths for invalid state transitions (e.g., double-frees)
- Heap/segment validation hooks (`heap_validate`) and canary-like integrity checks
- togglable `zero_on_free` and `UAF` checks


## Known Limits
- XL allocations are direct mapped and tracked separately; behavior differs from class-segmented allocations
- Segment classing reduces some fragmentation patterns but still trades memory efficiency for predictable allocation behavior (is supposed to improve cache locality)
- Thread-aware fast paths improve  latency butadd logic complexity

## Src Map

- API entrypoints, stats:
- `allocators/zialloc/alloc.cpp`
- Core impl, caching, deferred free:
- `allocators/zialloc/segments.cpp`
- OS mapping/protection/reservation wrappers:
- `allocators/zialloc/os.cpp`
- Shared constants, enums, validation macros:
- `allocators/zialloc/types.h`
- `allocators/zialloc/mem.h`
- Free dispatch wrapper(useless ngl):
- `allocators/zialloc/free.cpp`
- Public allocator interface contract (for friendly-competition):
- `include/allocator.h`
