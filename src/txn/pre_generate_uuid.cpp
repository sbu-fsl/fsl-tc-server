#include "pre_generate_uuid.h"

int pre_generate_open_flag_check(OPEN4args* const arg_OPEN4) {
  open_claim_type4 claim = arg_OPEN4->claim.claim;
  if (claim == CLAIM_NULL &&
      ((arg_OPEN4->openhow.opentype == OPEN4_CREATE) &&
       ((arg_OPEN4->openhow.openflag4_u.how.mode == UNCHECKED4) ||
        (arg_OPEN4->openhow.openflag4_u.how.mode == GUARDED4))))
    return 1;
  else
    return 0;
}
