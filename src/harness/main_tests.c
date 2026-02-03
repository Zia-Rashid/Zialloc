// main_tests.c - Main Test Runner
// Usage: ./run_tests [allocator_name]

#include "allocator.h"
#include "test_harness.h"
#include <stdio.h>
#include <string.h>

// External test registrations
extern test_case_t correctness_tests[];
extern const size_t num_correctness_tests;

extern test_case_t stress_tests[];
extern const size_t num_stress_tests;

extern test_case_t edge_tests[];
extern const size_t num_edge_tests;

extern test_case_t fragmentation_tests[];
extern const size_t num_fragmentation_tests;

extern test_case_t feature_tests[];
extern const size_t num_feature_tests;

extern test_case_t realistic_tests[];
extern const size_t num_realistic_tests;

// Allocator to test (provided by linked allocator object)
extern allocator_t *get_test_allocator(void);

static void print_usage(const char *prog) {
  printf("Usage: %s [options]\n", prog);
  printf("\nOptions:\n");
  printf("  --all         Run all test suites (default)\n");
  printf("  --correctness Run correctness tests only\n");
  printf("  --stress      Run stress tests only\n");
  printf("  --edge        Run edge case tests only\n");
  printf("  --frag        Run fragmentation tests only\n");
  printf("  --features    Run optional feature tests (skip if unsupported)\n");
  printf("  --realistic   Run realistic workload tests only\n");
  printf("  --help        Show this help\n");
}

int main(int argc, char *argv[]) {
  bool run_correctness = true;
  bool run_stress = true;
  bool run_edge = true;
  bool run_frag = true;
  bool run_features = true;
  bool run_realistic = true;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--help") == 0) {
      print_usage(argv[0]);
      return 0;
    } else if (strcmp(argv[i], "--all") == 0) {
      run_correctness = run_stress = run_edge = run_frag = run_features =
          run_realistic = true;
    } else if (strcmp(argv[i], "--correctness") == 0) {
      run_correctness = true;
      run_stress = run_edge = run_frag = false;
    } else if (strcmp(argv[i], "--stress") == 0) {
      run_stress = true;
      run_correctness = run_edge = run_frag = run_features = false;
    } else if (strcmp(argv[i], "--edge") == 0) {
      run_edge = true;
      run_correctness = run_stress = run_frag = false;
    } else if (strcmp(argv[i], "--frag") == 0) {
      run_frag = true;
      run_correctness = run_stress = run_edge = run_features = false;
    } else if (strcmp(argv[i], "--features") == 0) {
      run_features = true;
      run_correctness = run_stress = run_edge = run_frag = run_realistic =
          false;
    } else if (strcmp(argv[i], "--realistic") == 0) {
      run_realistic = true;
      run_correctness = run_stress = run_edge = run_frag = run_features = false;
    }
  }

  allocator_t *alloc = get_test_allocator();

  printf("Allocator Test Suite v%d.%d.%d\n", ALLOC_VERSION_MAJOR,
         ALLOC_VERSION_MINOR, ALLOC_VERSION_PATCH);
  printf("Testing: %s v%s by %s\n", alloc->name, alloc->version, alloc->author);
  printf("Backend: %s\n", alloc->memory_backend);
  printf("Description: %s\n", alloc->description);
  printf("\nFeatures:\n");
  printf("  Thread-safe:       %s\n",
         alloc->features.thread_safe ? "yes" : "no");
  printf("  Per-thread cache:  %s\n",
         alloc->features.per_thread_cache ? "yes" : "no");
  printf("  Huge pages:        %s\n",
         alloc->features.huge_page_support ? "yes" : "no");
  printf("  Guard pages:       %s\n",
         alloc->features.guard_pages ? "yes" : "no");
  printf("  Canaries:          %s\n", alloc->features.canaries ? "yes" : "no");
  printf("  Quarantine:        %s\n",
         alloc->features.quarantine ? "yes" : "no");
  printf("  Zero-on-free:      %s\n",
         alloc->features.zero_on_free ? "yes" : "no");
  printf("  Alignment:         %zu - %zu bytes\n",
         alloc->features.min_alignment, alloc->features.max_alignment);

  if (alloc->init) {
    int init_result = alloc->init();
    if (init_result != 0) {
      fprintf(stderr, "ERROR: Allocator init() failed with code %d\n",
              init_result);
      return 1;
    }
  }

  test_summary_t total_summary = {0};

  if (run_correctness) {
    test_summary_t summary = {0};
    run_test_suite("Correctness", correctness_tests, num_correctness_tests,
                   alloc, &summary);
    total_summary.total += summary.total;
    total_summary.passed += summary.passed;
    total_summary.failed += summary.failed;
    total_summary.skipped += summary.skipped;
  }

  if (run_stress) {
    test_summary_t summary = {0};
    run_test_suite("Stress", stress_tests, num_stress_tests, alloc, &summary);
    total_summary.total += summary.total;
    total_summary.passed += summary.passed;
    total_summary.failed += summary.failed;
    total_summary.skipped += summary.skipped;
  }

  if (run_edge) {
    test_summary_t summary = {0};
    run_test_suite("Edge Cases", edge_tests, num_edge_tests, alloc, &summary);
    total_summary.total += summary.total;
    total_summary.passed += summary.passed;
    total_summary.failed += summary.failed;
    total_summary.skipped += summary.skipped;
  }

  if (run_frag) {
    test_summary_t summary = {0};
    run_test_suite("Fragmentation", fragmentation_tests,
                   num_fragmentation_tests, alloc, &summary);
    total_summary.total += summary.total;
    total_summary.passed += summary.passed;
    total_summary.failed += summary.failed;
    total_summary.skipped += summary.skipped;
  }

  if (run_features) {
    test_summary_t summary = {0};
    run_test_suite("Optional Features", feature_tests, num_feature_tests, alloc,
                   &summary);
    total_summary.total += summary.total;
    total_summary.passed += summary.passed;
    total_summary.failed += summary.failed;
    total_summary.skipped += summary.skipped;
  }

  if (run_realistic) {
    test_summary_t summary = {0};
    run_test_suite("Realistic Workloads", realistic_tests, num_realistic_tests,
                   alloc, &summary);
    total_summary.total += summary.total;
    total_summary.passed += summary.passed;
    total_summary.failed += summary.failed;
    total_summary.skipped += summary.skipped;
  }

  printf("OVERALL RESULTS\n");
  print_summary(&total_summary);
  if (alloc->print_stats) {
    printf("\n");
    alloc->print_stats();
  }

  if (alloc->teardown) {
    alloc->teardown();
  }

  return (total_summary.failed > 0) ? 1 : 0;
}
