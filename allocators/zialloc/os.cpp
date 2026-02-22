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
#include "zialloc_memory.hpp"

namespace zialloc {
namespace os {

constexpr static inline size_t align_up(size_t size, size_t alignment) {
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

static inline size_t get_page_size() {
    static size_t pgsz = (size_t)sysconf(_SC_PAGESIZE);
    return pgsz;
}

// alloc `size` bytes of anonymous memory w/ mmap
// rets nullptr if fail, mem is zero init'd
static void* os_mmap(size_t size) {
    void* ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED) return nullptr;
    return ptr;
}   

// /\ will be used internally for segment init and page reclaiming
// --------------------------------------------------------------------------------------
// \/ will only really be used by XL chunks so that the size is whatever they want it to be. 

// alloc `size` bytes aligned to `alignment` w/i mmap
static void* os_mmap_aligned(size_t size, size_t alignment) {
    // over-allocate so we can find an aligned region inside
    size_t alloc_size = size + alignment - 1;
    void* raw = os_mmap(alloc_size);
    if (!raw) return nullptr;

    uintptr_t raw_addr = (uintptr_t)raw;
    uintptr_t aligned  = align_up(raw_addr, alignment);

    // trim leading slop
    if (aligned > raw_addr)
        munmap(raw, aligned - raw_addr);

    // trim trailing slop
    uintptr_t end     = aligned + size;
    uintptr_t raw_end = raw_addr + alloc_size;
    if (raw_end > end)
        munmap((void*)end, raw_end - end);

    return (void*)aligned;
}

// used when an entire segment can be unmapped (virt + phys)
static void os_munmap(void* ptr, size_t size) {
    munmap(ptr, size);
}

// release physical pages but keep the virtual address reservation
// access will re-fault in zeroed pages.
// This is what we use for freed pages to:      (for the documentation)
//   a) reduce RSS
//   b) give UAF segfault protection (reads return zero, not stale data)
static void os_decommit(void* ptr, size_t size) {
    madvise(ptr, size, MADV_DONTNEED);
}

static void os_commit(void* ptr, size_t size) {
    // hint to kernel to prefault pages (semantically a no-op on Linux)
    madvise(ptr, size, MADV_WILLNEED);
}

// Remove all permissions on a page range (makes any access segfault)
// for freed / guard pages
static void os_protect_none(void* ptr, size_t size) {
    mprotect(ptr, size, PROT_NONE);
}

// make RW (before re-allocating it)
static void os_protect_rw(void* ptr, size_t size) {
    mprotect(ptr, size, PROT_READ | PROT_WRITE);
}

// make RO (useful for metadata)
[[maybe_unused]] static void os_protect_ro(void* ptr, size_t size) {
    mprotect(ptr, size, PROT_READ);
}


// Create a guard page at `ptr` of `size` bytes.
static bool os_create_guard(void* ptr, size_t size) {
    // PROT_NONE causes immediate segfault on any access
    return mprotect(ptr, size, PROT_NONE) == 0;
} 


// allocate using huge pages 
[[maybe_unused]] static void* os_mmap_huge(size_t size) {
    // Try 2MiB huge pages; fall back to regular mmap on failure
    void* ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
    if (ptr == MAP_FAILED) return os_mmap(size);
    return ptr;
}

// reserve virt addr space w/o committing phys mem
// have to use commit_region to make it usable
static void* os_reserve_region(size_t size) {
    void* ptr = mmap(nullptr, size, PROT_NONE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (ptr == MAP_FAILED) return nullptr;
    return ptr;
}

// commit a subrange of a reserved region ot phys (make it read/write)
// range must be w/i a region previously returned by os_reserve_region
static bool os_commit_region(void* ptr, size_t size) {
    return mprotect(ptr, size, PROT_READ | PROT_WRITE) == 0;
}

// alloc a new segment-aligned region of `size` bytes.
void* alloc_segment(size_t size) {
    return os_mmap_aligned(size, SEGMENT_ALIGN);
}

void* reserve_region(size_t size) {
    return os_reserve_region(size);
}

bool commit_region(void* ptr, size_t size) {
    return os_commit_region(ptr, size);
}

void free_segment(void* ptr, size_t size) {
    os_munmap(ptr, size);
}

// free physical, keep virtual
void decommit_pages(void* ptr, size_t size) {
    os_decommit(ptr, size);
}

// recommit a page range w/i a segment
void commit_pages(void* ptr, size_t size) {
    os_commit(ptr, size);
}

bool setup_guard(void* ptr, size_t size) {
    return os_create_guard(ptr, size);
}

// any access segfaults.
void lock_page(void* ptr, size_t size) {
    os_protect_none(ptr, size);
}

void unlock_page(void* ptr, size_t size) {
    os_protect_rw(ptr, size);
}


}  // namespace os
}  // namespace zialloc

namespace zialloc::memory {

size_t align_up(size_t size, size_t alignment) {
    return zialloc::os::align_up(size, alignment);
}

void* alloc_segment(size_t size) {
    return zialloc::os::alloc_segment(size);
}

void* reserve_region(size_t size) {
    return zialloc::os::reserve_region(size);
}

bool commit_region(void* ptr, size_t size) {
    return zialloc::os::commit_region(ptr, size);
}

void free_segment(void* ptr, size_t size) {
    zialloc::os::free_segment(ptr, size);
}

void decommit_pages(void* ptr, size_t size) {
    zialloc::os::decommit_pages(ptr, size);
}

void commit_pages(void* ptr, size_t size) {
    zialloc::os::commit_pages(ptr, size);
}

bool setup_guard(void* ptr, size_t size) {
    return zialloc::os::setup_guard(ptr, size);
}

void lock_page(void* ptr, size_t size) {
    zialloc::os::lock_page(ptr, size);
}

void unlock_page(void* ptr, size_t size) {
    zialloc::os::unlock_page(ptr, size);
}

} // namespace zialloc::memory


