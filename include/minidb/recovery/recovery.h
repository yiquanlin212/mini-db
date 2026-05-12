#pragma once

#include <string>

#include "minidb/catalog/catalog.h"
#include "minidb/recovery/transaction.h"

namespace minidb {

struct RecoveryStats {
    int64_t records_scanned  = 0;
    int64_t txns_committed   = 0;
    int64_t txns_uncommitted = 0;
    int64_t inserts_redone   = 0;
    int64_t inserts_skipped  = 0;
};

class Recovery {
public:
    static RecoveryStats Run(const std::string& log_file,
                             Catalog* catalog,
                             TransactionManager* txn_mgr);
};

}  // namespace minidb
