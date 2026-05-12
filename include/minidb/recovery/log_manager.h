#pragma once

#include <string>

#include "minidb/recovery/log_record.h"
#include "minidb/storage/tuple.h"

namespace minidb {

class LogManager {
public:
    explicit LogManager(const std::string& log_file);
    ~LogManager();

    LogManager(const LogManager&) = delete;
    LogManager& operator=(const LogManager&) = delete;

    lsn_t AppendBegin(txn_id_t txn);
    lsn_t AppendCommit(txn_id_t txn);
    lsn_t AppendAbort(txn_id_t txn);
    lsn_t AppendInsert(txn_id_t txn,
                       const std::string& table_name,
                       const RID& rid,
                       const char* tuple_data,
                       size_t tuple_size);

    void Flush();

    lsn_t GetNextLSN()    const { return next_lsn_; }
    lsn_t GetFlushedLSN() const { return flushed_lsn_; }
    const std::string& GetFileName() const { return file_name_; }

private:
    lsn_t AppendRecord(LogRecord& rec);

    std::string file_name_;
    int         fd_         = -1;
    lsn_t       next_lsn_   = 0;
    lsn_t       flushed_lsn_ = INVALID_LSN;
};

}  // namespace minidb
