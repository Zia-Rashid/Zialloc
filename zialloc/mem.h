#ifndef SEGMENTS_H
#define SEGMENTS_H

#include "types.h"
#include <cstdint>
#include <iostream>
#include <mutex> // for multi-threaded allocation
#include <random>
#include <syscall.h>
#include <thread> // for multi-thread
#include <unistd.h>

#define INTEGRITY_CHECK(condition, message)                                    \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << "Integrity Failure: " << message << " at " << __FILE__      \
                << ":" << __LINE__ << std::endl;                               \
      std::abort();                                                            \
    }                                                                          \
  } while (0)

#define PTR_IN_BOUNDS(condition, message)                                      \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << "Illegal Pointer: " << message << " at " << __FILE__ << ":" \
                << __LINE__ << std::endl;                                      \
      std::abort();                                                            \
    }                                                                          \
  } while (0)

// Abort if heap has not been initialized (use only after malloc/calloc have
// run at least once). Use in free, realloc, and any other non-alloc entry.
#define IS_HEAP_INITIALIZED(cond)                                              \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "Heap not initialized: cannot call this function before "   \
                   "malloc or calloc."  << " at " << __FILE__ << ":"           \
                   << __LINE__ << std::endl;                                   \
      std::abort();                                                            \
    }                                                                          \
  } while (0)

typedef enum page_kind_e {
  PAGE_SM,  // small blocks go into 1mib pages inside a segment
  PAGE_MED, // medium   ^   ^   ^   8mib   ^    ^    ^    ^
  PAGE_LG,  // large    ^   ^   ^   16mib     ^    ^    ^    ^
  PAGE_XL   // x-large will either split(unlikely) or default to mmap.
} page_kind_t;

constexpr size_t page_kind_size(int kind) {
    return kind == PAGE_SM  ? (ZU(1) << SMALL_PAGE_SHIFT) :
           kind == PAGE_MED ? (ZU(1) << MEDIUM_PAGE_SHIFT) :
           kind == PAGE_LG  ? LARGE_PAGE_SIZE :
                              SEGMENT_SIZE;
}

typedef enum page_status_e { FULL, ACTIVE, EMPTY } page_status_t;

// all power aligned (i think), we can fill unused space w/ guard chunks.
// max_chunk <= (usable / 2) - alignment // so, ...
// Compute usable = P - overhead
// Set max_chunk = floor(usable / 2)
typedef enum chunk_max_e {
  // keep per-class max chunk <= ~half page so each page can hold at least 2
  // chunks even after metadata/alignment
  CHUNK_SM = 0x7FFF0,  // 512kib - 16b
  CHUNK_MD = 0x3FFFF0, // 4mib - 16b
  CHUNK_LG = 0x7FFFF0, // 8mib - 16b
  CHUNK_XL             // whatever it wants to be
} chunk_max_t;

static inline pid_t current_tid() { return syscall(SYS_gettid); }

static inline uint64_t generate_canary() {
  std::random_device rd;     // Non-deterministic seed
  std::mt19937_64 gen(rd()); // 64-bit generator
  std::uniform_int_distribution<uint64_t> dis;
  return dis(gen);
}

#endif // SEGMENTS_H
