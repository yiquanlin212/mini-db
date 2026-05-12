#include "minidb/recovery/recovery.h"

#include <fstream>
#include <unordered_set>

#include "minidb/catalog/schema.h"
#include "minidb/recovery/log_record.h"

namespace minidb {

RecoveryStats Recovery::Run(const std::string& log_file,
                            Catalog* catalog,
                            TransactionManager* txn_mgr) {
    RecoveryStats stats;
    std::unordered_set<txn_id_t> committed;
    std::unordered_set<txn_id_t> seen;
    txn_id_t max_txn = 0;

    {
        std::ifstream in(log_file, std::ios::binary);
        if (!in.is_open()) return stats;
        LogRecord r;
        while (DeserializeLogRecord(in, &r)) {
            stats.records_scanned++;
            if (r.txn_id > max_txn) max_txn = r.txn_id;
            seen.insert(r.txn_id);
            if (r.type == LogRecordType::COMMIT) {
                committed.insert(r.txn_id);
            }
        }
    }
    stats.txns_committed   = static_cast<int64_t>(committed.size());
    stats.txns_uncommitted = static_cast<int64_t>(seen.size()) - stats.txns_committed;

    {
        std::ifstream in(log_file, std::ios::binary);
        if (!in.is_open()) return stats;
        LogRecord r;
        while (DeserializeLogRecord(in, &r)) {
            if (r.type != LogRecordType::INSERT) continue;
            if (committed.count(r.txn_id) == 0) {
                stats.inserts_skipped++;
                continue;
            }
            TableInfo* tbl = catalog->GetTable(r.table_name);
            if (!tbl) {
                stats.inserts_skipped++;
                continue;
            }
            Tuple t(r.tuple_data.data(), r.tuple_data.size());
            std::vector<Value> row = TupleCodec::Decode(tbl->schema, t);
            RID dummy;
            if (catalog->InsertRow(r.table_name, row, &dummy)) {
                stats.inserts_redone++;
            } else {
                stats.inserts_skipped++;
            }
        }
    }

    txn_mgr->SetNextTxnId(max_txn + 1);
    return stats;
}

}  // namespace minidb
