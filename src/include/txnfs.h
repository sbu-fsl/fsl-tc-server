/* TXNFS data structures that needs to be exported */

/* As we tested the length of sub-FSAL FH is 30 bytes */
#define FH_LEN 32

/* Default vector capacity */
#define CACHE_CAP 256

struct txnfs_cache {
	uint32_t size;
	uint32_t capacity;
	struct txnfs_cache_entry *entries;
};

enum txnfs_cache_entry_type {
	txnfs_cache_entry_create = 0,
	txnfs_cache_entry_delete = 1
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
};

