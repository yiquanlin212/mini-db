#pragma once

#include <cstddef>
#include <list>
#include <unordered_map>
#include <vector>

#include "minidb/storage/disk_manager.h"
#include "minidb/storage/page.h"

namespace minidb {

class BufferPoolManager {
public:
    BufferPoolManager(size_t pool_size, DiskManager* disk_manager);
    ~BufferPoolManager();

    // Allocate a new page; returns pointer to the Page in memory (pinned).
    // Out-param page_id receives the new page's id.
    Page* NewPage(page_id_t* page_id);

    // Bring page_id into the pool (or return it if already there). Pinned.
    Page* FetchPage(page_id_t page_id);

    // Release a previously-pinned page. If is_dirty, mark it dirty.
    bool UnpinPage(page_id_t page_id, bool is_dirty);

    // Force a single page to disk (does not unpin).
    bool FlushPage(page_id_t page_id);

    // Force every dirty page in the pool to disk.
    void FlushAllPages();

    size_t GetPoolSize() const { return pool_size_; }

private:
    size_t pool_size_;
    std::vector<Page> pages_;           // the actual frames
    std::vector<int> pin_counts_;       // per-frame pin count
    DiskManager* disk_manager_;

    // page_id -> frame index in pages_
    std::unordered_map<page_id_t, size_t> page_table_;

    // LRU list of UNPINNED frames. Front = MRU, back = LRU (next to evict).
    std::list<size_t> lru_list_;
    std::unordered_map<size_t, std::list<size_t>::iterator> lru_map_;

    // Picks an unpinned frame to (re)use. Returns pool_size_ if all pinned.
    size_t FindVictimFrame();

    // Evict whatever page currently lives in frame_id (write back if dirty).
    void EvictFrame(size_t frame_id);
};

}  // namespace minidb
