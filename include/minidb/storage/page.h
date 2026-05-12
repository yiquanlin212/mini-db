#pragma once

#include <cstdint>
#include <cstring>

namespace minidb {

// 4KB page size — matches OS page size on macOS / Linux
constexpr size_t PAGE_SIZE = 4096;

// Page ID type. -1 means "not a valid page"
using page_id_t = int32_t;
constexpr page_id_t INVALID_PAGE_ID = -1;

class Page {
public:
    Page() : page_id_(INVALID_PAGE_ID), is_dirty_(false) {
        std::memset(data_, 0, PAGE_SIZE);
    }

    page_id_t GetPageId() const { return page_id_; }
    void SetPageId(page_id_t id) { page_id_ = id; }

    char* GetData() { return data_; }
    const char* GetData() const { return data_; }

    bool IsDirty() const { return is_dirty_; }
    void SetDirty(bool dirty) { is_dirty_ = dirty; }

private:
    page_id_t page_id_;
    bool is_dirty_;
    char data_[PAGE_SIZE];
};

}  // namespace minidb
