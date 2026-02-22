#include "zialloc_memory.hpp"

namespace zialloc::memory {

void free_dispatch(void *ptr) {
  (void)free_dispatch_with_size(ptr, nullptr);
}

} // namespace zialloc::memory
