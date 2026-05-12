#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

#include "minidb/storage/tuple.h"

namespace minidb {

using lsn_t    = int64_t;
using txn_id_t = int64_t;
constexpr lsn_t    INVALID_LSN    = -1;
constexpr txn_id_t INVALID_TXN_ID = -1;

enum class LogRecordType : int32_t {
    BEGIN  = 1,
    INSERT = 2,
    COMMIT = 3,
    ABORT  = 4,
};

struct LogRecord {
    lsn_t             lsn        = INVALID_LSN;
    txn_id_t          txn_id     = INVALID_TXN_ID;
    LogRecordType     type       = LogRecordType::BEGIN;
    std::string       table_name;
    RID               rid        = {INVALID_PAGE_ID, 0};
    std::vector<char> tuple_data;
};

void SerializeLogRecord(std::vector<char>& buf, const LogRecord& rec);
bool DeserializeLogRecord(std::istream& in, LogRecord* out);

}  // namespace minidb
