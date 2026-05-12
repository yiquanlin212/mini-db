#include "minidb/index/b_plus_tree_leaf.h"

#include <cstring>

namespace minidb {

static inline int32_t& NodeType(char* p)              { return *reinterpret_cast<int32_t*>(p); }
static inline int32_t& Size(char* p)                  { return *reinterpret_cast<int32_t*>(p + 4); }
static inline int32_t SizeRead(const char* p)         { return *reinterpret_cast<const int32_t*>(p + 4); }
static inline int32_t& MaxSize(char* p)               { return *reinterpret_cast<int32_t*>(p + 8); }
static inline int32_t MaxSizeRead(const char* p)      { return *reinterpret_cast<const int32_t*>(p + 8); }
static inline page_id_t& Parent(char* p)              { return *reinterpret_cast<page_id_t*>(p + 12); }
static inline page_id_t ParentRead(const char* p)     { return *reinterpret_cast<const page_id_t*>(p + 12); }
static inline page_id_t& NextPid(char* p)             { return *reinterpret_cast<page_id_t*>(p + 16); }
static inline page_id_t NextPidRead(const char* p)    { return *reinterpret_cast<const page_id_t*>(p + 16); }

static inline char* EntryAt(char* p, int32_t i) {
    return p + BPT_HEADER_SIZE + i * LEAF_ENTRY_SIZE;
}
static inline const char* EntryAtRead(const char* p, int32_t i) {
    return p + BPT_HEADER_SIZE + i * LEAF_ENTRY_SIZE;
}

void BPlusTreeLeaf::Init(char* page_data, int32_t max_size, page_id_t parent) {
    NodeType(page_data) = NODE_TYPE_LEAF;
    Size(page_data) = 0;
    MaxSize(page_data) = max_size;
    Parent(page_data) = parent;
    NextPid(page_data) = INVALID_PAGE_ID;
}

int32_t BPlusTreeLeaf::GetSize(const char* p)        { return SizeRead(p); }
int32_t BPlusTreeLeaf::GetMaxSize(const char* p)     { return MaxSizeRead(p); }
bool BPlusTreeLeaf::IsFull(const char* p)            { return SizeRead(p) >= MaxSizeRead(p); }
page_id_t BPlusTreeLeaf::GetParent(const char* p)    { return ParentRead(p); }
void BPlusTreeLeaf::SetParent(char* p, page_id_t parent) { Parent(p) = parent; }
page_id_t BPlusTreeLeaf::GetNextPageId(const char* p){ return NextPidRead(p); }
void BPlusTreeLeaf::SetNextPageId(char* p, page_id_t n) { NextPid(p) = n; }

int64_t BPlusTreeLeaf::KeyAt(const char* p, int32_t i) {
    return *reinterpret_cast<const int64_t*>(EntryAtRead(p, i));
}

RID BPlusTreeLeaf::ValueAt(const char* p, int32_t i) {
    const char* e = EntryAtRead(p, i);
    return RID{ *reinterpret_cast<const page_id_t*>(e + 8),
                *reinterpret_cast<const int32_t*>(e + 12) };
}

bool BPlusTreeLeaf::Insert(char* p, int64_t key, const RID& value) {
    int32_t size = SizeRead(p);

    int32_t left = 0, right = size;
    while (left < right) {
        int32_t mid = (left + right) / 2;
        int64_t mid_key = KeyAt(p, mid);
        if (mid_key < key)       left = mid + 1;
        else if (mid_key == key) return false;
        else                     right = mid;
    }

    if (left < size) {
        std::memmove(EntryAt(p, left + 1),
                     EntryAt(p, left),
                     (size - left) * LEAF_ENTRY_SIZE);
    }

    char* dst = EntryAt(p, left);
    *reinterpret_cast<int64_t*>(dst)        = key;
    *reinterpret_cast<page_id_t*>(dst + 8)  = value.page_id;
    *reinterpret_cast<int32_t*>(dst + 12)   = value.slot_id;

    Size(p) = size + 1;
    return true;
}

bool BPlusTreeLeaf::Find(const char* p, int64_t key, RID* out) {
    int32_t size = SizeRead(p);
    int32_t left = 0, right = size;
    while (left < right) {
        int32_t mid = (left + right) / 2;
        int64_t mid_key = KeyAt(p, mid);
        if (mid_key < key)       left = mid + 1;
        else if (mid_key == key) { *out = ValueAt(p, mid); return true; }
        else                     right = mid;
    }
    return false;
}

int64_t BPlusTreeLeaf::SplitInto(char* src, char* dst) {
    int32_t size = SizeRead(src);
    int32_t mid = size / 2;
    int32_t moved = size - mid;

    std::memcpy(EntryAt(dst, 0), EntryAtRead(src, mid), moved * LEAF_ENTRY_SIZE);

    Size(src) = mid;
    Size(dst) = moved;

    return KeyAt(dst, 0);
}

}  // namespace minidb
