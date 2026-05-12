#pragma once

#include <cstdint>
#include <optional>
#include <utility>

#include "minidb/index/b_plus_tree_internal.h"
#include "minidb/index/b_plus_tree_leaf.h"
#include "minidb/storage/buffer_pool_manager.h"
#include "minidb/storage/tuple.h"

namespace minidb {

class BPlusTree {
public:
    BPlusTree(BufferPoolManager* bpm,
              int32_t leaf_max_size = 4,
              int32_t internal_max_size = 4);

    bool Insert(int64_t key, const RID& value);
    bool Find(int64_t key, RID* value);

    page_id_t GetRootPageId() const { return root_page_id_; }

    void Print();

private:
    BufferPoolManager* bpm_;
    page_id_t root_page_id_;
    int32_t leaf_max_size_;
    int32_t internal_max_size_;

    using SplitInfo = std::optional<std::pair<int64_t, page_id_t>>;

    SplitInfo InsertRec(page_id_t pid, int64_t key, const RID& value, bool* ok_out);
    void PrintNode(page_id_t pid, int depth);
};

}  // namespace minidb
