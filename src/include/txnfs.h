#include <uuid/uuid.h>
/* TXNFS data structures that needs to be exported */

#ifndef _TXNFS_H_
#define _TXNFS_H_

/* As we tested the length of sub-FSAL FH is 30 bytes */
#define FH_LEN 32

/* Default vector capacity */
#define TXN_CACHE_CAP 256

/* TXN cache. This is a continuously allocated vector
 * that buffers file handle insertion or removal.
 * @c entries is an array whose size and capacity are
 * given by the struct. */
struct txnfs_cache {
	uint32_t size;
	uint32_t capacity;
	struct txnfs_cache_entry *entries;
};

enum txnfs_cache_entry_type {
	txnfs_cache_entry_create = 0,
	txnfs_cache_entry_delete = 1,
	txnfs_cache_entry_modify = 2
};

struct txnfs_cache_entry {
	uuid_t uuid;
	enum txnfs_cache_entry_type entry_type;
	uint32_t hdl_size;
	union {
		/* The size of sub-FSAL's file handle we tested is 30 bytes */
		char data[FH_LEN];
		void *addr;
	} fh;
	struct gsh_buffdesc abs_path;
};

#define TXNCACHE_FH(entry) \
	(likely(entry->hdl_size <= FH_LEN) ? entry->fh.data : entry->fh.addr)

/**
 * @brief Iterate through the TXNFS cache entries
 *
 * @param[in] it	A pointer of struct txnfs_cache_entry for iteration
 * @param[in] cache	The pointer to the txnfs_cache struct to be iterated.
 */
#define txnfs_cache_foreach(it, cache) \
	for (it = cache->entries; it < cache->entries + cache->size; ++it)

#endif
