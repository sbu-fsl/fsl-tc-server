
#undef TRACEPOINT_PROVIDER
#define TRACEPOINT_PROVIDER txnfs

#if !defined(GANESHA_LTTNG_TXNFS_TP_H) || \
	defined(TRACEPOINT_HEADER_MULTI_READ)
#define GANESHA_LTTNG_TXNFS_TP_H

#include <lttng/tracepoint.h>
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Trace the exit of backup_nfs4_op function
 * 
 * The total time spent on backing up can be derived when
 * the trace point `nfs_rpc:v4op_start` is also enabled
 * 
 * @param[in] txnid	Transaction ID
 * @param[in] opidx	The index of the operation in compound
 * @param[in] opcode	NFS4 operation number
 * @param[in] opname	Text name of the operation
 */
TRACEPOINT_EVENT(
	txnfs,
	end_backup,
	TP_ARGS(
		uint64_t, txnid,
		int, opidx,
		int, opcode,
		const char *, opname),
	TP_FIELDS(
		ctf_integer_hex(int, txnid, txnid)
		ctf_integer(int, opidx, opidx)
		ctf_integer(int, opcode, opcode)
		ctf_string(opname, opname)
	)
)

TRACEPOINT_LOGLEVEL(
	txnfs,
	end_backup,
	TRACE_INFO
)

/**
 * @brief Trace (before) backup OPEN operation
 * 
 * @param[in] txnid	Transaction ID
 * @param[in] is_create	Is this creation?
 */
TRACEPOINT_EVENT(
	txnfs,
	backup_open,
	TP_ARGS(
		uint64_t, txnid,
		bool, is_create
	),
	TP_FIELDS(
		ctf_integer_hex(uint64_t, txnid, txnid)
		ctf_integer(bool, is_create, is_create)
	)
)

TRACEPOINT_LOGLEVEL(
	txnfs,
	backup_open,
	TRACE_INFO
)

/** @brief Trace (before) backup WRITE operation
 * 
 * @param[in] txnid	Transaction ID
 * @param[in] offset	WRITE Operation offset
 * @param[in] size	WRITE data size
 */
TRACEPOINT_EVENT(
	txnfs,
	backup_write,
	TP_ARGS(
		uint64_t, txnid,
		size_t, offset,
		size_t, size
	),
	TP_FIELDS(
		ctf_integer_hex(uint64_t, txnid, txnid)
		ctf_integer(size_t, offset, offset)
		ctf_integer(size_t, size, size)
	)
)

TRACEPOINT_LOGLEVEL(
	txnfs,
	backup_write,
	TRACE_INFO
)

/**
 * @brief Trace (before) backup REMOVE operation
 * 
 * @param[in] txnid	Transaction ID
 */
TRACEPOINT_EVENT(
	txnfs,
	backup_remove,
	TP_ARGS(
		uint64_t, txnid
	),
	TP_FIELDS(
		ctf_integer_hex(uint64_t, txnid, txnid)
	)
)

TRACEPOINT_LOGLEVEL(
	txnfs,
	backup_remove,
	TRACE_INFO
)

/**
 * @brief Trace finishing lookup
 * 
 * @param[in] status	Returned status.major code
 * @param[in] name	Name of the file looked up
 * @param[in] size	File size
 */
TRACEPOINT_EVENT(
	txnfs,
	done_lookup,
	TP_ARGS(
		int, status,
		char *, name,
		size_t, size
	),
	TP_FIELDS(
		ctf_integer(int, status, status)
		ctf_string(name, name)
		ctf_integer(size_t, size, size)
	)
)

TRACEPOINT_LOGLEVEL(
	txnfs,
	done_lookup,
	TRACE_INFO
)

/**
 * @brief Trace finishing backing up the file
 * 
 * @param[in] opidx	Operation index
 * @param[in] filetype	File type
 * @param[in] typename	Name of the file type
 * @param[in] filesize	Size of the file
 */
TRACEPOINT_EVENT(
	txnfs,
	done_backup_file,
	TP_ARGS(
		int, opidx,
		int, filetype,
		const char *, typename,
		size_t, filesize
	),
	TP_FIELDS(
		ctf_integer(int, opidx, opidx)
		ctf_integer(int, filetype, filetype)
		ctf_string(typename, typename)
		ctf_integer(size_t, filesize, filesize)
	)
)

TRACEPOINT_LOGLEVEL(
	txnfs,
	done_backup_file,
	TRACE_INFO
)

/**
 * @brief Trace before start_compound is called
 * 
 * @param[in] opcnt	Number of operations in this compound
 */
TRACEPOINT_EVENT(
	txnfs,
	before_start_compound,
	TP_ARGS(int, opcnt),
	TP_FIELDS(
		ctf_integer(int, opcnt, opcnt)
	)
)

TRACEPOINT_LOGLEVEL(
	txnfs,
	before_start_compound,
	TRACE_INFO
)

/**
 * @brief Traced after entering txnfs_start_compound and variables
 * are initialized
 * 
 * @param[in] opcnt	Number of operations in this compound
 */
TRACEPOINT_EVENT(
	txnfs,
	init_start_compound,
	TP_ARGS(int, opcnt),
	TP_FIELDS(
		ctf_integer(int, opcnt, opcnt)
	)
)

TRACEPOINT_LOGLEVEL(
	txnfs,
	init_start_compound,
	TRACE_INFO
)

/**
 * @brief Trace creation of txn log
 * 
 * @param[in] txnid	Transaction ID
 */
TRACEPOINT_EVENT(
	txnfs,
	create_txn_log,
	TP_ARGS(uint64_t, txnid),
	TP_FIELDS(
		ctf_integer_hex(uint64_t, txnid, txnid)
	)
)

TRACEPOINT_LOGLEVEL(
	txnfs,
	create_txn_log,
	TRACE_INFO
)

/**
 * @brief Trace initialization of txn cache
 * 
 * @param[in] txnid	Transaction ID
 */
TRACEPOINT_EVENT(
	txnfs,
	init_txn_cache,
	TP_ARGS(uint64_t, txnid),
	TP_FIELDS(
		ctf_integer_hex(uint64_t, txnid, txnid)
	)
)

TRACEPOINT_LOGLEVEL(
	txnfs,
	init_txn_cache,
	TRACE_INFO
)

/**
 * @brief Trace calling sub-FSAL's start_compound
 * 
 * @param[in] txnid	Transaction ID
 * @param[in] subfsal	Name of the sub-FSAL
 */
TRACEPOINT_EVENT(
	txnfs,
	called_subfsal_start_compound,
	TP_ARGS(
		uint64_t, txnid,
		char *, subfsal
	),
	TP_FIELDS(
		ctf_integer_hex(uint64_t, txnid, txnid)
		ctf_string(subfsal, subfsal)
	)
)

TRACEPOINT_LOGLEVEL(
	txnfs,
	called_subfsal_start_compound,
	TRACE_INFO
)

/**
 * @brief Trace after start_compound is called
 * 
 * @param[in] opcnt	Number of operations in this compound
 * @param[in] txnid	Transaction ID
 */
TRACEPOINT_EVENT(
	txnfs,
	after_start_compound,
	TP_ARGS(
		int, opcnt,
		uint64_t, txnid
	),
	TP_FIELDS(
		ctf_integer(int, opcnt, opcnt)
		ctf_integer_hex(uint64_t, txnid, txnid)
	)
)

TRACEPOINT_LOGLEVEL(
	txnfs,
	after_start_compound,
	TRACE_INFO
)

/**
 * @brief Trace before end_compound is called
 * 
 * @param[in] txnid	Transaction ID
 */
TRACEPOINT_EVENT(
	txnfs,
	before_end_compound,
	TP_ARGS(uint64_t, txnid),
	TP_FIELDS(
		ctf_integer_hex(uint64_t, txnid, txnid)
	)
)

TRACEPOINT_LOGLEVEL(
	txnfs,
	before_end_compound,
	TRACE_INFO
)

/**
 * @brief Trace calling sub-FSAL's end_compound
 * 
 * @param[in] txnid	Transaction ID
 * @param[in] subfsal	sub-FSAL's name
 */
TRACEPOINT_EVENT(
	txnfs,
	called_subfsal_end_compound,
	TP_ARGS(
		uint64_t, txnid,
		char *, subfsal
	),
	TP_FIELDS(
		ctf_integer_hex(uint64_t, txnid, txnid)
		ctf_string(subfsal, subfsal)
	)
)

TRACEPOINT_LOGLEVEL(
	txnfs,
	called_subfsal_end_compound,
	TRACE_INFO
)

/**
 * @brief Trace entering TXN's end_compound payload
 * 
 * (after init vars, call sub-fsal methods and logging info)
 * 
 * @param[in] res	NFS compound result error code
 * @param[in] txnid	Transaction ID
 */
TRACEPOINT_EVENT(
	txnfs,
	init_end_compound,
	TP_ARGS(
		int, res,
		uint64_t, txnid
	),
	TP_FIELDS(
		ctf_integer(int, res, res)
		ctf_integer_hex(uint64_t, txnid, txnid)
	)
)

TRACEPOINT_LOGLEVEL(
	txnfs,
	init_end_compound,
	TRACE_INFO
)

/**
 * @brief Trace committing TXN cache into leveldb
 * 
 * @param[in] txnid	Transaction ID
 */
TRACEPOINT_EVENT(
	txnfs,
	committed_txn_cache,
	TP_ARGS(uint64_t, txnid),
	TP_FIELDS(
		ctf_integer_hex(uint64_t, txnid, txnid)
	)
)

TRACEPOINT_LOGLEVEL(
	txnfs,
	committed_txn_cache,
	TRACE_INFO
)

/**
 * @brief Trace collecting cache items to writebatch
 * 
 * @param[in] txnid	Transaction ID
 * @param[in] nput	Number of put operations
 * @param[in] ndel	Number of delete operations
 */
TRACEPOINT_EVENT(
	txnfs,
	collected_cache_entries,
	TP_ARGS(
		uint64_t, txnid,
		int, nput,
		int, ndel
	),
	TP_FIELDS(
		ctf_integer_hex(uint64_t, txnid, txnid)
		ctf_integer(int, nput, nput)
		ctf_integer(int, ndel, ndel)
	)
)

TRACEPOINT_LOGLEVEL(
	txnfs,
	collected_cache_entries,
	TRACE_INFO
)

/**
 * @brief Trace writing into the leveldb
 * 
 * @param[in] txnid	Transaction ID
 * @param[in] haserr	Has db error?
 */
TRACEPOINT_EVENT(
	txnfs,
	committed_cache_to_db,
	TP_ARGS(
		uint64_t, txnid,
		bool, haserr
	),
	TP_FIELDS(
		ctf_integer_hex(uint64_t, txnid, txnid)
		ctf_integer(bool, haserr, haserr)
	)
)

TRACEPOINT_LOGLEVEL(
	txnfs,
	committed_cache_to_db,
	TRACE_INFO
)

/**
 * @brief Trace restoring the compound
 * 
 * @param[in] txnid	Transaction ID
 * @param[in] ret	Return code of txnfs_compound_restore
 */
TRACEPOINT_EVENT(
	txnfs,
	restored_compound,
	TP_ARGS(
		uint64_t, txnid,
		int, ret
	),
	TP_FIELDS(
		ctf_integer_hex(uint64_t, txnid, txnid)
		ctf_integer(int, ret, ret)
	)
)

TRACEPOINT_LOGLEVEL(
	txnfs,
	restored_compound,
	TRACE_INFO
)

/**
 * @brief Trace cleaning up cache
 * 
 * @param[in] txnid	Transaction ID
 */
TRACEPOINT_EVENT(
	txnfs,
	cleaned_up_cache,
	TP_ARGS(uint64_t, txnid),
	TP_FIELDS(
		ctf_integer_hex(uint64_t, txnid, txnid)
	)
)

TRACEPOINT_LOGLEVEL(
	txnfs,
	cleaned_up_cache,
	TRACE_INFO
)

/**
 * @brief Trace cleaning up backup
 * 
 * @param[in] txnid	Transaction ID
 */
TRACEPOINT_EVENT(
	txnfs,
	cleaned_up_backup,
	TP_ARGS(uint64_t, txnid),
	TP_FIELDS(
		ctf_integer_hex(uint64_t, txnid, txnid)
	)
)

TRACEPOINT_LOGLEVEL(
	txnfs,
	cleaned_up_backup,
	TRACE_INFO
)

/**
 * @brief Trace after end_compound is called
 * 
 * @param[in] res	Result error code
 * @param[in] txnid	Transaction ID
 */
TRACEPOINT_EVENT(
	txnfs,
	after_end_compound,
	TP_ARGS(
		int, res,
		uint64_t, txnid
	),
	TP_FIELDS(
		ctf_integer(int, res, res)
		ctf_integer_hex(uint64_t, txnid, txnid)
	)
)

TRACEPOINT_LOGLEVEL(
	txnfs,
	after_end_compound,
	TRACE_INFO
)

/**
 * @brief Trace after sub-FSAL operation is finished
 * 
 * @param[in] ret	Major status code of subfsal call
 * @param[in] opidx	Operation index
 * @param[in] txnid	Transaction ID
 * @param[in] opname	Name of the operation
 */
TRACEPOINT_EVENT(
	txnfs,
	subfsal_op_done,
	TP_ARGS(
		int, ret,
		int, opidx,
		uint64_t, txnid,
		const char *, opname
	),
	TP_FIELDS(
		ctf_integer(int, ret, ret)
		ctf_integer(int, opidx, opidx)
		ctf_integer_hex(uint64_t, txnid, txnid)
		ctf_string(opname, opname)
	)
)

TRACEPOINT_LOGLEVEL(
	txnfs,
	subfsal_op_done,
	TRACE_INFO
)

/**
 * @brief Trace when subfsal handle_to_key is done
 * 
 * @param[in] fileid	The file ID of the target obj hdl
 */
TRACEPOINT_EVENT(
	txnfs,
	sub_handle_to_key,
	TP_ARGS(
		uint64_t, fileid
	),
	TP_FIELDS(
		ctf_integer(uint64_t, fileid, fileid)
	)
)

TRACEPOINT_LOGLEVEL(
	txnfs,
	sub_handle_to_key,
	TRACE_INFO
)

/**
 * @brief Trace after trying to get UUID from txn cache
 * 
 * @param[in] addr	The address where the sub-fsal file handle is located
 */
TRACEPOINT_EVENT(
	txnfs,
	get_uuid_in_cache,
	TP_ARGS(
		void *, addr
	),
	TP_FIELDS(
		ctf_integer_hex(void *, addr, addr)
	)
)

TRACEPOINT_LOGLEVEL(
	txnfs,
	get_uuid_in_cache,
	TRACE_INFO
)

/**
 * @brief Trace after trying to get UUID from leveldb
 * 
 * @param[in] addr	The address where the sub-fsal file handle is located
 */
TRACEPOINT_EVENT(
	txnfs,
	get_uuid_in_db,
	TP_ARGS(
		void *, addr
	),
	TP_FIELDS(
		ctf_integer_hex(void *, addr, addr)
	)
)

TRACEPOINT_LOGLEVEL(
	txnfs,
	get_uuid_in_db,
	TRACE_INFO
)

/**
 * @brief Trace after inserting <uuid, fh> to txn-cache
 * 
 * @param[in] addr	The address where the sub-fsal file handle is located
 * @param[in] type	Entry type
 */
TRACEPOINT_EVENT(
	txnfs,
	cache_insert_handle,
	TP_ARGS(
		void *, addr,
		int, type
	),
	TP_FIELDS(
		ctf_integer_hex(void *, addr, addr)
		ctf_integer(int, type, type)
	)
)

TRACEPOINT_LOGLEVEL(
	txnfs,
	cache_insert_handle,
	TRACE_INFO
)

/**
 * @brief Trace after allocation of a new TXNFS obj handle
 * 
 * @param[in] addr	The address of the new obj handle
 * @param[in] fileid	File number (obtainable by ls -i)
 * @param[in] type	Fle type
 */
TRACEPOINT_EVENT(
	txnfs,
	alloc_handle,
	TP_ARGS(
		void *, addr,
		uint64_t, fileid,
		int, type
	),
	TP_FIELDS(
		ctf_integer_hex(void *, addr, addr)
		ctf_integer(uint64_t, fileid, fileid)
		ctf_integer(int, type, type)
	)
)

TRACEPOINT_LOGLEVEL(
	txnfs,
	alloc_handle,
	TRACE_INFO
)

/**
 * @brief Trace when txnfs_delete_uuid is done
 * 
 * @param[in] fileid	The file number
 */
TRACEPOINT_EVENT(
	txnfs,
	delete_uuid,
	TP_ARGS(
		uint64_t, fileid
	),
	TP_FIELDS(
		ctf_integer(uint64_t, fileid, fileid)
	)
)

TRACEPOINT_LOGLEVEL(
	txnfs,
	delete_uuid,
	TRACE_INFO
)

/**
 * @brief Trace after finding relevant paths to lock
 * 
 * @param[in] txnid   Transaction ID
 * @param[in] npaths  Number of paths involved
 * @param[in] nops    Number of compound operations
 */
TRACEPOINT_EVENT(
	txnfs,
	find_relevant_paths,
	TP_ARGS(
		uint64_t, txnid,
    int, npaths,
    int, nops
	),
	TP_FIELDS(
		ctf_integer(uint64_t, txnid, txnid)
    ctf_integer(int, npaths, npaths)
    ctf_integer(int, nops, nops)
	)
)

TRACEPOINT_LOGLEVEL(
	txnfs,
	find_relevant_paths,
	TRACE_INFO
)

/**
 * @brief Trace after locking paths
 * 
 * @param[in] txnid   Transaction ID
 */
TRACEPOINT_EVENT(
	txnfs,
	locked_paths,
	TP_ARGS(
		uint64_t, txnid 
	),
	TP_FIELDS(
		ctf_integer(uint64_t, txnid, txnid)
	)
)

TRACEPOINT_LOGLEVEL(
	txnfs,
	locked_paths,
	TRACE_INFO
)

/**
 * @brief Trace after retrieving abs. path in create_handle
 * 
 * @param[in] fileid   File ID
 * @param[in] path     The abs. path
 */
TRACEPOINT_EVENT(
	txnfs,
	get_abs_path,
	TP_ARGS(
		uint64_t, fileid,
    const char *, path
	),
	TP_FIELDS(
		ctf_integer(uint64_t, fileid, fileid)
    ctf_string(path, path)
	)
)

TRACEPOINT_LOGLEVEL(
	txnfs,
	get_abs_path,
	TRACE_INFO
)

#endif /* GANESHA_LTTNG_TXNFS_TP_H */

#undef TRACEPOINT_INCLUDE
#define TRACEPOINT_INCLUDE "gsh_lttng/txnfs.h"

#include <lttng/tracepoint-event.h>
