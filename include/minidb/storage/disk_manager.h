#pragma once

#include <string>
#include "minidb/storage/page.h"

namespace minidb {

class DiskManager {
public:
    // Open (or create) a database file
    explicit DiskManager(const std::string& db_file);
    ~DiskManager();

    // Read PAGE_SIZE bytes from disk into page_data
    void ReadPage(page_id_t page_id, char* page_data);

    // Write PAGE_SIZE bytes from page_data to disk
    void WritePage(page_id_t page_id, const char* page_data);

    // Allocate a new page id (just bumps a counter)
    page_id_t AllocatePage();

    // Flush kernel page cache to physical disk
    void Sync();

    page_id_t GetNumPages() const { return next_page_id_; }

private:
    int fd_;                   // file descriptor (POSIX)
    std::string file_name_;
    page_id_t next_page_id_;
};

}  // namespace minidb
