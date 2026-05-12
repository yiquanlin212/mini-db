#pragma once

#include <cstdint>

#include "minidb/storage/page.h"
#include "minidb/storage/tuple.h"  // RID

namespace minidb {

constexpr int32_t NODE_TYPE_LEAF = 1;
constexpr int32_t NODE_TYPE_INTERNAL = 2;

// Leaf node layout:
//
// Header (24 bytes):
//   bytes 0-3:   node_type    (1 = leaf)
//   bytes 4-7:   size         (current # of entries)
//   bytes 8-11:  max_size     (capacity)
//   bytes 12-15: parent_pid   (page_id of parent; INVALID if root)
//   bytes 16-19: next_pid     (next leaf in linked list, INVALID if last)
//   bytes 20-23: reserved (padding)
//
// Entries (16 bytes each, sorted by key):
//   bytes 0-7:   key (int64)
//   bytes 8-11:  RID.page_id  (int32)
//   bytes 12-15: RID.slot_id  (int32)

constexpr int32_t BPT_HEADER_SIZE = 24;
constexpr int32_t LEAF_ENTRY_SIZE = 16;

class BPlusTreeLeaf {
public:
    static void Init(char* page_data, int32_t max_size,
                     page_id_t parent = INVALID_PAGE_ID);

    static int32_t GetSize(const char* page_data);
    static int32_t GetMaxSize(const char* page_data);
    static bool IsFull(const char* page_data);

    static page_id_t GetParent(const char* page_data);
    static void SetParent(char* page_data, page_id_t parent);

    static page_id_t GetNextPageId(const char* page_data);
    static void SetNextPageId(char* page_data, page_id_t next);

    static int64_t KeyAt(const char* page_data, int32_t index);
    static RID ValueAt(const char* page_data, int32_t index);

    static bool Insert(char* page_data, int64_t key, const RID& value);
    static bool Find(const char* page_data, int64_t key, RID* out);

    // Move upper half from src to dst. Returns dst's new first key
    // (the separator key to be pushed up to the parent).
    static int64_t SplitInto(char* src_page, char* dst_page);
};

}  // namespace minidb
