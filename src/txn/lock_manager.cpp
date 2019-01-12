#include "lock_manager.hpp"

#include <algorithm>
#include <linux/limits.h>
#include <stdio.h>

#include "util/path_utils.h"
#include "util/slice.h"

LockManager::LockRefCount::LockRefCount(bool write_lock)
    : write_lock(write_lock), refcount(1) {}

LockManager::LockManager() : paths() {}

std::string clean_path(char *path, bool *success) {
        char *clean_path = (char *)malloc(PATH_MAX);
        if (!clean_path) {
                *success = false;
                return std::string();
        }
        int ret = tc_path_normalize(path, clean_path, PATH_MAX);
        if (ret == -1) {
                // Logging or error codes?
                free(clean_path);
                *success = false;
                return std::string();
        }
        *success = true;
        std::string clean_string(clean_path);
        free(clean_path);
        return clean_string;
}

bool clean_paths(std::set<CleanLockRequest, CleanLockRequestCompare> *paths, LockRequest *files, int n) {
        for (int i = 0; i < n; i++)
        {
                struct CleanLockRequest clean_lock_request;
                bool ret;
                clean_lock_request.clean_path = clean_path(files[i].path, &ret);
                if (!ret) {
                        return false;
                }
                clean_lock_request.write_lock = files[i].write_lock;

                auto it = paths->find(clean_lock_request);
                if (it != paths->end()) {
                    // If this file has already been locked, we need to check
                    // its write_lock because we may have locked it for reading
                    // but now want to lock it for writing. Write lock requests
                    // supersede any read lock requests, regardless of order.
                    // We can't just modify the write_lock in place because
                    // std::set elements are immutable (even though our
                    // comparison function doesn't check `write_lock` so it
                    // would technically be okay) so we erase and re-insert

                    clean_lock_request.write_lock = clean_lock_request.write_lock || it->write_lock;
                    paths->erase(it);
                }
                paths->insert(clean_lock_request);
        }
        return true;
}

void LockManager::unlock(std::vector<std::string> files) {
        std::lock_guard<std::mutex> lock(paths_mutex);

        for (const auto &path : files) {
                auto it = paths.find(path);

                if (it != paths.end()) {
                        std::lock_guard<std::mutex> lock(it->second.refcount_mutex);
                        it->second.refcount--;

                        if (it->second.refcount == 0) {
                                paths.erase(it);
                        }
                } else {
                        printf("failed to unlock %s\n", path.c_str());
                        // What should we do if the path DNE or has already been
                        // unlocked?
                }

        }
}

bool LockManager::could_lock(CleanLockRequest file) {
        auto it = paths.find(file.clean_path);
        if (it == paths.end()) {
                return true;
        }
        return !file.write_lock && !it->second.write_lock;
}

LockHandle LockManager::lock(LockRequest *files, int n) {
        std::set<CleanLockRequest, CleanLockRequestCompare> clean_lock_requests;

        bool ret = clean_paths(&clean_lock_requests, files, n);
        if (!ret) {
                return LockHandle();
        }

        LockHandle lock_handle;
        // Spinlock until try_lock succeeds. Better way to do this?
        do {
                lock_handle = try_lock_clean_paths(clean_lock_requests);
        } while (!lock_handle.success);

        return lock_handle;
}

LockHandle LockManager::try_lock(LockRequest *files, int n) {
        std::set<CleanLockRequest, CleanLockRequestCompare> paths;

        bool ret = clean_paths(&paths, files, n);
        if (!ret) {
                return LockHandle();
        }

        return try_lock_clean_paths(paths);
}

LockHandle LockManager::try_lock_clean_paths(std::set<CleanLockRequest, CleanLockRequestCompare> files) {
        // Blocks to acquire paths_mutex. Should this do paths_mutex.try_lock()
        // and return nullptr if it fails? What about LockManager::lock()?
        std::lock_guard<std::mutex> lock(paths_mutex);

        bool ready = true;
        for (const auto &clean_lock_request : files) {
                ready &= could_lock(clean_lock_request);
        }
        if (!ready) {
                return LockHandle();
        }
        std::vector<std::string> locked_paths;
        for (const auto &clean_lock_request : files) {
                auto it = paths.find(clean_lock_request.clean_path);
                if (it != paths.end()) {
                        assert(!clean_lock_request.write_lock && !it->second.write_lock);
                        std::lock_guard<std::mutex> lock(
                            it->second.refcount_mutex);
                        it->second.refcount++;
                } else {
                        paths.emplace(std::piecewise_construct,
                                std::forward_as_tuple(clean_lock_request.clean_path),
                                std::forward_as_tuple(clean_lock_request.write_lock));
                }
                locked_paths.push_back(clean_lock_request.clean_path);

        }

        return LockHandle(this, locked_paths, true);
}

LockHandle::LockHandle()
    : lock_manager(nullptr), paths(), success(false) {}

LockHandle::LockHandle(LockManager *lock_manager, std::vector<std::string> paths, bool success)
    : lock_manager(lock_manager), paths(paths), success(success) {}

void LockHandle::unlock() {
        assert(success);
        lock_manager->unlock(paths);
}

lock_manager_t *new_lock_manager() {
        return (lock_manager_t*) new LockManager();
}

void free_lock_manager(lock_manager_t *lm) {
        delete (LockManager*) lm;
}

lock_handle_t *lm_lock(lock_manager_t* lm, lock_request_t *files, int n) {
        LockManager *lm_ = (LockManager*) lm;
        LockHandle *lh = new LockHandle;
        *lh = lm_->lock((LockRequest*) files, n);
        if (!lh->success) {
                delete lh;
                return nullptr;
        }
        return (lock_handle_t*) lh;
}
lock_handle_t *lm_try_lock(lock_manager_t* lm, lock_request_t *files, int n) {
        LockManager *lm_ = (LockManager*) lm;
        LockHandle *lh = new LockHandle;
        *lh = lm_->try_lock((LockRequest*) files, n);
        if (!lh->success) {
                delete lh;
                return nullptr;
        }
        return (lock_handle_t*) lh;
}

void unlock_handle(lock_handle_t *lh) {
        ((LockHandle*)lh)->unlock();
        delete (LockHandle*) lh;
}
