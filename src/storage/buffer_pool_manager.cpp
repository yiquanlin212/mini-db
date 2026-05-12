#include "minidb/storage/buffer_pool_manager.h"

#include <cstring>

namespace minidb {

BufferPoolManager::BufferPoolManager(size_t pool_size, DiskManager* disk_manager)
    : pool_size_(pool_size),
      pages_(pool_size),
      pin_counts_(pool_size, 0),
      disk_manager_(disk_manager) {
    // All frames start empty + unpinned, so all are eviction candidates.
    for (size_t i = 0; i < pool_size_; ++i) {
        lru_list_.push_back(i);
        lru_map_[i] = std::prev(lru_list_.end());
    }
}

BufferPoolManager::~BufferPoolManager() {
    FlushAllPages();
}

size_t BufferPoolManager::FindVictimFrame() {
    if (lru_list_.empty()) {
        return pool_size_;  // every frame is pinned → no victim available
    }
    size_t frame_id = lru_list_.back();
    lru_list_.pop_back();
    lru_map_.erase(frame_id);
    return frame_id;
}

void BufferPoolManager::EvictFrame(size_t frame_id) {
    Page& frame = pages_[frame_id];
    if (frame.GetPageId() == INVALID_PAGE_ID) {
        return;  // frame is empty, nothing to evict
    }
    if (frame.IsDirty()) {
        disk_manager_->WritePage(frame.GetPageId(), frame.GetData());
    }
    page_table_.erase(frame.GetPageId());
}

Page* BufferPoolManager::NewPage(page_id_t* page_id) {
    size_t frame_id = FindVictimFrame();
    if (frame_id == pool_size_) return nullptr;

    EvictFrame(frame_id);

    page_id_t new_pid = disk_manager_->AllocatePage();
    Page& frame = pages_[frame_id];
    frame.SetPageId(new_pid);
    frame.SetDirty(false);
    std::memset(frame.GetData(), 0, PAGE_SIZE);

    page_table_[new_pid] = frame_id;
    pin_counts_[frame_id] = 1;

    *page_id = new_pid;
    return &frame;
}

Page* BufferPoolManager::FetchPage(page_id_t page_id) {
    // Case 1: cache hit
    auto it = page_table_.find(page_id);
    if (it != page_table_.end()) {
        size_t frame_id = it->second;
        if (pin_counts_[frame_id] == 0) {
            // It was sitting in LRU; pull it out before pinning
            lru_list_.erase(lru_map_[frame_id]);
            lru_map_.erase(frame_id);
        }
        pin_counts_[frame_id]++;
        return &pages_[frame_id];
    }

    // Case 2: cache miss — need a frame, read from disk
    size_t frame_id = FindVictimFrame();
    if (frame_id == pool_size_) return nullptr;

    EvictFrame(frame_id);

    Page& frame = pages_[frame_id];
    frame.SetPageId(page_id);
    frame.SetDirty(false);
    disk_manager_->ReadPage(page_id, frame.GetData());

    page_table_[page_id] = frame_id;
    pin_counts_[frame_id] = 1;
    return &frame;
}

bool BufferPoolManager::UnpinPage(page_id_t page_id, bool is_dirty) {
    auto it = page_table_.find(page_id);
    if (it == page_table_.end()) return false;

    size_t frame_id = it->second;
    if (pin_counts_[frame_id] <= 0) return false;

    if (is_dirty) {
        pages_[frame_id].SetDirty(true);
    }

    pin_counts_[frame_id]--;
    if (pin_counts_[frame_id] == 0) {
        // Newly evictable → put at front (most recently used end)
        lru_list_.push_front(frame_id);
        lru_map_[frame_id] = lru_list_.begin();
    }
    return true;
}

bool BufferPoolManager::FlushPage(page_id_t page_id) {
    auto it = page_table_.find(page_id);
    if (it == page_table_.end()) return false;
    size_t frame_id = it->second;
    disk_manager_->WritePage(page_id, pages_[frame_id].GetData());
    pages_[frame_id].SetDirty(false);
    return true;
}

void BufferPoolManager::FlushAllPages() {
    for (const auto& [pid, fid] : page_table_) {
        if (pages_[fid].IsDirty()) {
            disk_manager_->WritePage(pid, pages_[fid].GetData());
            pages_[fid].SetDirty(false);
        }
    }
}

}  // namespace minidb
