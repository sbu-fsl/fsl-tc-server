#ifndef _ID_MANAGER_H
#define _ID_MANAGER_H

#define TXN_UUID_LEN 16

#ifdef __cplusplus
extern "C" {
#endif
#include "lwrapper.h"

// Returns 0 upon success
int initialize_id_manager(const db_store_t* db);

// Returns an opaque 16 byte buffer containing the file ID
// This buffer may contain null bytes
char *generate_file_id(const db_store_t* db);

// Returns a file ID that represents the root of the file system.
char *get_root_id(const db_store_t* db);

// Returns the lower-64 bits of the id.
uint64_t get_lower_half(const char* id);
// Returns the lower-64 bits of the id.
uint64_t get_upper_half(const char* id);

#ifdef __cplusplus
}
#endif

// Returns a base10 null terminated ascii representation of
// the opaque file id returned by generate_file_id()
char *id_to_string(const char *buf);

#endif // _ID_MANAGER_H
