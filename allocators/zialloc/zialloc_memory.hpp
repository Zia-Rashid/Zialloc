#pragma once

#include <cstddef>
#include <cstdint>
#include "mem.h"

namespace zialloc::memory {

// segment/page helpers 
size_t align_up(size_t size, size_t alignment);
void* alloc_segment(size_t size);
void free_segment(void* ptr, size_t size);
void* reserve_region(size_t size);
bool commit_region(void* ptr, size_t size);
void decommit_pages(void* ptr, size_t size);
void commit_pages(void* ptr, size_t size);
bool setup_guard(void* ptr, size_t size);
void lock_page(void* ptr, size_t size);
void unlock_page(void* ptr, size_t size);

// free-path entry
void free_dispatch(void* ptr);
bool free_dispatch_with_size(void* ptr, size_t* usable_size);

// heap allocation entry
void* heap_alloc(size_t size);
size_t heap_usable_size(void* ptr);
bool heap_validate();
bool heap_add_segment_for_class(page_kind_t kind);

// heap metadata registration 
bool heap_register_segment(void* segment_base);
void heap_clear_metadata();
bool heap_init_reserved(void* reserved_base, size_t size);
bool heap_add_segment_from_reserved(segment_kind_t kind);

class Chunk;
class Page;
class Segment;
class Heap;

} // namespace zialloc::memory

namespace zialloc {

// allocator hooks
void allocator_free_internal(void* ptr);
size_t allocator_usable_size_internal(void* ptr);
bool allocator_validate_internal();

} // namespace zialloc
