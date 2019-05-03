#include "txnfs_methods.h"
#include <assert.h>

int txnfs_db_insert_handle(struct gsh_buffdesc *hdl_desc, uuid_t uuid) {
	db_kvpair_t kvpair;
	uuid_generate(uuid);
	char uuid_str[UUID_STR_LEN];
  uuid_unparse_lower(uuid, uuid_str);
	LogDebug(COMPONENT_FSAL, "generate uuid=%s\n", uuid_str);
	
	kvpair.key = uuid; 
	kvpair.key_len = TXN_UUID_LEN;

	kvpair.val = (char *) hdl_desc->addr;
	kvpair.val_len = hdl_desc->len;
	
	return put_id_handle(&kvpair, 1, db);
}

int txnfs_db_get_uuid(struct gsh_buffdesc *hdl_desc, uuid_t uuid) {
	db_kvpair_t kvpair;

	LogDebug(COMPONENT_FSAL, "HandleAddr: %p HandleLen: %zu", hdl_desc->addr, hdl_desc->len);
	kvpair.key = (char *) hdl_desc->addr;
	kvpair.key_len = hdl_desc->len;
	kvpair.val = NULL;
	kvpair.val_len = TXN_UUID_LEN;
	int res = get_id_handle(&kvpair, 1, db, true);
	LogDebug(COMPONENT_FSAL, "get_id_handle = %d", res);

	if (kvpair.val_len == 0)
	{
		return -1;
	}
	
	uuid_copy(uuid, kvpair.val);
	return 0;
}

int txnfs_db_delete_uuid(uuid_t uuid) {
	db_kvpair_t kvpair;

	kvpair.key = uuid;
	kvpair.key_len = TXN_UUID_LEN;
	kvpair.val = NULL;
	assert(get_id_handle(&kvpair, 1, db, false) == 0);
	LogDebug(COMPONENT_FSAL, "get_keys = %p %zu", kvpair.val, kvpair.val_len);
	char uuid_str[UUID_STR_LEN];
  uuid_unparse_lower(uuid, uuid_str);
	LogDebug(COMPONENT_FSAL, "delete uuid=%s\n", uuid_str);
	

  // this handle might have been deleted already!
  // check open2 tests
	/*assert(kvpair.val_len != 0);*/
	if (kvpair.val_len == 0)
	{
		return -1;
 	}

	return delete_id_handle(&kvpair, 1, db, false);
}

int txnfs_db_delete_handle(struct gsh_buffdesc *hdl_desc) {
	db_kvpair_t kvpair;

	kvpair.key = (char *) hdl_desc->addr;
	kvpair.key_len = hdl_desc->len;
	kvpair.val = NULL;

	return delete_id_handle(&kvpair, 1, db, true);
}

bool txnfs_db_handle_exists(struct gsh_buffdesc *hdl_desc)
{
	db_kvpair_t kvpair;

	LogDebug(COMPONENT_FSAL, "HandleAddr: %p HandleLen: %zu", hdl_desc->addr, hdl_desc->len);
	kvpair.key = (char *) hdl_desc->addr;
	kvpair.key_len = hdl_desc->len;
	kvpair.val = NULL;
	int res = get_keys(&kvpair, 1, db);
	LogDebug(COMPONENT_FSAL, "get_keys = %d", res);

	return kvpair.val != NULL;
}
