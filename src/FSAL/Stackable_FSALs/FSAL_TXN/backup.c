#include "txnfs_methods.h"
#include <assert.h>

int txnfs_compound_backup(uint64_t txnid, COMPOUND4args* args) {
  UDBG;	
  // assert backup dir exists
  // create directory with uuid as name in backup dir
  // for each open / write / unlink / rename compound operation take backup
	return 0;
}

int txnfs_compound_restore(uint64_t txnid, COMPOUND4res* res) {
  UDBG;	
  // assert backup dir exists
  // assert txn backup dir exists
  //
	return 0;
}
