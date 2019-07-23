
#undef TRACEPOINT_PROVIDER
#define TRACEPOINT_PROVIDER txnfs

#if !defined(GANESHA_LTTNG_TXNFS_TP_H) || \
	defined(TRACEPOINT_HEADER_MULTI_READ)
#define GANESHA_LTTNG_TXNFS_TP_H

#include <lttng/tracepoint.h>
#include <stdint.h>

/**
 * @brief Trace the exit of backup_nfs4_op function
 * 
 * The total time spent on backing up can be derived when
 * the trace point `nfs_rpc:v4op_start` is also enabled
 * 
 * @param[in] opidx	The index of the operation in compound
 * @param[in] opcode	NFS4 operation number
 * @param[in] opname	Text name of the operation
 */
TRACEPOINT_EVENT(
	txnfs,
	end_backup,
	TP_ARGS(int, opidx,
		int, opcode,
		const char *, opname),
	TP_FIELDS(
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
