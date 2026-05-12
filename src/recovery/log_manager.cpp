#include "minidb/recovery/log_manager.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace minidb {

LogManager::LogManager(const std::string& log_file) : file_name_(log_file) {
    fd_ = ::open(log_file.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd_ < 0) {
        throw std::runtime_error("LogManager: cannot open " + log_file);
    }
}

LogManager::~LogManager() {
    if (fd_ >= 0) {
        ::fsync(fd_);
        ::close(fd_);
        fd_ = -1;
    }
}

lsn_t LogManager::AppendRecord(LogRecord& rec) {
    rec.lsn = next_lsn_++;
    std::vector<char> buf;
    buf.reserve(64 + rec.tuple_data.size() + rec.table_name.size());
    SerializeLogRecord(buf, rec);

    const char* p = buf.data();
    size_t      n = buf.size();
    while (n > 0) {
        ssize_t w = ::write(fd_, p, n);
        if (w < 0) {
            if (errno == EINTR) continue;
            throw std::runtime_error("LogManager: write failed");
        }
        p += w;
        n -= static_cast<size_t>(w);
    }
    return rec.lsn;
}

lsn_t LogManager::AppendBegin(txn_id_t txn) {
    LogRecord r;
    r.txn_id = txn;
    r.type   = LogRecordType::BEGIN;
    return AppendRecord(r);
}

lsn_t LogManager::AppendCommit(txn_id_t txn) {
    LogRecord r;
    r.txn_id = txn;
    r.type   = LogRecordType::COMMIT;
    return AppendRecord(r);
}

lsn_t LogManager::AppendAbort(txn_id_t txn) {
    LogRecord r;
    r.txn_id = txn;
    r.type   = LogRecordType::ABORT;
    return AppendRecord(r);
}

lsn_t LogManager::AppendInsert(txn_id_t txn,
                                const std::string& table_name,
                                const RID& rid,
                                const char* tuple_data,
                                size_t tuple_size) {
    LogRecord r;
    r.txn_id     = txn;
    r.type       = LogRecordType::INSERT;
    r.table_name = table_name;
    r.rid        = rid;
    r.tuple_data.assign(tuple_data, tuple_data + tuple_size);
    return AppendRecord(r);
}

void LogManager::Flush() {
    if (::fsync(fd_) < 0) {
        throw std::runtime_error("LogManager: fsync failed");
    }
    flushed_lsn_ = next_lsn_ - 1;
}

}  // namespace minidb
