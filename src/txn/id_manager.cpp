#include <algorithm>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdio.h>
#include <string.h>

#include <absl/base/casts.h>
#include <absl/numeric/int128.h>

#include "id_manager.h"
#include "lwrapper.h"
#include "util/slice.h"

#define NEXT_FILE_ID_KEY ("__NEXT_FILE_ID__")
#define NEXT_FILE_ID_KEY_LEN (sizeof(NEXT_FILE_ID_KEY) - 1)

namespace {

constexpr int kUUIDReserveBatchSize = 4096;

// File ids starts with a high component of 1, which is the root field id. This
// also means that ids with high component of 0 are reserved.
constexpr absl::uint128 root_file_id = absl::MakeUint128(/*high=*/1, 0);

constexpr absl::uint128 null_file_id = absl::MakeUint128(/*high=*/0, 0);

// This is an abstract id that represents the id of the current file handle.
constexpr absl::uint128 current_file_id = absl::MakeUint128(/*high=*/0, 1);

// This is an abstract id that represents the id of the saved file handle.
constexpr absl::uint128 saved_file_id = absl::MakeUint128(/*high=*/0, 2);

// We store a next_file_id and max_reserved_id in memory to prevent constantly
// hitting the database.
absl::uint128 next_file_id = absl::MakeUint128(1, 1);

// The max reserved uuid (exclusive).
absl::uint128 max_reserved_id(0);

std::mutex uuid_mutex;

char *uint128_to_buf(absl::uint128 n) {
  char *s = (char *)malloc(TXN_UUID_LEN);
  if (s == nullptr) {
    return nullptr;
  }

  memcpy(s, (void *)&n, TXN_UUID_LEN);

  return s;
}

int uuid_batch_reserve(const db_store_t *db) {
  struct db_kvpair lookup;
  lookup.key = NEXT_FILE_ID_KEY;
  lookup.key_len = NEXT_FILE_ID_KEY_LEN;

  max_reserved_id += kUUIDReserveBatchSize;
  char* buf = uint128_to_buf(max_reserved_id);
  lookup.val = buf;
  lookup.val_len = NEXT_FILE_ID_KEY_LEN;

  int ret = put_keys(&lookup, 1, db);
  free(buf);
  return ret;
}

}  // namespace

absl::uint128 buf_to_uint128(const char *buf) {
  absl::uint128 n(0);

  memcpy((void *)&n, buf, TXN_UUID_LEN);

  return n;
}

uint64_t get_lower_half(const char *buf) {
  return absl::Uint128Low64(buf_to_uint128(buf));
}

uint64_t get_upper_half(const char *buf) {
  return absl::Uint128High64(buf_to_uint128(buf));
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

  std::lock_guard<std::mutex> l(uuid_mutex);
  int ret = get_keys(&lookup, 1, db);
  if (ret != 0) return ret;

  if (lookup.val_len > 0) {
    next_file_id = buf_to_uint128((char *)lookup.val);
  }

  max_reserved_id = next_file_id;

  return uuid_batch_reserve(db);
}

char *generate_file_id(const db_store_t *db) {
  std::lock_guard<std::mutex> l(uuid_mutex);
  // Make sure in-memory next_file_id has been initialized
  assert(max_reserved_id != 0);

  if (next_file_id == max_reserved_id) {
    if (uuid_batch_reserve(db) != 0) return nullptr;
  }

  return uint128_to_buf(next_file_id++);
}

char *get_root_id(const db_store_t *db) { return uint128_to_buf(root_file_id); }

uuid_t uuid_root() {
  return absl::bit_cast<uuid_t>(root_file_id);
}

uuid_t uuid_null() {
  return absl::bit_cast<uuid_t>(null_file_id);
}

uuid_t uuid_next(uuid_t current) {
  absl::uint128 u128 = absl::bit_cast<absl::uint128>(current);
  u128 += 1;
  return absl::bit_cast<uuid_t>(u128);
}

uuid_t uuid_allocate(db_store_t *db, int n) {
  std::lock_guard<std::mutex> l(uuid_mutex);
  const absl::uint128 new_next = next_file_id + n;
  while (new_next >= max_reserved_id) {
    if (uuid_batch_reserve(db) != 0) std::abort();
  }
  return absl::bit_cast<uuid_t>(std::exchange(next_file_id, new_next));
}
