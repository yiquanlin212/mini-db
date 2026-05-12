#include "minidb/index/b_plus_tree.h"

#include <iostream>
#include <string>
#include <vector>

namespace minidb {

static inline int32_t NodeTypeOf(const char* p) {
    return *reinterpret_cast<const int32_t*>(p);
}

BPlusTree::BPlusTree(BufferPoolManager* bpm,
                     int32_t leaf_max_size,
                     int32_t internal_max_size)
    : bpm_(bpm),
      leaf_max_size_(leaf_max_size),
      internal_max_size_(internal_max_size) {
    Page* root = bpm_->NewPage(&root_page_id_);
    BPlusTreeLeaf::Init(root->GetData(), leaf_max_size_);
    bpm_->UnpinPage(root_page_id_, true);
}

bool BPlusTree::Find(int64_t key, RID* value) {
    page_id_t pid = root_page_id_;
    while (true) {
        Page* page = bpm_->FetchPage(pid);
        const char* data = page->GetData();
        if (NodeTypeOf(data) == NODE_TYPE_LEAF) {
            bool ok = BPlusTreeLeaf::Find(data, key, value);
            bpm_->UnpinPage(pid, false);
            return ok;
        }
        int32_t idx = BPlusTreeInternal::FindChildIndex(data, key);
        page_id_t child = BPlusTreeInternal::ChildAt(data, idx);
        bpm_->UnpinPage(pid, false);
        pid = child;
    }
}

BPlusTree::SplitInfo BPlusTree::InsertRec(page_id_t pid, int64_t key,
                                          const RID& value, bool* ok_out) {
    Page* page = bpm_->FetchPage(pid);
    char* data = page->GetData();

    // ---------- LEAF ----------
    if (NodeTypeOf(data) == NODE_TYPE_LEAF) {
        if (!BPlusTreeLeaf::IsFull(data)) {
            bool ok = BPlusTreeLeaf::Insert(data, key, value);
            *ok_out = ok;
            bpm_->UnpinPage(pid, ok);
            return std::nullopt;
        }

        page_id_t new_pid;
        Page* new_page = bpm_->NewPage(&new_pid);
        if (!new_page) {
            *ok_out = false;
            bpm_->UnpinPage(pid, false);
            return std::nullopt;
        }
        BPlusTreeLeaf::Init(new_page->GetData(), leaf_max_size_,
                            BPlusTreeLeaf::GetParent(data));

        BPlusTreeLeaf::SplitInto(data, new_page->GetData());

        BPlusTreeLeaf::SetNextPageId(new_page->GetData(),
                                     BPlusTreeLeaf::GetNextPageId(data));
        BPlusTreeLeaf::SetNextPageId(data, new_pid);

        int64_t new_first_key = BPlusTreeLeaf::KeyAt(new_page->GetData(), 0);
        bool ok;
        if (key < new_first_key) {
            ok = BPlusTreeLeaf::Insert(data, key, value);
        } else {
            ok = BPlusTreeLeaf::Insert(new_page->GetData(), key, value);
        }
        *ok_out = ok;

        int64_t sep_key = BPlusTreeLeaf::KeyAt(new_page->GetData(), 0);

        bpm_->UnpinPage(new_pid, true);
        bpm_->UnpinPage(pid, true);
        return std::make_pair(sep_key, new_pid);
    }

    // ---------- INTERNAL ----------
    int32_t idx = BPlusTreeInternal::FindChildIndex(data, key);
    page_id_t child_pid = BPlusTreeInternal::ChildAt(data, idx);
    bpm_->UnpinPage(pid, false);

    SplitInfo from_child = InsertRec(child_pid, key, value, ok_out);
    if (!from_child.has_value()) {
        return std::nullopt;
    }

    int64_t sep_key = from_child->first;
    page_id_t new_right = from_child->second;

    page = bpm_->FetchPage(pid);
    data = page->GetData();

    if (!BPlusTreeInternal::IsFull(data)) {
        BPlusTreeInternal::InsertAfter(data, child_pid, sep_key, new_right);
        Page* rp = bpm_->FetchPage(new_right);
        if (NodeTypeOf(rp->GetData()) == NODE_TYPE_LEAF) {
            BPlusTreeLeaf::SetParent(rp->GetData(), pid);
        } else {
            BPlusTreeInternal::SetParent(rp->GetData(), pid);
        }
        bpm_->UnpinPage(new_right, true);
        bpm_->UnpinPage(pid, true);
        return std::nullopt;
    }

    // Internal full -> split. Insert temporarily, then split.
    BPlusTreeInternal::InsertAfter(data, child_pid, sep_key, new_right);

    page_id_t sibling_pid;
    Page* sibling_page = bpm_->NewPage(&sibling_pid);
    if (!sibling_page) {
        *ok_out = false;
        bpm_->UnpinPage(pid, true);
        return std::nullopt;
    }
    BPlusTreeInternal::Init(sibling_page->GetData(), internal_max_size_,
                            BPlusTreeInternal::GetParent(data));

    int64_t push_up_key = BPlusTreeInternal::SplitInto(data, sibling_page->GetData());

    // Update parent pointer for every child now in sibling
    int32_t sib_size = BPlusTreeInternal::GetSize(sibling_page->GetData());
    for (int32_t i = 0; i < sib_size; ++i) {
        page_id_t c = BPlusTreeInternal::ChildAt(sibling_page->GetData(), i);
        Page* cp = bpm_->FetchPage(c);
        if (NodeTypeOf(cp->GetData()) == NODE_TYPE_LEAF) {
            BPlusTreeLeaf::SetParent(cp->GetData(), sibling_pid);
        } else {
            BPlusTreeInternal::SetParent(cp->GetData(), sibling_pid);
        }
        bpm_->UnpinPage(c, true);
    }

    bpm_->UnpinPage(sibling_pid, true);
    bpm_->UnpinPage(pid, true);
    return std::make_pair(push_up_key, sibling_pid);
}

bool BPlusTree::Insert(int64_t key, const RID& value) {
    bool ok = true;
    SplitInfo from_root = InsertRec(root_page_id_, key, value, &ok);
    if (!from_root.has_value()) {
        return ok;
    }

    // Root split: create a new internal root
    page_id_t new_root_pid;
    Page* new_root_page = bpm_->NewPage(&new_root_pid);
    BPlusTreeInternal::Init(new_root_page->GetData(), internal_max_size_);
    BPlusTreeInternal::PopulateRoot(new_root_page->GetData(),
                                    root_page_id_,
                                    from_root->first,
                                    from_root->second);

    Page* lc = bpm_->FetchPage(root_page_id_);
    if (NodeTypeOf(lc->GetData()) == NODE_TYPE_LEAF) {
        BPlusTreeLeaf::SetParent(lc->GetData(), new_root_pid);
    } else {
        BPlusTreeInternal::SetParent(lc->GetData(), new_root_pid);
    }
    bpm_->UnpinPage(root_page_id_, true);

    Page* rc = bpm_->FetchPage(from_root->second);
    if (NodeTypeOf(rc->GetData()) == NODE_TYPE_LEAF) {
        BPlusTreeLeaf::SetParent(rc->GetData(), new_root_pid);
    } else {
        BPlusTreeInternal::SetParent(rc->GetData(), new_root_pid);
    }
    bpm_->UnpinPage(from_root->second, true);

    root_page_id_ = new_root_pid;
    bpm_->UnpinPage(new_root_pid, true);
    return ok;
}

// -------------------- Debug printing --------------------

void BPlusTree::PrintNode(page_id_t pid, int depth) {
    Page* page = bpm_->FetchPage(pid);
    const char* data = page->GetData();
    std::string indent(depth * 2, ' ');

    if (NodeTypeOf(data) == NODE_TYPE_LEAF) {
        int32_t n = BPlusTreeLeaf::GetSize(data);
        std::cout << indent << "[Leaf pid=" << pid << " size=" << n << " keys=";
        for (int32_t i = 0; i < n; ++i) {
            std::cout << BPlusTreeLeaf::KeyAt(data, i);
            if (i + 1 < n) std::cout << ",";
        }
        std::cout << " next=" << BPlusTreeLeaf::GetNextPageId(data) << "]" << std::endl;
        bpm_->UnpinPage(pid, false);
        return;
    }

    int32_t n = BPlusTreeInternal::GetSize(data);
    std::cout << indent << "[Internal pid=" << pid << " size=" << n << " keys=(";
    for (int32_t i = 1; i < n; ++i) {
        std::cout << BPlusTreeInternal::KeyAt(data, i);
        if (i + 1 < n) std::cout << ",";
    }
    std::cout << ")]" << std::endl;

    std::vector<page_id_t> children;
    children.reserve(n);
    for (int32_t i = 0; i < n; ++i) {
        children.push_back(BPlusTreeInternal::ChildAt(data, i));
    }
    bpm_->UnpinPage(pid, false);
    for (page_id_t c : children) {
        PrintNode(c, depth + 1);
    }
}

void BPlusTree::Print() {
    std::cout << "----- B+ Tree (root=" << root_page_id_ << ") -----" << std::endl;
    PrintNode(root_page_id_, 0);
    std::cout << "----------------------------------" << std::endl;
}

}  // namespace minidb
