#include <benchmark/benchmark.h>

#include <stdio.h>
#include <string.h>
#include <thread>

#include "id_manager.h"

static void BM_generate_ids(benchmark::State& state) {
  const db_store_t* db = init_db_store("testdb", true);
  initialize_id_manager(db);

  while (state.KeepRunning()) {
    for (int i = 0; i < state.range(0); i++) {
      free(generate_file_id(db));
    }
  }
  destroy_db_store(db);
}

BENCHMARK(BM_generate_ids)->Arg(512);

BENCHMARK_MAIN();
