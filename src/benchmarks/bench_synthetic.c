// bench_synthetic.c - Synthetic Benchmark Workloads
// Workloads: WL-SYN-001 through WL-SYN-010

#include "allocator.h"
#include "benchmark.h"
#include <string.h>

// WL-SYN-001: Small fixed 64B, immediate free (10M iterations)

static void wl_syn_001_run(allocator_t *alloc, bench_metrics_t *metrics) {
  const size_t iterations = 10000000;
  latency_samples_t lat;
  latency_init(&lat);

  for (size_t i = 0; i < BENCH_WARMUP_OPS; i++) {
    void *p = alloc->malloc(64);
    alloc->free(p);
  }

  size_t start_rss = bench_get_rss();
  uint64_t start = bench_get_time_ns();

  for (size_t i = 0; i < iterations; i++) {
    uint64_t op_start = bench_get_time_ns();
    void *p = alloc->malloc(64);
    alloc->free(p);

    if (i % BENCH_SAMPLE_INTERVAL == 0) {
      latency_record(&lat, bench_get_time_ns() - op_start);
    }
  }

  uint64_t elapsed = bench_get_time_ns() - start;
  size_t end_rss = bench_get_rss();

  metrics->throughput_ops_sec = (double)iterations / ((double)elapsed / 1e9);
  metrics->rss_bytes = (end_rss > start_rss) ? end_rss - start_rss : end_rss;
  latency_compute(&lat, metrics);
  latency_free(&lat);
}

// WL-SYN-002: Small random 16-256B uniform, immediate free (10M)

static void wl_syn_002_run(allocator_t *alloc, bench_metrics_t *metrics) {
  const size_t iterations = 10000000;
  latency_samples_t lat;
  latency_init(&lat);
  bench_rng_t rng;
  bench_rng_seed(&rng, 0x12345678);

  for (size_t i = 0; i < BENCH_WARMUP_OPS; i++) {
    size_t sz = bench_rng_range(&rng, 16, 256);
    void *p = alloc->malloc(sz);
    alloc->free(p);
  }

  bench_rng_seed(&rng, 0x12345678);
  uint64_t start = bench_get_time_ns();

  for (size_t i = 0; i < iterations; i++) {
    size_t sz = bench_rng_range(&rng, 16, 256);
    uint64_t op_start = bench_get_time_ns();
    void *p = alloc->malloc(sz);
    alloc->free(p);

    if (i % BENCH_SAMPLE_INTERVAL == 0) {
      latency_record(&lat, bench_get_time_ns() - op_start);
    }
  }

  uint64_t elapsed = bench_get_time_ns() - start;
  metrics->throughput_ops_sec = (double)iterations / ((double)elapsed / 1e9);
  metrics->rss_bytes = bench_get_rss();
  latency_compute(&lat, metrics);
  latency_free(&lat);
}

// WL-SYN-003: Medium fixed 4KB, immediate free (1M)

static void wl_syn_003_run(allocator_t *alloc, bench_metrics_t *metrics) {
  const size_t iterations = 1000000;
  latency_samples_t lat;
  latency_init(&lat);

  for (size_t i = 0; i < BENCH_WARMUP_OPS; i++) {
    void *p = alloc->malloc(4096);
    alloc->free(p);
  }

  uint64_t start = bench_get_time_ns();

  for (size_t i = 0; i < iterations; i++) {
    uint64_t op_start = bench_get_time_ns();
    void *p = alloc->malloc(4096);
    alloc->free(p);

    if (i % BENCH_SAMPLE_INTERVAL == 0) {
      latency_record(&lat, bench_get_time_ns() - op_start);
    }
  }

  uint64_t elapsed = bench_get_time_ns() - start;
  metrics->throughput_ops_sec = (double)iterations / ((double)elapsed / 1e9);
  metrics->rss_bytes = bench_get_rss();
  latency_compute(&lat, metrics);
  latency_free(&lat);
}

// WL-SYN-004: Medium random 1-64KB uniform, immediate free (1M)

static void wl_syn_004_run(allocator_t *alloc, bench_metrics_t *metrics) {
  const size_t iterations = 1000000;
  latency_samples_t lat;
  latency_init(&lat);
  bench_rng_t rng;
  bench_rng_seed(&rng, 0xDEADBEEF);

  for (size_t i = 0; i < BENCH_WARMUP_OPS; i++) {
    size_t sz = bench_rng_range(&rng, 1024, 65536);
    void *p = alloc->malloc(sz);
    alloc->free(p);
  }

  bench_rng_seed(&rng, 0xDEADBEEF);
  uint64_t start = bench_get_time_ns();

  for (size_t i = 0; i < iterations; i++) {
    size_t sz = bench_rng_range(&rng, 1024, 65536);
    uint64_t op_start = bench_get_time_ns();
    void *p = alloc->malloc(sz);
    alloc->free(p);

    if (i % BENCH_SAMPLE_INTERVAL == 0) {
      latency_record(&lat, bench_get_time_ns() - op_start);
    }
  }

  uint64_t elapsed = bench_get_time_ns() - start;
  metrics->throughput_ops_sec = (double)iterations / ((double)elapsed / 1e9);
  metrics->rss_bytes = bench_get_rss();
  latency_compute(&lat, metrics);
  latency_free(&lat);
}

// WL-SYN-005: Large fixed 1MB, immediate free (100K)

static void wl_syn_005_run(allocator_t *alloc, bench_metrics_t *metrics) {
  const size_t iterations = 100000;
  latency_samples_t lat;
  latency_init(&lat);

  for (size_t i = 0; i < 1000; i++) {
    void *p = alloc->malloc(1048576);
    alloc->free(p);
  }

  uint64_t start = bench_get_time_ns();

  for (size_t i = 0; i < iterations; i++) {
    uint64_t op_start = bench_get_time_ns();
    void *p = alloc->malloc(1048576);
    alloc->free(p);

    if (i % 10 == 0) {
      latency_record(&lat, bench_get_time_ns() - op_start);
    }
  }

  uint64_t elapsed = bench_get_time_ns() - start;
  metrics->throughput_ops_sec = (double)iterations / ((double)elapsed / 1e9);
  metrics->rss_bytes = bench_get_rss();
  latency_compute(&lat, metrics);
  latency_free(&lat);
}

// WL-SYN-006: Large random 64KB-4MB uniform, immediate free (100K)

static void wl_syn_006_run(allocator_t *alloc, bench_metrics_t *metrics) {
  const size_t iterations = 100000;
  latency_samples_t lat;
  latency_init(&lat);
  bench_rng_t rng;
  bench_rng_seed(&rng, 0xCAFEBABE);

  for (size_t i = 0; i < 1000; i++) {
    size_t sz = bench_rng_range(&rng, 65536, 4194304);
    void *p = alloc->malloc(sz);
    alloc->free(p);
  }

  bench_rng_seed(&rng, 0xCAFEBABE);
  uint64_t start = bench_get_time_ns();

  for (size_t i = 0; i < iterations; i++) {
    size_t sz = bench_rng_range(&rng, 65536, 4194304);
    uint64_t op_start = bench_get_time_ns();
    void *p = alloc->malloc(sz);
    alloc->free(p);

    if (i % 10 == 0) {
      latency_record(&lat, bench_get_time_ns() - op_start);
    }
  }

  uint64_t elapsed = bench_get_time_ns() - start;
  metrics->throughput_ops_sec = (double)iterations / ((double)elapsed / 1e9);
  metrics->rss_bytes = bench_get_rss();
  latency_compute(&lat, metrics);
  latency_free(&lat);
}

// WL-SYN-007: Mixed sizes power-law, batch free (10M)

static void wl_syn_007_run(allocator_t *alloc, bench_metrics_t *metrics) {
  const size_t iterations = 10000000;
  const size_t batch_size = 1000;
  latency_samples_t lat;
  latency_init(&lat);
  bench_rng_t rng;
  bench_rng_seed(&rng, 0xFEEDFACE);

  void *batch[1000];

  uint64_t start = bench_get_time_ns();
  size_t total_ops = 0;

  while (total_ops < iterations) {
    for (size_t i = 0; i < batch_size && total_ops < iterations; i++) {
      size_t sz = bench_rng_powerlaw(&rng, 16, 65536, 2.0);
      uint64_t op_start = bench_get_time_ns();
      batch[i] = alloc->malloc(sz);

      if (total_ops % BENCH_SAMPLE_INTERVAL == 0) {
        latency_record(&lat, bench_get_time_ns() - op_start);
      }
      total_ops++;
    }

    for (size_t i = 0; i < batch_size; i++) {
      if (batch[i]) {
        alloc->free(batch[i]);
        batch[i] = NULL;
      }
    }
  }

  uint64_t elapsed = bench_get_time_ns() - start;
  metrics->throughput_ops_sec = (double)iterations / ((double)elapsed / 1e9);
  metrics->rss_bytes = bench_get_rss();
  latency_compute(&lat, metrics);
  latency_free(&lat);
}

// WL-SYN-008: Realloc grow chain 16B → 4KB (1M)

static void wl_syn_008_run(allocator_t *alloc, bench_metrics_t *metrics) {
  const size_t iterations = 1000000;
  latency_samples_t lat;
  latency_init(&lat);

  uint64_t start = bench_get_time_ns();

  for (size_t i = 0; i < iterations; i++) {
    void *p = alloc->malloc(16);

    for (size_t sz = 32; sz <= 4096; sz *= 2) {
      uint64_t op_start = bench_get_time_ns();
      p = alloc->realloc(p, sz);

      if (i % BENCH_SAMPLE_INTERVAL == 0 && sz == 4096) {
        latency_record(&lat, bench_get_time_ns() - op_start);
      }
    }

    alloc->free(p);
  }

  uint64_t elapsed = bench_get_time_ns() - start;
  metrics->throughput_ops_sec =
      (double)(iterations * 8) / ((double)elapsed / 1e9);
  metrics->rss_bytes = bench_get_rss();
  latency_compute(&lat, metrics);
  latency_free(&lat);
}

// WL-SYN-009: Realloc shrink chain 4KB → 16B (1M)

static void wl_syn_009_run(allocator_t *alloc, bench_metrics_t *metrics) {
  const size_t iterations = 1000000;
  latency_samples_t lat;
  latency_init(&lat);

  uint64_t start = bench_get_time_ns();

  for (size_t i = 0; i < iterations; i++) {
    void *p = alloc->malloc(4096);

    for (size_t sz = 2048; sz >= 16; sz /= 2) {
      uint64_t op_start = bench_get_time_ns();
      p = alloc->realloc(p, sz);

      if (i % BENCH_SAMPLE_INTERVAL == 0 && sz == 16) {
        latency_record(&lat, bench_get_time_ns() - op_start);
      }
    }

    alloc->free(p);
  }

  uint64_t elapsed = bench_get_time_ns() - start;
  metrics->throughput_ops_sec =
      (double)(iterations * 8) / ((double)elapsed / 1e9);
  metrics->rss_bytes = bench_get_rss();
  latency_compute(&lat, metrics);
  latency_free(&lat);
}

// WL-SYN-010: Calloc 16-4KB, immediate free (1M)

static void wl_syn_010_run(allocator_t *alloc, bench_metrics_t *metrics) {
  const size_t iterations = 1000000;
  latency_samples_t lat;
  latency_init(&lat);
  bench_rng_t rng;
  bench_rng_seed(&rng, 0xABCD1234);

  for (size_t i = 0; i < BENCH_WARMUP_OPS; i++) {
    size_t nmemb = bench_rng_range(&rng, 1, 256);
    size_t size = bench_rng_range(&rng, 16, 16);
    void *p = alloc->calloc(nmemb, size);
    alloc->free(p);
  }

  bench_rng_seed(&rng, 0xABCD1234);
  uint64_t start = bench_get_time_ns();

  for (size_t i = 0; i < iterations; i++) {
    size_t nmemb = bench_rng_range(&rng, 1, 256);
    size_t size = bench_rng_range(&rng, 16, 16);

    uint64_t op_start = bench_get_time_ns();
    void *p = alloc->calloc(nmemb, size);
    alloc->free(p);

    if (i % BENCH_SAMPLE_INTERVAL == 0) {
      latency_record(&lat, bench_get_time_ns() - op_start);
    }
  }

  uint64_t elapsed = bench_get_time_ns() - start;
  metrics->throughput_ops_sec = (double)iterations / ((double)elapsed / 1e9);
  metrics->rss_bytes = bench_get_rss();
  latency_compute(&lat, metrics);
  latency_free(&lat);
}

// Workload Registration

bench_workload_t synthetic_workloads[] = {
    {"WL-SYN-001", "Small fixed 64B", NULL, wl_syn_001_run, NULL, 10000000},
    {"WL-SYN-002", "Small random 16-256B", NULL, wl_syn_002_run, NULL,
     10000000},
    {"WL-SYN-003", "Medium fixed 4KB", NULL, wl_syn_003_run, NULL, 1000000},
    {"WL-SYN-004", "Medium random 1-64KB", NULL, wl_syn_004_run, NULL, 1000000},
    {"WL-SYN-005", "Large fixed 1MB", NULL, wl_syn_005_run, NULL, 100000},
    {"WL-SYN-006", "Large random 64KB-4MB", NULL, wl_syn_006_run, NULL, 100000},
    {"WL-SYN-007", "Mixed power-law batch", NULL, wl_syn_007_run, NULL,
     10000000},
    {"WL-SYN-008", "Realloc grow chain", NULL, wl_syn_008_run, NULL, 1000000},
    {"WL-SYN-009", "Realloc shrink chain", NULL, wl_syn_009_run, NULL, 1000000},
    {"WL-SYN-010", "Calloc random", NULL, wl_syn_010_run, NULL, 1000000},
};

const size_t num_synthetic_workloads =
    sizeof(synthetic_workloads) / sizeof(synthetic_workloads[0]);
