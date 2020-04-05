#include "pch.h"
using namespace std;

static void EmptyRandom(benchmark::State& state) {

  for (auto _ : state) {

    // benchmark::DoNotOptimize(c);
  }
}
BENCHMARK(EmptyRandom);






// BENCHMARK_MAIN();
