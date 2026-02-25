#define _POSIX_C_SOURCE 199309L

#include <math.h>
#include <time.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>

#include "allocator.h"

#define BENCH_MAX_SAMPLES 1000000

// allocator to benchmark/debug (provided by linked allocator object)
extern "C" allocator_t *get_bench_allocator(void);

static inline uint64_t bench_get_time_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static inline size_t bench_get_rss(void) {
  FILE *f = fopen("/proc/self/statm", "r");
  if (!f)
    return 0;

  size_t size = 0;
  size_t resident = 0;
  if (fscanf(f, "%zu %zu", &size, &resident) != 2) {
    fclose(f);
    return 0;
  }
  fclose(f);
  return resident * 4096;
}

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
  uint64_t *samples;
  size_t count;
  size_t capacity;
} latency_samples_t;

typedef struct {
  uint64_t state;
} bench_rng_t;

static int cmp_u64(const void *a, const void *b) {
  uint64_t va = *(const uint64_t *)a;
  uint64_t vb = *(const uint64_t *)b;
  return (va > vb) - (va < vb);
}

static inline uint64_t bench_rng_next(bench_rng_t *rng) {
  uint64_t x = rng->state;
  x ^= x << 13;
  x ^= x >> 7;
  x ^= x << 17;
  rng->state = x;
  return x;
}

static inline void bench_rng_seed(bench_rng_t *rng, uint64_t seed) {
  rng->state = seed;
}

static inline size_t bench_rng_powerlaw(bench_rng_t *rng, size_t min,
                                        size_t max, double alpha) {
  double u = (double)bench_rng_next(rng) / (double)UINT64_MAX;
  double min_a = pow((double)min, 1.0 - alpha);
  double max_a = pow((double)max, 1.0 - alpha);
  double x = pow(min_a + u * (max_a - min_a), 1.0 / (1.0 - alpha));
  return (size_t)x;
}

static inline bool latency_init(latency_samples_t *ls) {
  ls->capacity = BENCH_MAX_SAMPLES;
  ls->samples = (uint64_t *)malloc(ls->capacity * sizeof(uint64_t));
  ls->count = 0;
  return ls->samples != nullptr;
}

static inline void latency_record(latency_samples_t *ls, uint64_t ns) {
  if (ls->count < ls->capacity) {
    ls->samples[ls->count++] = ns;
  }
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
  ls->samples = nullptr;
  ls->count = 0;
  ls->capacity = 0;
}

int bench(size_t iterations, size_t batch_size) {
  allocator_t *alloc = get_bench_allocator();
  if (!alloc) {
    fprintf(stderr, "ERROR: get_bench_allocator() returned NULL\n");
    return 1;
  }

  if (batch_size == 0) {
    fprintf(stderr, "ERROR: batch_size must be > 0\n");
    return 1;
  }

  bench_metrics_t metrics{};
  latency_samples_t lat{};
  if (!latency_init(&lat)) {
    fprintf(stderr, "ERROR: failed to allocate latency sample buffer\n");
    return 1;
  }

  bench_rng_t rng;
  bench_rng_seed(&rng, 0xFEEDFACE);

  void **batch = (void **)malloc(batch_size * sizeof(void *));
  if (!batch) {
    latency_free(&lat);
    fprintf(stderr, "ERROR: failed to allocate batch array\n");
    return 1;
  }
  memset(batch, 0, batch_size * sizeof(void *));

  uint64_t start = bench_get_time_ns();
  size_t total_ops = 0;

  while (total_ops < iterations) {
    for (size_t i = 0; i < batch_size && total_ops < iterations; i++) {
      size_t sz = bench_rng_powerlaw(&rng, 16, 65536, 2.0);
      uint64_t op_start = bench_get_time_ns();
      batch[i] = alloc->malloc ? alloc->malloc(sz) : nullptr;

      if (total_ops % 100 == 0) {
        latency_record(&lat, bench_get_time_ns() - op_start);
      }
      total_ops++;
    }

    for (size_t i = 0; i < batch_size; i++) {
      if (batch[i] && alloc->free) {
        alloc->free(batch[i]);
        batch[i] = nullptr;
      }
    }
  }

  uint64_t elapsed = bench_get_time_ns() - start;
  metrics.throughput_ops_sec =
      (double)iterations / ((double)elapsed / 1e9);
  metrics.rss_bytes = bench_get_rss();
  latency_compute(&lat, &metrics);

  printf("bench results:\n");
  printf("  iterations:      %zu\n", iterations);
  printf("  batch size:      %zu\n", batch_size);
  printf("  throughput:      %.2f ops/sec\n", metrics.throughput_ops_sec);
  printf("  latency p50:     %lu ns\n", (unsigned long)metrics.latency_p50_ns);
  printf("  latency p99:     %lu ns\n", (unsigned long)metrics.latency_p99_ns);
  printf("  latency p99.9:   %lu ns\n", (unsigned long)metrics.latency_p999_ns);
  printf("  latency max:     %lu ns\n", (unsigned long)metrics.latency_max_ns);
  printf("  rss:             %zu bytes\n", metrics.rss_bytes);

  free(batch);
  latency_free(&lat);
  return 0;
}

static void print_help(void) {
  printf("commands:\n");
  printf("  help\n");
  printf("  alloc <id> <size>\n");
  printf("  calloc <id> <nmemb> <size>\n");
  printf("  realloc <id> <size>\n");
  printf("  free <id>\n");
  printf("  usable <id>\n");
  printf("  fill <id> <byte> <count>\n");
  printf("  dump <id> <count>\n");
  printf("  list\n");
  printf("  stats\n");
  printf("  validate\n");
  printf("  bench [iterations] [batch_size]\n");
  printf("  quit\n");
}

int main(void) {
  allocator_t *alloc = get_bench_allocator();
  if (!alloc) {
    fprintf(stderr, "ERROR: get_bench_allocator() returned NULL\n");
    return 1;
  }

  if (alloc->init) {
    int init_result = alloc->init();
    if (init_result != 0) {
      fprintf(stderr, "ERROR: Allocator init() failed with code %d\n", init_result);
      return 1;
    }
  }

  std::unordered_map<std::string, void *> blocks;

  printf("Zialloc debug shell. Type 'help' for commands.\n");

  std::string line;
  while (true) {
    std::cout << "zialloc> " << std::flush;
    if (!std::getline(std::cin, line)) {
      break;
    }

    std::istringstream iss(line);
    std::string cmd;
    if (!(iss >> cmd)) {
      continue;
    }

    if (cmd == "help") {
      print_help();
      continue;
    }

    if (cmd == "quit" || cmd == "exit") {
      break;
    }

    if (cmd == "alloc") {
      std::string id;
      size_t size = 0;
      if (!(iss >> id >> size)) {
        printf("usage: alloc <id> <size>\n");
        continue;
      }
      if (blocks.count(id) != 0) {
        printf("id '%s' already exists\n", id.c_str());
        continue;
      }
      void *p = alloc->malloc ? alloc->malloc(size) : nullptr;
      blocks[id] = p;
      printf("%s = %p\n", id.c_str(), p);
      continue;
    }

    if (cmd == "calloc") {
      std::string id;
      size_t nmemb = 0;
      size_t size = 0;
      if (!(iss >> id >> nmemb >> size)) {
        printf("usage: calloc <id> <nmemb> <size>\n");
        continue;
      }
      if (blocks.count(id) != 0) {
        printf("id '%s' already exists\n", id.c_str());
        continue;
      }
      void *p = alloc->calloc ? alloc->calloc(nmemb, size) : nullptr;
      blocks[id] = p;
      printf("%s = %p\n", id.c_str(), p);
      continue;
    }

    if (cmd == "realloc") {
      std::string id;
      size_t size = 0;
      if (!(iss >> id >> size)) {
        printf("usage: realloc <id> <size>\n");
        continue;
      }
      auto it = blocks.find(id);
      if (it == blocks.end()) {
        printf("unknown id '%s'\n", id.c_str());
        continue;
      }
      void *oldp = it->second;
      void *newp = alloc->realloc ? alloc->realloc(oldp, size) : nullptr;
      it->second = newp;
      printf("%s: %p -> %p\n", id.c_str(), oldp, newp);
      continue;
    }

    if (cmd == "free") {
      std::string id;
      if (!(iss >> id)) {
        printf("usage: free <id>\n");
        continue;
      }
      auto it = blocks.find(id);
      if (it == blocks.end()) {
        printf("unknown id '%s'\n", id.c_str());
        continue;
      }
      if (alloc->free) {
        alloc->free(it->second);
      }
      printf("freed %s (%p)\n", id.c_str(), it->second);
      blocks.erase(it);
      continue;
    }

    if (cmd == "usable") {
      std::string id;
      if (!(iss >> id)) {
        printf("usage: usable <id>\n");
        continue;
      }
      auto it = blocks.find(id);
      if (it == blocks.end()) {
        printf("unknown id '%s'\n", id.c_str());
        continue;
      }
      if (!alloc->usable_size) {
        printf("usable_size not implemented\n");
        continue;
      }
      size_t usz = alloc->usable_size(it->second);
      printf("usable(%s) = %zu\n", id.c_str(), usz);
      continue;
    }

    if (cmd == "fill") {
      std::string id;
      unsigned value = 0;
      size_t count = 0;
      if (!(iss >> id >> value >> count)) {
        printf("usage: fill <id> <byte> <count>\n");
        continue;
      }
      auto it = blocks.find(id);
      if (it == blocks.end()) {
        printf("unknown id '%s'\n", id.c_str());
        continue;
      }
      if (!it->second) {
        printf("id '%s' is null\n", id.c_str());
        continue;
      }
      size_t limit = count;
      if (alloc->usable_size) {
        size_t usz = alloc->usable_size(it->second);
        if (limit > usz)
          limit = usz;
      }
      memset(it->second, (int)(value & 0xFFu), limit);
      printf("filled %zu bytes at %s (%p) with 0x%02X\n", limit, id.c_str(),
             it->second, (value & 0xFFu));
      continue;
    }

    if (cmd == "dump") {
      std::string id;
      size_t count = 0;
      if (!(iss >> id >> count)) {
        printf("usage: dump <id> <count>\n");
        continue;
      }
      auto it = blocks.find(id);
      if (it == blocks.end()) {
        printf("unknown id '%s'\n", id.c_str());
        continue;
      }
      if (!it->second) {
        printf("id '%s' is null\n", id.c_str());
        continue;
      }
      size_t limit = count;
      if (alloc->usable_size) {
        size_t usz = alloc->usable_size(it->second);
        if (limit > usz)
          limit = usz;
      }
      const unsigned char *bytes = (const unsigned char *)it->second;
      printf("dump %s (%p), %zu bytes:\n", id.c_str(), it->second, limit);
      for (size_t i = 0; i < limit; ++i) {
        printf("%02X%s", bytes[i], ((i + 1) % 16 == 0) ? "\n" : " ");
      }
      if (limit % 16 != 0)
        printf("\n");
      continue;
    }

    if (cmd == "list") {
      printf("live blocks: %zu\n", blocks.size());
      for (const auto &kv : blocks) {
        printf("  %s => %p\n", kv.first.c_str(), kv.second);
      }
      continue;
    }

    if (cmd == "stats") {
      if (alloc->print_stats) {
        alloc->print_stats();
      } else {
        printf("print_stats not implemented\n");
      }
      continue;
    }

    if (cmd == "validate") {
      if (!alloc->validate_heap) {
        printf("validate_heap not implemented\n");
        continue;
      }
      bool ok = alloc->validate_heap();
      printf("heap validate: %s\n", ok ? "ok" : "FAILED");
      continue;
    }

    if (cmd == "bench") {
      size_t iterations = 10000000;
      size_t batch_size = 1000;
      if (!(iss >> iterations)) {
        iterations = 10000000;
      }
      if (!(iss >> batch_size)) {
        batch_size = 1000;
      }
      (void)bench(iterations, batch_size);
      continue;
    }

    printf("unknown command: %s\n", cmd.c_str());
    print_help();
  }

  for (auto &kv : blocks) {
    if (alloc->free && kv.second) {
      alloc->free(kv.second);
    }
  }
  blocks.clear();

  if (alloc->teardown) {
    alloc->teardown();
  }

  return 0;
}
