#include "minidb/index/b_plus_tree_internal.h"

#include <cstring>

namespace minidb {

static inline int32_t& NodeType(char* p)              { return *reinterpret_cast<int32_t*>(p); }
static inline int32_t& Size(char* p)                  { return *reinterpret_cast<int32_t*>(p + 4); }
static inline int32_t SizeRead(const char* p)         { return *reinterpret_cast<const int32_t*>(p + 4); }
static inline int32_t& MaxSize(char* p)               { return *reinterpret_cast<int32_t*>(p + 8); }
static inline int32_t MaxSizeRead(const char* p)      { return *reinterpret_cast<const int32_t*>(p + 8); }
static inline page_id_t& Parent(char* p)              { return *reinterpret_cast<page_id_t*>(p + 12); }
static inline page_id_t ParentRead(const char* p)     { return *reinterpret_cast<const page_id_t*>(p + 12); }

static inline char* EntryAt(char* p, int32_t i) {
    return p + BPT_HEADER_SIZE + i * INTERNAL_ENTRY_SIZE;
}
static inline const char* EntryAtRead(const char* p, int32_t i) {
    return p + BPT_HEADER_SIZE + i * INTERNAL_ENTRY_SIZE;
}
static inline int64_t& KeyField(char* p, int32_t i) {
    return *reinterpret_cast<int64_t*>(EntryAt(p, i));
}
static inline int64_t KeyFieldRead(const char* p, int32_t i) {
    return *reinterpret_cast<const int64_t*>(EntryAtRead(p, i));
}
static inline page_id_t& ChildField(char* p, int32_t i) {
    return *reinterpret_cast<page_id_t*>(EntryAt(p, i) + 8);
}
static inline page_id_t ChildFieldRead(const char* p, int32_t i) {
    return *reinterpret_cast<const page_id_t*>(EntryAtRead(p, i) + 8);
}

void BPlusTreeInternal::Init(char* p, int32_t max_size, page_id_t parent) {
    NodeType(p) = NODE_TYPE_INTERNAL;
    Size(p) = 0;
    MaxSize(p) = max_size;
    Parent(p) = parent;
}

int32_t BPlusTreeInternal::GetSize(const char* p)         { return SizeRead(p); }
int32_t BPlusTreeInternal::GetMaxSize(const char* p)      { return MaxSizeRead(p); }
bool BPlusTreeInternal::IsFull(const char* p)             { return SizeRead(p) >= MaxSizeRead(p); }
page_id_t BPlusTreeInternal::GetParent(const char* p)     { return ParentRead(p); }
void BPlusTreeInternal::SetParent(char* p, page_id_t par) { Parent(p) = par; }

int64_t BPlusTreeInternal::KeyAt(const char* p, int32_t i)     { return KeyFieldRead(p, i); }
page_id_t BPlusTreeInternal::ChildAt(const char* p, int32_t i) { return ChildFieldRead(p, i); }

void BPlusTreeInternal::PopulateRoot(char* p, page_id_t left_child,
                                     int64_t key, page_id_t right_child) {
    KeyField(p, 0) = 0;
    ChildField(p, 0) = left_child;
    KeyField(p, 1) = key;
    ChildField(p, 1) = right_child;
    Size(p) = 2;
}

int32_t BPlusTreeInternal::FindChildIndex(const char* p, int64_t key) {
    int32_t size = SizeRead(p);
    int32_t idx = 0;
    for (int32_t i = 1; i < size; ++i) {
        if (KeyFieldRead(p, i) <= key) idx = i;
        else break;
    }
    return idx;
}

void BPlusTreeInternal::InsertAfter(char* p, page_id_t left_child,
                                    int64_t key, page_id_t right_child) {
    int32_t size = SizeRead(p);
    int32_t i = 0;
    while (i < size && ChildFieldRead(p, i) != left_child) ++i;
    int32_t pos = i + 1;
    if (pos < size) {
        std::memmove(EntryAt(p, pos + 1),
                     EntryAt(p, pos),
                     (size - pos) * INTERNAL_ENTRY_SIZE);
    }
    KeyField(p, pos) = key;
    ChildField(p, pos) = right_child;
    Size(p) = size + 1;
}

int64_t BPlusTreeInternal::SplitInto(char* src, char* dst) {
    int32_t size = SizeRead(src);
    int32_t mid = size / 2;
    int64_t middle_key = KeyFieldRead(src, mid);
    int32_t moved = size - mid;

    std::memcpy(EntryAt(dst, 0), EntryAtRead(src, mid), moved * INTERNAL_ENTRY_SIZE);
    KeyField(dst, 0) = 0;  // dst's first child has no separator key

    Size(src) = mid;
    Size(dst) = moved;

    return middle_key;
}

}  // namespace minidb
