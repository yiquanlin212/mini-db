#include "minidb/storage/table_heap.h"

namespace minidb {

TableHeap::TableHeap(BufferPoolManager* bpm) : bpm_(bpm) {
    Page* page = bpm_->NewPage(&first_page_id_);
    TablePage::Init(page->GetData());
    bpm_->UnpinPage(first_page_id_, true);
    last_page_id_ = first_page_id_;
}

bool TableHeap::InsertTuple(const Tuple& tuple, RID* rid) {
    // Always try to insert into the last page first (append-only behavior)
    Page* page = bpm_->FetchPage(last_page_id_);
    int32_t slot_id;
    if (TablePage::InsertTuple(page->GetData(), tuple, &slot_id)) {
        *rid = {last_page_id_, slot_id};
        bpm_->UnpinPage(last_page_id_, true);
        return true;
    }

    // Last page is full: allocate a new one and link it
    page_id_t new_pid;
    Page* new_page = bpm_->NewPage(&new_pid);
    if (!new_page) {
        bpm_->UnpinPage(last_page_id_, false);
        return false;
    }
    TablePage::Init(new_page->GetData());
    TablePage::SetNextPageId(page->GetData(), new_pid);
    bpm_->UnpinPage(last_page_id_, true);  // mark dirty: chain pointer changed

    last_page_id_ = new_pid;

    bool ok = TablePage::InsertTuple(new_page->GetData(), tuple, &slot_id);
    bpm_->UnpinPage(new_pid, ok);
    if (ok) *rid = {new_pid, slot_id};
    return ok;
}

bool TableHeap::GetTuple(const RID& rid, Tuple* out_tuple) {
    Page* page = bpm_->FetchPage(rid.page_id);
    if (!page) return false;
    bool ok = TablePage::GetTuple(page->GetData(), rid.slot_id, out_tuple);
    bpm_->UnpinPage(rid.page_id, false);
    return ok;
}

TableIterator TableHeap::begin() {
    Page* page = bpm_->FetchPage(first_page_id_);
    int32_t n = TablePage::GetNumTuples(page->GetData());
    bpm_->UnpinPage(first_page_id_, false);
    if (n > 0) return TableIterator(this, RID{first_page_id_, 0});
    return end();
}

TableIterator TableHeap::end() {
    return TableIterator(this, RID{INVALID_PAGE_ID, 0});
}

// ----------------- TableIterator -----------------

TableIterator::TableIterator(TableHeap* heap, RID rid) : heap_(heap), rid_(rid) {
    if (rid_.page_id != INVALID_PAGE_ID) Load();
}

void TableIterator::Load() {
    Page* page = heap_->bpm_->FetchPage(rid_.page_id);
    TablePage::GetTuple(page->GetData(), rid_.slot_id, &current_tuple_);
    heap_->bpm_->UnpinPage(rid_.page_id, false);
}

TableIterator& TableIterator::operator++() {
    Page* page = heap_->bpm_->FetchPage(rid_.page_id);
    int32_t n = TablePage::GetNumTuples(page->GetData());
    page_id_t next_pid = TablePage::GetNextPageId(page->GetData());
    heap_->bpm_->UnpinPage(rid_.page_id, false);

    // Try next slot on the same page
    if (rid_.slot_id + 1 < n) {
        rid_.slot_id++;
        Load();
        return *this;
    }
    // Otherwise move to next page (or end)
    if (next_pid == INVALID_PAGE_ID) {
        rid_ = {INVALID_PAGE_ID, 0};
        return *this;
    }
    rid_ = {next_pid, 0};
    Load();
    return *this;
}

}  // namespace minidb
