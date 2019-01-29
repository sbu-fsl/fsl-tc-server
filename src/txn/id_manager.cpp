#include <iostream>
#include <iomanip>
#include <sstream>
#include <string.h>
#include <mutex>
#include <stdio.h>

#include <absl/numeric/int128.h>

#include "util/slice.h"
#include "id_manager.h"
#include "lwrapper.h"

#define NEXT_FILE_ID_KEY ("__NEXT_FILE_ID__")
#define NEXT_FILE_ID_KEY_LEN (sizeof(NEXT_FILE_ID_KEY) - 1)
#define KEY_RESERVE_AMOUNT 256

// File ids starts with 0x100000000ULL, which is the root field id. This also
// means that ids smaller than 0xFFFFFFFF are reserved.
constexpr absl::uint128 root_file_id(0x100000000ULL);
// We store a next_file_id and max_reserved_id in memory to prevent constantly
// hitting the database.
absl::uint128 next_file_id(0x100000001ULL);
absl::uint128 max_reserved_id(0);
std::mutex increment_mutex;

absl::uint128 buf_to_uint128(const char *buf) {
  absl::uint128 n(0);

  memcpy((void *)&n, buf, TXN_UUID_LEN);

  return n;
}

uint64_t get_lower_half(const char* buf) {
  return absl::Uint128Low64(buf_to_uint128(buf));
}

uint64_t get_upper_half(const char *buf) {
  return absl::Uint128High64(buf_to_uint128(buf));
}

char *uint128_to_buf(absl::uint128 n) {

  char *s = (char *)malloc(TXN_UUID_LEN);
  if (s == nullptr) {
    return nullptr;
  }

  memcpy(s, (void *)&n, TXN_UUID_LEN);

  return s;
}

char *uint128_to_string(absl::uint128 n) {
  char *s = (char *)malloc(40);  // ciel(log10(2^128)) + 1
  if (s == nullptr) {
    return nullptr;
  }

  std::stringstream ss;
  ss << n;

  size_t len = ss.str().size();
  memcpy(s, ss.str().c_str(), len);
  s[len] = '\0';

  return s;
}

char *id_to_string(const char *buf) {
  return uint128_to_string(buf_to_uint128(buf));
}

int initialize_id_manager(const db_store_t *db) {
  struct db_kvpair lookup;

  lookup.key = NEXT_FILE_ID_KEY;
  lookup.key_len = NEXT_FILE_ID_KEY_LEN;

  int ret = get_keys(&lookup, 1, db);
  if (ret != 0) return ret;

  // If NEXT_FILE_ID_KEY is not found int the database, create an entry with
  // the next file id.
  if (lookup.val_len == 0) {
    lookup.val = (void *)uint128_to_buf(next_file_id);
    lookup.val_len = TXN_UUID_LEN;
    ret = put_keys(&lookup, 1, db);
    if (ret != 0) return ret;
  }

  next_file_id = buf_to_uint128((char *)lookup.val);
  max_reserved_id = next_file_id + KEY_RESERVE_AMOUNT;
  free(lookup.val);
  lookup.val = uint128_to_buf(max_reserved_id);

  ret = put_keys(&lookup, 1, db);
  free(lookup.val);
  if (ret != 0) return ret;

  return 0;
}

char *generate_file_id(const db_store_t *db) {
  std::lock_guard<std::mutex> lock(increment_mutex);
  // Make sure in-memory next_file_id has been initialized
  assert(next_file_id != 0);

  if (next_file_id == max_reserved_id) {
    struct db_kvpair lookup;
    lookup.key = NEXT_FILE_ID_KEY;
    lookup.key_len = NEXT_FILE_ID_KEY_LEN;

    max_reserved_id = next_file_id + KEY_RESERVE_AMOUNT;
    lookup.val = uint128_to_buf(max_reserved_id);
    lookup.val_len = NEXT_FILE_ID_KEY_LEN;

    int ret = put_keys(&lookup, 1, db);
    free(lookup.val);
    if (ret != 0) return nullptr;
  }

  return uint128_to_buf(next_file_id++);
}

char *get_root_id(const db_store_t *db) { return uint128_to_buf(root_file_id); }
