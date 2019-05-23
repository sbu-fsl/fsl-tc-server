#ifndef _TXN_LOGGER_INTERNAL_H
#define _TXN_LOGGER_INTERNAL_H

#include "txn_logger.h"

namespace txnfs {
namespace proto {
class TransactionLog;
}
} // namespace txnfs

using namespace txnfs;

int txn_log_from_pb(proto::TransactionLog *txnpb, struct TxnLog *txn_log);
int txn_log_to_pb(struct TxnLog *txn_log, proto::TransactionLog *txnpb);
void txn_log_free(struct TxnLog *txn_log);

#endif
