#pragma once

#include "minidb/storage/buffer_pool_manager.h"
#include "minidb/storage/page.h"
#include "minidb/storage/table_page.h"
#include "minidb/storage/tuple.h"

namespace minidb {

class TableIterator;

class TableHeap {
public:
    explicit TableHeap(BufferPoolManager* bpm);

    bool InsertTuple(const Tuple& tuple, RID* rid);
    bool GetTuple(const RID& rid, Tuple* out_tuple);

    page_id_t GetFirstPageId() const { return first_page_id_; }

    TableIterator begin();
    TableIterator end();

private:
    friend class TableIterator;

    BufferPoolManager* bpm_;
    page_id_t first_page_id_;
    page_id_t last_page_id_;
};

class TableIterator {
public:
    TableIterator(TableHeap* heap, RID rid);

    const Tuple& operator*() const { return current_tuple_; }
    const RID&   GetRID()    const { return rid_; }
    TableIterator& operator++();

    bool operator==(const TableIterator& other) const {
        return rid_.page_id == other.rid_.page_id && rid_.slot_id == other.rid_.slot_id;
    }
    bool operator!=(const TableIterator& other) const { return !(*this == other); }

private:
    TableHeap* heap_;
    RID rid_;
    Tuple current_tuple_;

    void Load();
};

}  // namespace minidb
