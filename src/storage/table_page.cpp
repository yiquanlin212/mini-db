#include "minidb/storage/table_page.h"

#include <cstring>

namespace minidb {

// Header field accessors
static inline int32_t& NumTuples(char* page)            { return *reinterpret_cast<int32_t*>(page); }
static inline int32_t NumTuplesRead(const char* page)   { return *reinterpret_cast<const int32_t*>(page); }
static inline int32_t& FreeOffset(char* page)           { return *reinterpret_cast<int32_t*>(page + 4); }
static inline int32_t FreeOffsetRead(const char* page)  { return *reinterpret_cast<const int32_t*>(page + 4); }
static inline page_id_t& NextPid(char* page)            { return *reinterpret_cast<page_id_t*>(page + 8); }
static inline page_id_t NextPidRead(const char* page)   { return *reinterpret_cast<const page_id_t*>(page + 8); }

// Slot accessors
static inline char* SlotPtr(char* page, int32_t slot_id) {
    return page + PAGE_HEADER_SIZE + slot_id * SLOT_SIZE;
}
static inline const char* SlotPtrRead(const char* page, int32_t slot_id) {
    return page + PAGE_HEADER_SIZE + slot_id * SLOT_SIZE;
}
static inline int32_t& SlotOffset(char* page, int32_t slot_id)  { return *reinterpret_cast<int32_t*>(SlotPtr(page, slot_id)); }
static inline int32_t SlotOffsetRead(const char* page, int32_t slot_id) { return *reinterpret_cast<const int32_t*>(SlotPtrRead(page, slot_id)); }
static inline int32_t& SlotLength(char* page, int32_t slot_id)  { return *reinterpret_cast<int32_t*>(SlotPtr(page, slot_id) + 4); }
static inline int32_t SlotLengthRead(const char* page, int32_t slot_id) { return *reinterpret_cast<const int32_t*>(SlotPtrRead(page, slot_id) + 4); }

void TablePage::Init(char* page_data) {
    NumTuples(page_data) = 0;
    FreeOffset(page_data) = PAGE_SIZE;
    NextPid(page_data) = INVALID_PAGE_ID;
}

int32_t TablePage::GetNumTuples(const char* page_data)     { return NumTuplesRead(page_data); }
page_id_t TablePage::GetNextPageId(const char* page_data)  { return NextPidRead(page_data); }
void TablePage::SetNextPageId(char* page_data, page_id_t next_page_id) { NextPid(page_data) = next_page_id; }

int32_t TablePage::GetFreeSpace(const char* page_data) {
    int32_t n = NumTuplesRead(page_data);
    int32_t free_off = FreeOffsetRead(page_data);
    int32_t slot_array_end = PAGE_HEADER_SIZE + n * SLOT_SIZE;
    return free_off - slot_array_end;
}

bool TablePage::InsertTuple(char* page_data, const Tuple& tuple, int32_t* slot_id) {
    int32_t tuple_size = static_cast<int32_t>(tuple.GetSize());
    if (GetFreeSpace(page_data) < tuple_size + SLOT_SIZE) return false;

    int32_t n = NumTuplesRead(page_data);
    int32_t free_off = FreeOffsetRead(page_data);

    int32_t new_tuple_offset = free_off - tuple_size;
    std::memcpy(page_data + new_tuple_offset, tuple.GetData(), tuple_size);

    SlotOffset(page_data, n) = new_tuple_offset;
    SlotLength(page_data, n) = tuple_size;

    NumTuples(page_data) = n + 1;
    FreeOffset(page_data) = new_tuple_offset;

    *slot_id = n;
    return true;
}

bool TablePage::GetTuple(const char* page_data, int32_t slot_id, Tuple* tuple) {
    int32_t n = NumTuplesRead(page_data);
    if (slot_id < 0 || slot_id >= n) return false;

    int32_t offset = SlotOffsetRead(page_data, slot_id);
    int32_t length = SlotLengthRead(page_data, slot_id);
    tuple->SetData(page_data + offset, length);
    return true;
}

}  // namespace minidb
