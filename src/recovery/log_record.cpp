#include "minidb/recovery/log_record.h"

#include <cstring>
#include <istream>

namespace minidb {

static inline void AppendBytes(std::vector<char>& buf, const void* p, size_t n) {
    const char* src = static_cast<const char*>(p);
    buf.insert(buf.end(), src, src + n);
}

void SerializeLogRecord(std::vector<char>& buf, const LogRecord& rec) {
    AppendBytes(buf, &rec.lsn, 8);
    AppendBytes(buf, &rec.txn_id, 8);
    int32_t t = static_cast<int32_t>(rec.type);
    AppendBytes(buf, &t, 4);

    int32_t name_len = static_cast<int32_t>(rec.table_name.size());
    AppendBytes(buf, &name_len, 4);
    if (name_len > 0) AppendBytes(buf, rec.table_name.data(), name_len);

    AppendBytes(buf, &rec.rid.page_id, 4);
    AppendBytes(buf, &rec.rid.slot_id, 4);

    int32_t tlen = static_cast<int32_t>(rec.tuple_data.size());
    AppendBytes(buf, &tlen, 4);
    if (tlen > 0) AppendBytes(buf, rec.tuple_data.data(), tlen);
}

bool DeserializeLogRecord(std::istream& in, LogRecord* out) {
    if (!in.read(reinterpret_cast<char*>(&out->lsn), 8)) return false;
    if (!in.read(reinterpret_cast<char*>(&out->txn_id), 8)) return false;
    int32_t t;
    if (!in.read(reinterpret_cast<char*>(&t), 4)) return false;
    out->type = static_cast<LogRecordType>(t);

    int32_t name_len;
    if (!in.read(reinterpret_cast<char*>(&name_len), 4)) return false;
    out->table_name.assign(name_len, '\0');
    if (name_len > 0 && !in.read(out->table_name.data(), name_len)) return false;

    if (!in.read(reinterpret_cast<char*>(&out->rid.page_id), 4)) return false;
    if (!in.read(reinterpret_cast<char*>(&out->rid.slot_id), 4)) return false;

    int32_t tlen;
    if (!in.read(reinterpret_cast<char*>(&tlen), 4)) return false;
    out->tuple_data.assign(tlen, '\0');
    if (tlen > 0 && !in.read(out->tuple_data.data(), tlen)) return false;

    return true;
}

}  // namespace minidb
