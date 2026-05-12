#pragma once

#include <memory>
#include <vector>

#include "minidb/catalog/catalog.h"
#include "minidb/catalog/schema.h"
#include "minidb/execution/expression.h"

namespace minidb {

// Volcano (iterator) execution model.
// Each operator: Init() resets state; Next(out) pulls one row from children.
class Executor {
public:
    virtual ~Executor() = default;
    virtual void Init() = 0;
    virtual bool Next(std::vector<Value>* out_row) = 0;
    virtual const Schema& OutputSchema() const = 0;
};

// SeqScan: full-table scan of a TableHeap.
class SeqScanExecutor : public Executor {
public:
    SeqScanExecutor(TableInfo* table)
        : table_(table),
          it_(table->heap.get(), RID{INVALID_PAGE_ID, 0}),
          end_(nullptr, RID{INVALID_PAGE_ID, 0}) {}

    void Init() override {
        it_ = table_->heap->begin();
        end_ = table_->heap->end();
    }

    bool Next(std::vector<Value>* out_row) override {
        if (it_ == end_) return false;
        *out_row = TupleCodec::Decode(table_->schema, *it_);
        ++it_;
        return true;
    }

    const Schema& OutputSchema() const override { return table_->schema; }

private:
    TableInfo*    table_;
    TableIterator it_;
    TableIterator end_;
};

// IndexScan: B+ tree point lookup -> table heap.
class IndexScanExecutor : public Executor {
public:
    IndexScanExecutor(TableInfo* table, IndexInfo* index, int64_t lookup_key)
        : table_(table), index_(index), lookup_key_(lookup_key) {}

    void Init() override { consumed_ = false; }

    bool Next(std::vector<Value>* out_row) override {
        if (consumed_) return false;
        consumed_ = true;
        RID rid;
        if (!index_->tree->Find(lookup_key_, &rid)) return false;
        Tuple t;
        if (!table_->heap->GetTuple(rid, &t)) return false;
        *out_row = TupleCodec::Decode(table_->schema, t);
        return true;
    }

    const Schema& OutputSchema() const override { return table_->schema; }

private:
    TableInfo* table_;
    IndexInfo* index_;
    int64_t    lookup_key_;
    bool       consumed_ = false;
};

// Filter: pull rows from a child, emit those satisfying the predicate.
class FilterExecutor : public Executor {
public:
    FilterExecutor(std::unique_ptr<Executor> child, ExprPtr predicate)
        : child_(std::move(child)), predicate_(std::move(predicate)) {}

    void Init() override { child_->Init(); }

    bool Next(std::vector<Value>* out_row) override {
        std::vector<Value> row;
        while (child_->Next(&row)) {
            if (predicate_->Evaluate(row).AsInt() != 0) {
                *out_row = std::move(row);
                return true;
            }
        }
        return false;
    }

    const Schema& OutputSchema() const override { return child_->OutputSchema(); }

private:
    std::unique_ptr<Executor> child_;
    ExprPtr                   predicate_;
};

}  // namespace minidb
