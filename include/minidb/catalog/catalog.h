#pragma once

#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include "minidb/catalog/schema.h"
#include "minidb/index/b_plus_tree.h"
#include "minidb/recovery/log_manager.h"
#include "minidb/recovery/transaction.h"
#include "minidb/storage/buffer_pool_manager.h"
#include "minidb/storage/table_heap.h"

namespace minidb {

struct TableInfo {
    std::string                name;
    Schema                     schema;
    std::unique_ptr<TableHeap> heap;
};

struct IndexInfo {
    std::string                name;
    std::string                table_name;
    int32_t                    key_column;
    std::unique_ptr<BPlusTree> tree;
};

class Catalog {
public:
    explicit Catalog(BufferPoolManager* bpm) : bpm_(bpm) {}

    TableInfo* CreateTable(const std::string& name, Schema schema) {
        if (tables_.count(name)) {
            throw std::runtime_error("CreateTable: table already exists: " + name);
        }
        auto info = std::make_unique<TableInfo>();
        info->name = name;
        info->schema = std::move(schema);
        info->heap = std::make_unique<TableHeap>(bpm_);
        TableInfo* raw = info.get();
        tables_[name] = std::move(info);
        return raw;
    }

    TableInfo* GetTable(const std::string& name) {
        auto it = tables_.find(name);
        return it == tables_.end() ? nullptr : it->second.get();
    }

    IndexInfo* CreateIndex(const std::string& index_name,
                           const std::string& table_name,
                           const std::string& key_column_name,
                           int32_t leaf_max = 32,
                           int32_t internal_max = 32) {
        if (indexes_.count(index_name)) {
            throw std::runtime_error("CreateIndex: index already exists: " + index_name);
        }
        TableInfo* table = GetTable(table_name);
        if (!table) throw std::runtime_error("CreateIndex: no such table: " + table_name);
        int32_t col_idx = table->schema.IndexOf(key_column_name);
        if (col_idx < 0) throw std::runtime_error("CreateIndex: no such column: " + key_column_name);
        if (table->schema.GetColumn(col_idx).type != TypeId::INTEGER) {
            throw std::runtime_error("CreateIndex: only INTEGER keys supported for now");
        }

        auto info = std::make_unique<IndexInfo>();
        info->name = index_name;
        info->table_name = table_name;
        info->key_column = col_idx;
        info->tree = std::make_unique<BPlusTree>(bpm_, leaf_max, internal_max);

        for (auto it = table->heap->begin(); it != table->heap->end(); ++it) {
            const Tuple& t = *it;
            Value key = TupleCodec::DecodeColumn(table->schema, t, col_idx);
            info->tree->Insert(key.AsInt(), it.GetRID());
        }

        IndexInfo* raw = info.get();
        indexes_[index_name] = std::move(info);
        return raw;
    }

    IndexInfo* GetIndex(const std::string& index_name) {
        auto it = indexes_.find(index_name);
        return it == indexes_.end() ? nullptr : it->second.get();
    }

    bool InsertRow(const std::string& table_name, const std::vector<Value>& row, RID* rid_out = nullptr) {
        TableInfo* table = GetTable(table_name);
        if (!table) return false;
        Tuple t = TupleCodec::Encode(table->schema, row);
        RID rid;
        if (!table->heap->InsertTuple(t, &rid)) return false;
        if (rid_out) *rid_out = rid;

        for (auto& [name, idx] : indexes_) {
            if (idx->table_name != table_name) continue;
            Value key = row[idx->key_column];
            if (key.GetType() != TypeId::INTEGER) continue;
            idx->tree->Insert(key.AsInt(), rid);
        }
        return true;
    }

    bool InsertRowLogged(const std::string& table_name,
                         const std::vector<Value>& row,
                         Transaction* txn,
                         LogManager* log,
                         RID* rid_out = nullptr) {
        TableInfo* table = GetTable(table_name);
        if (!table) return false;
        Tuple t = TupleCodec::Encode(table->schema, row);
        RID rid;
        if (!table->heap->InsertTuple(t, &rid)) return false;

        log->AppendInsert(txn->GetId(), table_name, rid, t.GetData(), t.GetSize());

        if (rid_out) *rid_out = rid;

        for (auto& [name, idx] : indexes_) {
            if (idx->table_name != table_name) continue;
            Value key = row[idx->key_column];
            if (key.GetType() != TypeId::INTEGER) continue;
            idx->tree->Insert(key.AsInt(), rid);
        }
        return true;
    }

private:
    BufferPoolManager* bpm_;
    std::unordered_map<std::string, std::unique_ptr<TableInfo>> tables_;
    std::unordered_map<std::string, std::unique_ptr<IndexInfo>> indexes_;
};

}  // namespace minidb
