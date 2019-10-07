#ifndef _LOCK_MANAGER_HPP
#define _LOCK_MANAGER_HPP

#include <linux/limits.h>
#include <unordered_map>
#include <mutex>
#include <string>
#include <vector>
#include <set>

#include "path_utils.h"
#include "util/slice.h"

// Contains C interface
#include "lock_manager.h"
typedef struct lock_request LockRequest;

// Structure internally used for passing the clean paths to be locked to the LockManager
struct CleanLockRequest {
        std::string clean_path;
        bool write_lock;
};

struct CleanLockRequestCompare {
        bool operator()(const CleanLockRequest &lhs, const CleanLockRequest &rhs) {
                return lhs.clean_path < rhs.clean_path;
        }
};

struct LockHandle;

struct LockManager {
private:
        // Make LockHandle a friend so it can call unlock
        friend struct LockHandle;

        // The data associated with each locked path in the LockManager -- a
        // flag for read/write and a reference count
        struct LockRefCount {
                // Whether or not the file is being held for writing. This must
                // be stored in the LockManager alongside the path so we know
                // whether or not a new reader can acquire the lock jointly.
                bool write_lock;

                std::mutex refcount_mutex;
                // Reference count for the number of concurrent readers holding
                // the lock
                int refcount;

                LockRefCount(bool write_lock);
        };

        std::mutex paths_mutex;
        // Associate all locked paths with a `LockRefCount`: a struct indicating
        // whether or not the lock is a write lock and a reference count of the
        // number of readers holding the lock
        std::unordered_map<std::string, LockRefCount> paths;

        bool could_lock(CleanLockRequest file);
        LockHandle try_lock_clean_paths(std::set<CleanLockRequest, CleanLockRequestCompare> paths);
        LockHandle add_lock(std::string path, bool write_lock);
        void unlock(std::vector<std::string> paths);

       public:
        LockManager();

        // For both lock and try_lock, `path` represents a filesystem sub-tree
        // -- it can be either a directory or a file. The caller is responsible
        // for examining the LockHandle to see if the operation succeeded and
        // for unlocking the LockHandle so all resources can be freed

        // This method blocks until the lock can be acquired.
        LockHandle lock(LockRequest *files, int n);

        // Only attempt to acquire lock -- do not block if the lock cannot be
        // acquired
        LockHandle try_lock(LockRequest *files, int n);
};

struct LockHandle {
        LockManager *lock_manager;

        std::vector<std::string> paths;

        bool success;

        LockHandle();
        LockHandle(LockManager *lock_manager, std::vector<std::string> paths, bool success);

        // Decrement the refcount of the path associated with this LockHandle
        void unlock();
};

#endif  //_LOCK_MANAGER_HPP
