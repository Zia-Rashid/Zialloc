// benchmark.h - Benchmarking Infrastructure

#ifndef BENCHMARK_H
#define BENCHMARK_H

#include "allocator.h"
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef COLOR_RED
#define COLOR_RED "\033[31m"
#define COLOR_GREEN "\033[32m"
#define COLOR_YELLOW "\033[33m"
#define COLOR_CYAN "\033[36m"
#define COLOR_RESET "\033[0m"
#endif

#define BENCH_WARMUP_OPS 10000
#define BENCH_SAMPLE_INTERVAL 100
#define BENCH_MAX_SAMPLES 1000000

typedef struct {
  double throughput_ops_sec;
  uint64_t latency_p50_ns;
  uint64_t latency_p99_ns;
  uint64_t latency_p999_ns;
  uint64_t latency_max_ns;
  size_t rss_bytes;
  double overhead_ratio;
  double fragmentation_ratio;
} bench_metrics_t;

typedef struct {
  const char *workload_id;
  const char *description;
  void (*setup)(allocator_t *alloc);
  void (*run)(allocator_t *alloc, bench_metrics_t *metrics);
  void (*teardown)(allocator_t *alloc);
  size_t iterations;
} bench_workload_t;

static inline uint64_t bench_get_time_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static inline size_t bench_get_rss(void) {
  FILE *f = fopen("/proc/self/statm", "r");
  if (!f)
    return 0;

  size_t size, resident;
  if (fscanf(f, "%zu %zu", &size, &resident) != 2) {
    fclose(f);
    return 0;
  }
  fclose(f);
  return resident * 4096;
}

typedef struct {
  uint64_t *samples;
  size_t count;
  size_t capacity;
} latency_samples_t;

static inline void latency_init(latency_samples_t *ls) {
  ls->capacity = BENCH_MAX_SAMPLES;
  ls->samples = (uint64_t *)malloc(ls->capacity * sizeof(uint64_t));
  ls->count = 0;
}

static inline void latency_record(latency_samples_t *ls, uint64_t ns) {
  if (ls->count < ls->capacity) {
    ls->samples[ls->count++] = ns;
  }
}

static int cmp_u64(const void *a, const void *b) {
  uint64_t va = *(const uint64_t *)a;
  uint64_t vb = *(const uint64_t *)b;
  return (va > vb) - (va < vb);
}

static inline void latency_compute(latency_samples_t *ls,
                                   bench_metrics_t *metrics) {
  if (ls->count == 0) {
    metrics->latency_p50_ns = 0;
    metrics->latency_p99_ns = 0;
    metrics->latency_p999_ns = 0;
    metrics->latency_max_ns = 0;
    return;
  }

  qsort(ls->samples, ls->count, sizeof(uint64_t), cmp_u64);

  metrics->latency_p50_ns = ls->samples[ls->count / 2];
  metrics->latency_p99_ns = ls->samples[(size_t)(ls->count * 0.99)];
  metrics->latency_p999_ns = ls->samples[(size_t)(ls->count * 0.999)];
  metrics->latency_max_ns = ls->samples[ls->count - 1];
}

static inline void latency_free(latency_samples_t *ls) {
  free(ls->samples);
  ls->samples = NULL;
  ls->count = 0;
}

// PRNG for Benchmarks

typedef struct {
  uint64_t state;
} bench_rng_t;

static inline void bench_rng_seed(bench_rng_t *rng, uint64_t seed) {
  rng->state = seed;
}

static inline uint64_t bench_rng_next(bench_rng_t *rng) {
  uint64_t x = rng->state;
  x ^= x << 13;
  x ^= x >> 7;
  x ^= x << 17;
  rng->state = x;
  return x;
}

static inline size_t bench_rng_range(bench_rng_t *rng, size_t min, size_t max) {
  return min + (bench_rng_next(rng) % (max - min + 1));
}

// this is random math, dont question it (its so it can mimic real world
// workloads)
static inline size_t bench_rng_powerlaw(bench_rng_t *rng, size_t min,
                                        size_t max, double alpha) {
  double u = (double)bench_rng_next(rng) / (double)UINT64_MAX;
  double min_a = pow((double)min, 1.0 - alpha);
  double max_a = pow((double)max, 1.0 - alpha);
  double x = pow(min_a + u * (max_a - min_a), 1.0 / (1.0 - alpha));
  return (size_t)x;
}

static inline void bench_run_workload(bench_workload_t *wl, allocator_t *alloc,
                                      bench_metrics_t *metrics, int num_runs) {
  printf("  Workload: " COLOR_CYAN "%s" COLOR_RESET " (%s)\n", wl->workload_id,
         wl->description);
  printf("  Iterations: %zu, Runs: %d\n", wl->iterations, num_runs);

  double best_throughput = 0;
  bench_metrics_t best_metrics = {0};

  for (int run = 0; run < num_runs; run++) {
    printf("    Run %d/%d... ", run + 1, num_runs);
    fflush(stdout);

    if (wl->setup)
      wl->setup(alloc);

    bench_metrics_t run_metrics = {0};
    wl->run(alloc, &run_metrics);

    if (wl->teardown)
      wl->teardown(alloc);

    printf(COLOR_GREEN "DONE" COLOR_RESET " (%.2fM ops/sec, p99=%lu ns)\n",
           run_metrics.throughput_ops_sec / 1e6,
           (unsigned long)run_metrics.latency_p99_ns);

    if (run_metrics.throughput_ops_sec > best_throughput) {
      best_throughput = run_metrics.throughput_ops_sec;
      best_metrics = run_metrics;
    }
  }

  *metrics = best_metrics;
  printf("  Best: %.2fM ops/sec, p50=%lu ns, p99=%lu ns, RSS=%zu KB\n\n",
         metrics->throughput_ops_sec / 1e6,
         (unsigned long)metrics->latency_p50_ns,
         (unsigned long)metrics->latency_p99_ns, metrics->rss_bytes / 1024);
}

static inline void bench_print_summary_header(void) {
  printf("\n%-12s %-12s %12s %10s %10s %10s %10s\n", "Allocator", "Workload",
         "Throughput", "p50", "p99", "p999", "RSS");
  printf("%-12s %-12s %12s %10s %10s %10s %10s\n", "", "", "(ops/sec)", "(ns)",
         "(ns)", "(ns)", "(KB)");
}

static inline void bench_print_summary_row(const char *allocator_name,
                                           const char *workload_id,
                                           bench_metrics_t *metrics) {
  printf("%-12s %-12s %12.2e %10lu %10lu %10lu %10zu\n", allocator_name,
         workload_id, metrics->throughput_ops_sec,
         (unsigned long)metrics->latency_p50_ns,
         (unsigned long)metrics->latency_p99_ns,
         (unsigned long)metrics->latency_p999_ns, metrics->rss_bytes / 1024);
}

#ifdef __cplusplus
}
#endif

#endif // BENCHMARK_H
