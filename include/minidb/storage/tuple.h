#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "minidb/storage/page.h"

namespace minidb {

// Record ID: a tuple is uniquely identified by (page_id, slot_id)
struct RID {
    page_id_t page_id;
    int32_t slot_id;
};

// A tuple is an opaque byte buffer for now.
// Later we'll add a Schema to interpret the bytes as typed columns.
class Tuple {
public:
    Tuple() = default;
    Tuple(const char* data, size_t size) : data_(data, data + size) {}
    explicit Tuple(const std::string& s) : data_(s.begin(), s.end()) {}

    const char* GetData() const { return data_.data(); }
    size_t GetSize() const { return data_.size(); }

    void SetData(const char* src, size_t size) {
        data_.assign(src, src + size);
    }

    std::string AsString() const {
        return std::string(data_.begin(), data_.end());
    }

private:
    std::vector<char> data_;
};

}  // namespace minidb
