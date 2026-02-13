/* 
    file for implementing the OS mmap, free, alignment nonsense.
    include alignment at an offset.
    
*/

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cerrno>
#include <sys/mman.h>
#include <unistd.h>

#include "types.h"

namespace zialloc {
namespace os {

// alignment has to be a power of 2.
static inline size_t align_up(size_t size, size_t alignment) {
    return (size + alignment - 1) & ~(alignment - 1);
}  
/*
        (0x37 + 0x10 - 1) & ~(0x10 - 1)
        (0x37 + 0x0F)     & ~(0x0F)
        (0x46)            & ~(0x0F)
        0b0100_0110       & 0x0F = 0b0000_1111 -> ~0x0F = 0b1111_0000
        & 0b1111_0000
        ----------------
        0b0100_0000    =>   0x40     
*/

// Return the system page size (typically 4096).
static inline size_t get_page_size() {
    // TODO: call sysconf(_SC_PAGESIZE) or use getpagesize()
    return 0;
}


///////////////////////////////////
//   Core OS memory operations   //
///////////////////////////////////

// alloc `size` bytes of anonymous memory w/ mmap.
// returns nullptr on failure. memory is initially zeroed by the kernel.
// flags: MAP_PRIVATE | MAP_ANONYMOUS
static void* os_mmap(size_t size) {
    void* ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED) return nullptr;
    return ptr;
}   

// /\ will be used internally for segment init and page reclaiming
// --------------------------------------------------------------------------------------
// \/ will only really be used by XL chunks so that the size is whatever they want it to be. 

// alloc `size` bytes aligned to `alignment` via mmap.
// Over-allocates by (alignment - 1) bytes, then trims the excess
// using munmap on the leading/trailing slop.
// This is the main entry point for segment allocation.
static void* os_mmap_aligned(size_t size, size_t alignment) {
     //   1. alloc_size = size + alignment - 1
    //   2. raw = mmap(nullptr, alloc_size, ...)
    //   3. aligned = align_up((uintptr_t)raw, alignment)
    //   4. trim leading:  if (aligned > raw) munmap(raw, aligned - raw)
    //   5. trim trailing: end = aligned + size; raw_end = raw + alloc_size;
    //                     if (raw_end > end) munmap(end, raw_end - end)
    //   6. return (void*)aligned
    (void)size; (void)alignment;
    return nullptr;
}

// Release memory back to the OS entirely (virtual + physical).
// Used when an entire segment can be unmapped.
static void os_munmap(void* ptr, size_t size) {
    // TODO: munmap(ptr, size)
    (void)ptr; (void)size;
}


// ─────────────────────────────────────────────
// Physical memory management (keep vmem, release phys)
// ─────────────────────────────────────────────

// Release physical pages but keep the virtual address reservation.
// Any subsequent access to this range will re-fault in zeroed pages.
// This is what we use for freed pages to:
//   a) reduce RSS
//   b) give UAF segfault protection (reads return zero, not stale data)
static void os_decommit(void* ptr, size_t size) {
    // TODO: madvise(ptr, size, MADV_DONTNEED)
    // On newer kernels you could also try MADV_FREE (lazy reclaim)
    (void)ptr; (void)size;
}

// Re-commit previously decommitted pages (no-op on Linux, 
// since touching the page auto-faults it back in, but good
// to have as a semantic marker and for portability).
static void os_commit(void* ptr, size_t size) {
    // TODO: on Linux this is essentially a no-op.
    // On other OSes (Windows) you'd call VirtualAlloc(..., MEM_COMMIT, ...).
    // For now, consider madvise(ptr, size, MADV_WILLNEED) as a hint.
    (void)ptr; (void)size;
}


// Remove all permissions on a page range (makes any access segfault).
// Used on freed pages and guard pages.
static void os_protect_none(void* ptr, size_t size) {
    // TODO: mprotect(ptr, size, PROT_NONE)
    (void)ptr; (void)size;
}

// Restore read+write on a page range (before re-allocating it).
static void os_protect_rw(void* ptr, size_t size) {
    // TODO: mprotect(ptr, size, PROT_READ | PROT_WRITE)
    (void)ptr; (void)size;
}

// Mark a page range read-only (useful for metadata pages).
static void os_protect_ro(void* ptr, size_t size) {
    // TODO: mprotect(ptr, size, PROT_READ)
    (void)ptr; (void)size;
}


// Create a guard page at `ptr` of `size` bytes.
// A guard page is PROT_NONE memory that causes an immediate segfault
// on any access — placed between segments and at page boundaries.
static bool os_create_guard(void* ptr, size_t size) {
    // TODO: mprotect(ptr, size, PROT_NONE)
    // Return true on success, false on failure.
    (void)ptr; (void)size;
    return false;
} // make sure to back the segments.cpp guard pages appropriately.


// Allocate using huge pages (2MiB on x86, MAP_HUGETLB).
// Falls back to regular mmap on failure.
static void* os_mmap_huge(size_t size) {
    // TODO:
    //   void* ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE,
    //                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
    //   if (ptr == MAP_FAILED) return os_mmap(size);  // fallback
    //   return ptr;
    (void)size;
    return nullptr;
}


// ─────────────────────────────────────────────
// High-level wrappers (used by segments.cpp / alloc.cpp)
// ─────────────────────────────────────────────

// Allocate a new segment-aligned region of `size` bytes.
// This is the primary function segments.cpp will call.
void* alloc_segment(size_t size) {
    // TODO: return os_mmap_aligned(size, SEGMENT_ALIGN)
    (void)size;
    return nullptr;
}

// Free an entire segment region.
void free_segment(void* ptr, size_t size) {
    // TODO: os_munmap(ptr, size)
    (void)ptr; (void)size;
}

// Decommit a page range within a segment (free physical, keep virtual).
void decommit_pages(void* ptr, size_t size) {
    // TODO: os_decommit(ptr, size)
    (void)ptr; (void)size;
}

// Recommit a page range within a segment before re-use.
void commit_pages(void* ptr, size_t size) {
    // TODO: os_commit(ptr, size)
    (void)ptr; (void)size;
}

// Set up a guard page.
bool setup_guard(void* ptr, size_t size) {
    // TODO: return os_create_guard(ptr, size)
    (void)ptr; (void)size;
    return false;
}

// Lock a freed page so any access segfaults.
void lock_page(void* ptr, size_t size) {
    // TODO: os_protect_none(ptr, size)
    (void)ptr; (void)size;
}

// Unlock a page for allocation.
void unlock_page(void* ptr, size_t size) {
    // TODO: os_protect_rw(ptr, size)
    (void)ptr; (void)size;
}


}  // namespace os
}  // namespace zialloc


/*
    ═══════════════════════════════════════════════════════════════
    WHAT TO DO NEXT (os.cpp):
    ═══════════════════════════════════════════════════════════════

    1. BASICS FIRST: Implement align_up() and get_page_size().
       These are one-liners — align_up is bitmask arithmetic,
       get_page_size wraps sysconf(_SC_PAGESIZE).

    2. CORE MMAP: Implement os_mmap() — straightforward mmap call.
       Then os_mmap_aligned() which over-allocates + trims.
       Then os_munmap() — just munmap().

    3. DECOMMIT / COMMIT: Implement os_decommit() with 
       madvise(MADV_DONTNEED). os_commit() is a no-op on Linux
       but add MADV_WILLNEED as a hint.

    4. PROTECTION: Implement os_protect_none/rw/ro using mprotect().
       These are all one-liners with different prot flags.

    5. GUARD PAGES: os_create_guard() is just mprotect(PROT_NONE).

    6. HUGE PAGES: os_mmap_huge() tries MAP_HUGETLB, falls back.
       This is low priority — implement last.

    7. HIGH-LEVEL WRAPPERS: Once the static helpers above work,
       the public wrappers (alloc_segment, free_segment, etc.) 
       are trivial pass-throughs. Wire them up.

    Recommended order: 1 → 2 → 3 → 7 → 4 → 5 → 6
    Get alloc_segment / free_segment working first so you can 
    test segment creation in segments.cpp before worrying about
    page protection and security features.
    ═══════════════════════════════════════════════════════════════
*/
