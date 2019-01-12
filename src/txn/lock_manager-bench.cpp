#include <benchmark/benchmark.h>

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "lock_manager.hpp"

using namespace std;

std::vector<LockRequest> make_lock_requests(std::vector<std::pair<const char *, bool>> data) {
        std::vector<LockRequest> res;
        for (const auto &pair : data) {
                LockRequest lock_request;
                lock_request.path = (char*) pair.first;
                lock_request.write_lock = pair.second;
                res.push_back(lock_request);
        }
        return res;
}

static vector<string> get_paths(int n) {
        ifstream paths_file("../txn/linux-4.6.3-paths");
        vector<string> paths;
        string line;
        bool unlimited = n == -1;
        while (getline(paths_file, line) && (unlimited || n > 0)) {
                paths.push_back(line);
                n--;
        }
        return paths;
}

static vector<string> get_paths() { return get_paths(-1); }

static void BM_insert_paths(benchmark::State &state) {
        vector<string> paths = get_paths(state.range(0));
        while (state.KeepRunning()) {
                LockManager lm;
                vector<LockHandle> handles;
                for (auto &path : paths) {
                        auto lock_request = make_lock_requests({{path.c_str(), false}});

                        handles.push_back(lm.lock(lock_request.data(), lock_request.size()));
                }
                for (auto &handle : handles) {
                        handle.unlock();
                }
        }
}

BENCHMARK(BM_insert_paths)->Range(1, 32768);

static void BM_insert_set(benchmark::State &state) {
        vector<string> paths = get_paths(state.range(0));
        while (state.KeepRunning()) {
                LockManager lm;
                vector<LockRequest> lock_requests;
                for (auto &path : paths) {
                        LockRequest lock_request;
                        lock_request.path = (char*) path.c_str();
                        lock_request.write_lock = false;
                        lock_requests.push_back(lock_request);
                }
                LockHandle handle = lm.lock(lock_requests.data(), lock_requests.size());
                handle.unlock();
        }
}

BENCHMARK(BM_insert_set)->Range(1, 32768);

static void BM_try_lock(benchmark::State &state) {
        vector<string> paths = get_paths(state.range(0));
        LockManager lm;
        vector<LockHandle> handles;
        for (auto &path : paths) {
                auto lock_request = make_lock_requests({{path.c_str(), false}});
                handles.push_back(lm.lock(lock_request.data(), lock_request.size()));
        }
        while (state.KeepRunning()) {
                for (auto &path : paths) {
                        auto lock_request = make_lock_requests({{path.c_str(), true}});
                        lm.try_lock(lock_request.data(), lock_request.size());
                }
        }
        for (auto &handle : handles) {
                handle.unlock();
        }
}
BENCHMARK(BM_try_lock)->Range(1, 32768);

BENCHMARK_MAIN();
