#include "txnfs_methods.h"
/*#include "id_manager.h"*/
#include <assert.h>

int txnfs_db_insert_handle(struct gsh_buffdesc *hdl_desc, uuid_t uuid) {
	db_kvpair_t kvpair;
	uuid_generate(uuid);
	char uuid_str[37];
  uuid_unparse_lower(uuid, uuid_str);
	LogDebug(COMPONENT_FSAL, "generate uuid=%s\n", uuid_str);
	
	kvpair.key = (char*)uuid; 
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

	if (kvpair.val == NULL)
	{
		return -1;
	}
	
	uuid_copy(uuid, kvpair.val);
	return 0;
}

int txnfs_db_delete_uuid(uuid_t uuid) {
	db_kvpair_t kvpair;

	kvpair.key = (char*)uuid;
	kvpair.key_len = TXN_UUID_LEN;
	kvpair.val = NULL;
	assert(get_id_handle(&kvpair, 1, db, false) == 0);
	assert(kvpair.val != NULL);

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

/*uuid_t txnfs_get_uuid() 
{
	UDBG;
	uuid_t uuid;
	[>srand(time(NULL));<]
	[>uuid.lo = rand();
	uuid.hi = rand();	<]
	return uuid;
}*/
/*uuid_t txnfs_get_uuid() 
{
	assert(op_ctx);

	 [>first time the thread or request is initialized <]
	if (op_ctx->uuid_len == 0) 
	{
		op_ctx->uuid_len = UUID_ALLOC_LIMIT;
	}

	if (op_ctx->uuid_index >= op_ctx->uuid_len) {
		 [>get more uuids<]
		uuid_t tuuid = uuid_allocate(db, UUID_ALLOC_LIMIT);
		memcpy(op_ctx->uuid, uuid_to_buf(tuuid), TXN_UUID_LEN);
		assert(op_ctx->uuid_len == UUID_ALLOC_LIMIT);
		 [>reset index<]
		op_ctx->uuid_index = 0;
	}

	 [>TODO - improve!	<]
	memcpy(op_ctx->uuid, uuid_to_buf(uuid_next(buf_to_uuid(op_ctx->uuid))), TXN_UUID_LEN);
	op_ctx->uuid_index++;
	uuid_t uuid = buf_to_uuid(op_ctx->uuid);
	LogDebug(COMPONENT_FSAL, "generate uuid=%lu %lu\n", uuid.lo, uuid.hi);
	[>LogDebug(COMPONENT_FSAL, "generate uuid=%s\n", uuid_to_string(op_ctx->uuid));<]
	return uuid;
}*/
