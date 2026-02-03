# Allocator Test Suite

VRIG's custom allocator test suite and benchmarking framework.

## Quick Start

1.  **Clone the template**:
    ```bash
    git clone https://github.com/CLUB-NAME/allocator-test-suite.git
    cd allocator-test-suite
    ```

2.  **Setup Dependencies** (Local Build):
    This script will clone and build `mimalloc` and `jemalloc` locally. No `sudo` required.
    ```bash
    ./setup_allocators.sh
    ```

3.  **Run Tests**:
    ```bash
    # Baseline (Glibc)
    make run-tests
    
    # Mimalloc
    make run-tests ALLOCATOR=allocators/mimalloc/mimalloc_wrapper.c \
        EXTRA_CFLAGS="-Iallocators/mimalloc/mimalloc_src/include" \
        LDFLAGS+="$(pwd)/build_secure/libmimalloc-secure.a -lpthread -lrt"

    # Jemalloc
    make run-tests ALLOCATOR=allocators/jemalloc/jemalloc_wrapper.c \
        EXTRA_CFLAGS="-Iallocators/jemalloc/jemalloc_src/include/jemalloc" \
        LDFLAGS+="$(pwd)/allocators/jemalloc/jemalloc_src/lib/libjemalloc.a -lpthread -ldl"
    ```

## Directory Structure

```
allocator-test-suite/
├── include/
│   ├── allocator.h        # Allocator interface (from spec)
│   ├── test_harness.h     # Test framework utilities
│   └── benchmark.h        # Benchmark infrastructure
├── src/
│   ├── tests/
│   │   ├── test_correctness.c    # TC-BASIC, TC-SIZE, TC-ALIGN, TC-REALLOC, TC-CALLOC
│   │   ├── test_stress.c         # TC-STRESS-001 through TC-STRESS-006
│   │   ├── test_edge.c           # TC-EDGE-001 through TC-EDGE-010
│   │   └── test_fragmentation.c  # TC-FRAG-001 through TC-FRAG-004
│   ├── benchmarks/
│   │   └── bench_synthetic.c     # WL-SYN-001 through WL-SYN-010
│   └── harness/
│       ├── main_tests.c   # Test runner entry point
│       └── main_bench.c   # Benchmark runner entry point
├── allocators/
│   └── glibc/
│       └── glibc_allocator.c  # Default glibc wrapper
├── Makefile
└── README.md
```

## Integrating Your Allocator

### Step 1: Implement the Interface

Your allocator must export an `allocator_t` structure. Here's the minimal template:

```c
// myalloc.c
#include "allocator.h"

// Implement required functions
static void* myalloc_malloc(size_t size) { /* ... */ }
static void myalloc_free(void* ptr) { /* ... */ }
static void* myalloc_realloc(void* ptr, size_t size) { /* ... */ }
static void* myalloc_calloc(size_t nmemb, size_t size) { /* ... */ }
static int myalloc_init(void) { return 0; }
static void myalloc_teardown(void) { }

// Export allocator structure
allocator_t myalloc_allocator = {
    .malloc = myalloc_malloc,
    .free = myalloc_free,
    .realloc = myalloc_realloc,
    .calloc = myalloc_calloc,
    
    // Optional (set to NULL if not implemented)
    .memalign = NULL,
    .aligned_alloc = NULL,
    .usable_size = NULL,
    .print_stats = NULL,
    .validate_heap = NULL,
    .get_stats = NULL,
    
    // Lifecycle
    .init = myalloc_init,
    .teardown = myalloc_teardown,
    
    // Metadata
    .name = "myalloc",
    .author = "Your Name",
    .version = "1.0.0",
    .description = "My custom allocator",
    .memory_backend = "mmap",
    
    .features = {
        .thread_safe = false,
        .min_alignment = 16,
        .max_alignment = 4096,
    },
};

// Override the default allocator getter
allocator_t* get_test_allocator(void) {
    return &myalloc_allocator;
}

allocator_t* get_bench_allocator(void) {
    return &myalloc_allocator;
}
```

### Step 2: Build and Test

```bash
# Point to your allocator source
make ALLOCATOR=path/to/myalloc.c run-tests

# Run with debug mode (AddressSanitizer)
make debug ALLOCATOR=path/to/myalloc.c run-tests

# Run benchmarks
make MODE=bench ALLOCATOR=path/to/myalloc.c run-bench
```

## Test Suites

### Correctness Tests (29 tests)
- **TC-BASIC-001 to 005**: Basic malloc/free operations
- **TC-SIZE-001 to 005**: Size handling (1B to 256MB)
- **TC-ALIGN-001 to 003**: Alignment verification
- **TC-REALLOC-001 to 007**: Realloc semantics
- **TC-CALLOC-001 to 004**: Calloc semantics
- **TC-USABLE-001 to 002**: usable_size (if implemented)

### Stress Tests (6 tests)
- **TC-STRESS-001**: Random malloc/free (1M ops)
- **TC-STRESS-002**: LIFO pattern (1M ops)
- **TC-STRESS-003**: FIFO pattern (1M ops)
- **TC-STRESS-004**: Realloc chains (100K ops)
- **TC-STRESS-005**: Peak memory cycling (100 cycles)
- **TC-STRESS-006**: 100K simultaneous allocations

### Edge Case Tests (10 tests)
- **TC-EDGE-001 to 002**: SIZE_MAX handling
- **TC-EDGE-003**: 100K × 1-byte allocations
- **TC-EDGE-004**: Page boundary allocations
- **TC-EDGE-005**: Rapid init/teardown cycles
- **TC-EDGE-006 to 010**: Various edge cases

### Fragmentation Tests (4 tests)
- **TC-FRAG-001**: Swiss cheese pattern
- **TC-FRAG-002**: Sawtooth pattern
- **TC-FRAG-003**: Size class thrashing
- **TC-FRAG-004**: Long-running simulation

## Benchmarks

### Synthetic Workloads

| ID | Description | Iterations |
|----|-------------|------------|
| WL-SYN-001 | Small fixed 64B, immediate free | 10M |
| WL-SYN-002 | Small random 16-256B | 10M |
| WL-SYN-003 | Medium fixed 4KB | 1M |
| WL-SYN-004 | Medium random 1-64KB | 1M |
| WL-SYN-005 | Large fixed 1MB | 100K |
| WL-SYN-006 | Large random 64KB-4MB | 100K |
| WL-SYN-007 | Mixed power-law, batch free | 10M |
| WL-SYN-008 | Realloc grow chain 16B→4KB | 1M |
| WL-SYN-009 | Realloc shrink chain 4KB→16B | 1M |
| WL-SYN-010 | Calloc random 16-4KB | 1M |

### Benchmark Options

```bash
# Run all workloads
./bin/run_bench --all

# Run quick subset
./bin/run_bench --quick

# Run specific workload
./bin/run_bench --workload=WL-SYN-001

# Multiple runs for statistical significance
./bin/run_bench --runs=10

# CSV output for analysis
./bin/run_bench --csv > results.csv
```

## Build Options

| Variable | Description |
|----------|-------------|
| `ALLOCATOR=path` | Path to custom allocator source |
| `MODE=debug` | -O0 -g3 with ASan/UBSan |
| `MODE=release` | -O2 -DNDEBUG |
| `MODE=bench` | -O3 -march=native -DNDEBUG |
| `ARGS=...` | Pass arguments to runner |

## Comparing Allocators

```bash
# Test glibc baseline
make run-bench --csv > results_glibc.csv

# Test your allocator
make ALLOCATOR=../myalloc/myalloc.c run-bench --csv > results_myalloc.csv

# Compare
diff results_glibc.csv results_myalloc.csv
```

## Debugging Tips

1. **Run with sanitizers**:
   ```bash
   make debug run-tests
   ```

2. **Run specific test suite**:
   ```bash
   make run-tests ARGS='--correctness'
   make run-tests ARGS='--stress'
   ```

3. **Use Valgrind**:
   ```bash
   make release
   valgrind --leak-check=full ./bin/run_tests
   ```

4. **Profile with perf**:
   ```bash
   make MODE=bench bench
   perf record ./bin/run_bench --workload=WL-SYN-001
   perf report
   ```

## Adding New Tests

1. Create test function in appropriate `test_*.c` file:
   ```c
   static test_result_t test_new_001(allocator_t* alloc) {
       // Your test
       TEST_ASSERT(condition, "message");
       return TEST_PASS;
   }
   ```

2. Add to test registration array:
   ```c
   test_case_t my_tests[] = {
       {"TC-NEW-001", "description", test_new_001},
   };
   ```
