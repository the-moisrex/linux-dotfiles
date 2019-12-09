#include <benchmark/benchmark.h>

using namespace std;

static void EmptyRandom(benchmark::State& state) {

  for (auto _ : state) {

    // benchmark::DoNotOptimize(c);
  }

}
// Register the function as a benchmark
BENCHMARK(EmptyRandom);


// BENCHMARK_MAIN();
