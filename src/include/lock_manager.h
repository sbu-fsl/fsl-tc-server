#ifndef _LOCK_MANAGER_H
#define _LOCK_MANAGER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Structure used for passing the paths to be locked to the LockManager
struct lock_request {
        char *path;
        bool write_lock;
};

// Opaque types for C interface
typedef struct lock_request lock_request_t;
typedef struct lock_manager lock_manager_t;
typedef struct lock_handle lock_handle_t;

//  lock_manager_t must be freed with free_lock_manager()
lock_manager_t *new_lock_manager();
void free_lock_manager(lock_manager_t *lm);

// lock_handle_t* can be unlocked with call to unlock_handle().
// This also frees the lock_handle_t*
// Caller maintains ownership of `files` and all of the contents
// lm_try_lock() returns a nullptr if the lock cannot be acquired
// In this case, unlock_handle() should not be called.
lock_handle_t *lm_lock(lock_manager_t* lm, lock_request_t *files, int n);
lock_handle_t *lm_try_lock(lock_manager_t* lm, lock_request_t *files, int n);

void unlock_handle(lock_handle_t *lh);

#ifdef __cplusplus
}
#endif


#endif  //_LOCK_MANAGER_H

