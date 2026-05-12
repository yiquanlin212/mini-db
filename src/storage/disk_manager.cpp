#include "minidb/storage/disk_manager.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include <cstring>
#include <stdexcept>
#include <string>

namespace minidb {

DiskManager::DiskManager(const std::string& db_file)
    : fd_(-1), file_name_(db_file), next_page_id_(0) {
    // O_RDWR: read+write   O_CREAT: create if missing   0644: rw for owner, r for others
    fd_ = ::open(db_file.c_str(), O_RDWR | O_CREAT, 0644);
    if (fd_ < 0) {
        throw std::runtime_error("DiskManager: failed to open " + db_file);
    }

    // Figure out how many pages already exist by checking file size
    struct stat st;
    if (::fstat(fd_, &st) < 0) {
        ::close(fd_);
        throw std::runtime_error("DiskManager: fstat failed");
    }
    next_page_id_ = static_cast<page_id_t>(st.st_size / PAGE_SIZE);
}

DiskManager::~DiskManager() {
    if (fd_ >= 0) {
        ::fsync(fd_);
        ::close(fd_);
    }
}

void DiskManager::ReadPage(page_id_t page_id, char* page_data) {
    off_t offset = static_cast<off_t>(page_id) * PAGE_SIZE;
    ssize_t n = ::pread(fd_, page_data, PAGE_SIZE, offset);
    if (n < 0) {
        throw std::runtime_error("DiskManager: pread failed");
    }
    // Reading past EOF returns fewer bytes — zero-fill the tail
    if (n < static_cast<ssize_t>(PAGE_SIZE)) {
        std::memset(page_data + n, 0, PAGE_SIZE - n);
    }
}

void DiskManager::WritePage(page_id_t page_id, const char* page_data) {
    off_t offset = static_cast<off_t>(page_id) * PAGE_SIZE;
    ssize_t n = ::pwrite(fd_, page_data, PAGE_SIZE, offset);
    if (n != static_cast<ssize_t>(PAGE_SIZE)) {
        throw std::runtime_error("DiskManager: pwrite incomplete");
    }
}

page_id_t DiskManager::AllocatePage() {
    return next_page_id_++;
}

void DiskManager::Sync() {
    if (::fsync(fd_) < 0) {
        throw std::runtime_error("DiskManager: fsync failed");
    }
}

}  // namespace minidb
