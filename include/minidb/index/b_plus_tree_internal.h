#pragma once

#include <cstdint>

#include "minidb/index/b_plus_tree_leaf.h"
#include "minidb/storage/page.h"

namespace minidb {

// Internal node layout — shares the 24-byte BPT_HEADER_SIZE.
// `size` = number of CHILDREN  (so there are size-1 separator keys)
// Logical: child[0], key[1], child[1], key[2], child[2], ..., key[n-1], child[n-1]
//
// Invariant: for entry i (i>=1), all keys in subtree(child[i]) are >= key[i]
//            all keys in subtree(child[i-1]) are < key[i]
//
// Physical: 16-byte entry slot (same size as leaves)
//   Entry i: [key:8][child_pid:4][padding:4]
//   - entry 0's key field is unused (no separator before leftmost child)

constexpr int32_t INTERNAL_ENTRY_SIZE = 16;

class BPlusTreeInternal {
public:
    static void Init(char* page_data, int32_t max_size,
                     page_id_t parent = INVALID_PAGE_ID);

    static int32_t GetSize(const char* page_data);
    static int32_t GetMaxSize(const char* page_data);
    static bool IsFull(const char* page_data);

    static page_id_t GetParent(const char* page_data);
    static void SetParent(char* page_data, page_id_t parent);

    static int64_t KeyAt(const char* page_data, int32_t index);
    static page_id_t ChildAt(const char* page_data, int32_t index);

    static void PopulateRoot(char* page_data, page_id_t left_child,
                             int64_t key, page_id_t right_child);

    static int32_t FindChildIndex(const char* page_data, int64_t key);

    static void InsertAfter(char* page_data, page_id_t left_child,
                            int64_t key, page_id_t right_child);

    // Split: return the middle key that gets PUSHED UP (not duplicated)
    static int64_t SplitInto(char* src_page, char* dst_page);
};

}  // namespace minidb
