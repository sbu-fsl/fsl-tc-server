
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
 * @param[in] opcode	Operation code
 * @param[in] opname	Text name of the operation
 */
TRACEPOINT_EVENT(
	txnfs,
	done_backup_file,
	TP_ARGS(
		int, opidx,
		int, opcode,
		char *, opname
	),
	TP_FIELDS(
		ctf_integer(int, opidx, opidx)
		ctf_integer(int, opcode, opcode)
		ctf_string(opname, opname)
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
 * @brief Trace creation of txn_context
 * 
 * @param[in] opcnt	Number of operations in this compound
 * @param[in] context	Address of created txn_context
 */
TRACEPOINT_EVENT(
	txnfs,
	create_txn_context,
	TP_ARGS(
		int, opcnt,
		void *, txn_context
	),
	TP_FIELDS(
		ctf_integer(int, opcnt, opcnt)
		ctf_integer_hex(void *, txn_context, txn_context)
	)
)

TRACEPOINT_LOGLEVEL(
	txnfs,
	create_txn_context,
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

#endif /* GANESHA_LTTNG_TXNFS_TP_H */

#undef TRACEPOINT_INCLUDE
#define TRACEPOINT_INCLUDE "gsh_lttng/txnfs.h"

#include <lttng/tracepoint-event.h>
