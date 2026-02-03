// main_bench.c - Main Benchmark Runner
// Usage: ./run_bench [options]

#include "allocator.h"
#include "benchmark.h"
#include <getopt.h>
#include <stdio.h>
#include <string.h>

// External workload registrations
extern bench_workload_t synthetic_workloads[];
extern const size_t num_synthetic_workloads;

// Allocator to benchmark (provided by linked allocator object)
extern allocator_t *get_bench_allocator(void);

static void print_usage(const char *prog) {
  printf("Usage: %s [options]\n", prog);
  printf("\nOptions:\n");
  printf("  --all            Run all workloads (default)\n");
  printf("  --quick          Run quick subset (SYN-001, SYN-002, SYN-007)\n");
  printf("  --workload=ID    Run specific workload (e.g., WL-SYN-001)\n");
  printf("  --runs=N         Number of runs per workload (default: 3)\n");
  printf("  --csv            Output in CSV format\n");
  printf("  --help           Show this help\n");
}

static void print_csv_header(void) {
  printf("allocator,workload,throughput_ops_sec,p50_ns,p99_ns,p999_ns,max_ns,"
         "rss_kb\n");
}

static void print_csv_row(const char *allocator, const char *workload,
                          bench_metrics_t *m) {
  printf("%s,%s,%.2f,%lu,%lu,%lu,%lu,%zu\n", allocator, workload,
         m->throughput_ops_sec, (unsigned long)m->latency_p50_ns,
         (unsigned long)m->latency_p99_ns, (unsigned long)m->latency_p999_ns,
         (unsigned long)m->latency_max_ns, m->rss_bytes / 1024);
}

int main(int argc, char *argv[]) {
  int num_runs = 3;
  bool csv_output = false;
  bool run_all = true;
  bool run_quick = false;
  char *specific_workload = NULL;

  static struct option long_options[] = {
      {"all", no_argument, 0, 'a'},
      {"quick", no_argument, 0, 'q'},
      {"workload", required_argument, 0, 'w'},
      {"runs", required_argument, 0, 'r'},
      {"csv", no_argument, 0, 'c'},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}};

  int opt;
  while ((opt = getopt_long(argc, argv, "aqw:r:ch", long_options, NULL)) !=
         -1) {
    switch (opt) {
    case 'a':
      run_all = true;
      run_quick = false;
      break;
    case 'q':
      run_quick = true;
      run_all = false;
      break;
    case 'w':
      specific_workload = optarg;
      run_all = false;
      run_quick = false;
      break;
    case 'r':
      num_runs = atoi(optarg);
      if (num_runs < 1)
        num_runs = 1;
      if (num_runs > 20)
        num_runs = 20;
      break;
    case 'c':
      csv_output = true;
      break;
    case 'h':
      print_usage(argv[0]);
      return 0;
    default:
      print_usage(argv[0]);
      return 1;
    }
  }

  allocator_t *alloc = get_bench_allocator();

  if (!csv_output) {
    printf("Allocator Benchmark Suite v%d.%d.%d\n", ALLOC_VERSION_MAJOR,
           ALLOC_VERSION_MINOR, ALLOC_VERSION_PATCH);
    printf("Benchmarking: %s v%s by %s\n", alloc->name, alloc->version,
           alloc->author);
    printf("Runs per workload: %d\n", num_runs);
  }

  if (alloc->init) {
    int init_result = alloc->init();
    if (init_result != 0) {
      fprintf(stderr, "ERROR: Allocator init() failed with code %d\n",
              init_result);
      return 1;
    }
  }

  bench_metrics_t results[20];
  size_t num_results = 0;

  if (csv_output) {
    print_csv_header();
  }

  for (size_t i = 0; i < num_synthetic_workloads && num_results < 20; i++) {
    bool should_run = false;

    if (run_all) {
      should_run = true;
    } else if (run_quick) {
      should_run =
          (strcmp(synthetic_workloads[i].workload_id, "WL-SYN-001") == 0 ||
           strcmp(synthetic_workloads[i].workload_id, "WL-SYN-002") == 0 ||
           strcmp(synthetic_workloads[i].workload_id, "WL-SYN-007") == 0);
    } else if (specific_workload) {
      should_run =
          (strcmp(synthetic_workloads[i].workload_id, specific_workload) == 0);
    }

    if (should_run) {
      bench_metrics_t metrics = {0};
      bench_run_workload(&synthetic_workloads[i], alloc, &metrics, num_runs);

      if (csv_output) {
        print_csv_row(alloc->name, synthetic_workloads[i].workload_id,
                      &metrics);
      }

      results[num_results++] = metrics;
    }
  }

  if (!csv_output && num_results > 0) {
    bench_print_summary_header();

    size_t result_idx = 0;
    for (size_t i = 0; i < num_synthetic_workloads && result_idx < num_results;
         i++) {
      bool was_run = false;

      if (run_all) {
        was_run = true;
      } else if (run_quick) {
        was_run =
            (strcmp(synthetic_workloads[i].workload_id, "WL-SYN-001") == 0 ||
             strcmp(synthetic_workloads[i].workload_id, "WL-SYN-002") == 0 ||
             strcmp(synthetic_workloads[i].workload_id, "WL-SYN-007") == 0);
      } else if (specific_workload) {
        was_run = (strcmp(synthetic_workloads[i].workload_id,
                          specific_workload) == 0);
      }

      if (was_run) {
        bench_print_summary_row(alloc->name, synthetic_workloads[i].workload_id,
                                &results[result_idx]);
        result_idx++;
      }
    }
  }

  if (!csv_output && alloc->print_stats) {
    printf("\n");
    alloc->print_stats();
  }

  // Mr gorbachov tear down this allocator
  if (alloc->teardown) {
    alloc->teardown();
  }

  return 0;
}
