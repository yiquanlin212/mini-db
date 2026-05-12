#pragma once

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <memory>
#include <queue>
#include <string>
#include <vector>

#include "minidb/catalog/schema.h"
#include "minidb/execution/executor.h"

namespace minidb {

struct SortKey {
    int32_t col_idx;
    bool    ascending = true;
};

inline int CompareRows(const std::vector<Value>& a,
                       const std::vector<Value>& b,
                       const std::vector<SortKey>& keys) {
    for (const auto& k : keys) {
        const Value& va = a[k.col_idx];
        const Value& vb = b[k.col_idx];
        int c = 0;
        if (va.GetType() == TypeId::INTEGER) {
            int64_t x = va.AsInt(), y = vb.AsInt();
            c = (x < y) ? -1 : (x > y) ? 1 : 0;
        } else {
            const std::string& sa = va.AsVarchar();
            const std::string& sb = vb.AsVarchar();
            c = sa.compare(sb);
            if (c < 0) c = -1; else if (c > 0) c = 1;
        }
        if (c != 0) return k.ascending ? c : -c;
    }
    return 0;
}

// Two-phase external sort, same algorithm as Postgres/MySQL/SQLite/SQL Server.
// Phase 1: read run_size rows -> sort in memory -> write a "run" file.
//          Repeat until child is exhausted. K runs on disk.
// Phase 2: K-way merge using a min-heap. Pop min, emit, refill from that run.
class ExternalMergeSortExecutor : public Executor {
public:
    ExternalMergeSortExecutor(std::unique_ptr<Executor> child,
                              std::vector<SortKey> keys,
                              size_t run_size = 1024,
                              std::string tmp_prefix = "sort_run_")
        : child_(std::move(child)),
          keys_(std::move(keys)),
          run_size_(run_size),
          tmp_prefix_(std::move(tmp_prefix)) {}

    ~ExternalMergeSortExecutor() override {
        for (const auto& f : run_files_) std::remove(f.c_str());
    }

    void Init() override {
        for (const auto& f : run_files_) std::remove(f.c_str());
        run_files_.clear();
        readers_.clear();

        child_->Init();
        Phase1_GenerateRuns();
        Phase2_PrepareMerge();
    }

    bool Next(std::vector<Value>* out_row) override {
        if (heap_.empty()) return false;
        HeapEntry top = heap_.top(); heap_.pop();
        *out_row = std::move(top.row);

        std::vector<Value> next_row;
        if (ReadRowFromRun(top.run_idx, &next_row)) {
            heap_.push(HeapEntry{std::move(next_row), top.run_idx, &keys_});
        }
        return true;
    }

    const Schema& OutputSchema() const override { return child_->OutputSchema(); }

    size_t NumRuns() const { return run_files_.size(); }

private:
    void Phase1_GenerateRuns() {
        std::vector<std::vector<Value>> buf;
        buf.reserve(run_size_);
        std::vector<Value> row;
        while (child_->Next(&row)) {
            buf.push_back(std::move(row));
            if (buf.size() >= run_size_) {
                FlushRun(buf);
                buf.clear();
            }
        }
        if (!buf.empty()) FlushRun(buf);
    }

    void FlushRun(std::vector<std::vector<Value>>& buf) {
        std::sort(buf.begin(), buf.end(),
                  [this](const std::vector<Value>& a, const std::vector<Value>& b) {
                      return CompareRows(a, b, keys_) < 0;
                  });
        std::string fname = tmp_prefix_ + std::to_string(run_files_.size()) + ".bin";
        std::ofstream out(fname, std::ios::binary);
        const Schema& schema = child_->OutputSchema();
        for (const auto& r : buf) WriteRow(out, schema, r);
        run_files_.push_back(fname);
    }

    void Phase2_PrepareMerge() {
        readers_.reserve(run_files_.size());
        for (const auto& f : run_files_) {
            readers_.emplace_back(std::make_unique<std::ifstream>(f, std::ios::binary));
        }
        while (!heap_.empty()) heap_.pop();
        for (size_t i = 0; i < readers_.size(); ++i) {
            std::vector<Value> row;
            if (ReadRowFromRun(i, &row)) {
                heap_.push(HeapEntry{std::move(row), i, &keys_});
            }
        }
    }

    bool ReadRowFromRun(size_t run_idx, std::vector<Value>* out) {
        return ReadRow(*readers_[run_idx], child_->OutputSchema(), out);
    }

    static void WriteRow(std::ostream& out, const Schema& schema,
                         const std::vector<Value>& row) {
        for (int32_t i = 0; i < schema.NumColumns(); ++i) {
            const auto& col = schema.GetColumn(i);
            if (col.type == TypeId::INTEGER) {
                int64_t v = row[i].AsInt();
                out.write(reinterpret_cast<const char*>(&v), 8);
            } else {
                const std::string& s = row[i].AsVarchar();
                int32_t len = static_cast<int32_t>(s.size());
                out.write(reinterpret_cast<const char*>(&len), 4);
                out.write(s.data(), len);
            }
        }
    }

    static bool ReadRow(std::istream& in, const Schema& schema,
                        std::vector<Value>* out) {
        out->clear();
        out->reserve(schema.NumColumns());
        for (int32_t i = 0; i < schema.NumColumns(); ++i) {
            const auto& col = schema.GetColumn(i);
            if (col.type == TypeId::INTEGER) {
                int64_t v;
                if (!in.read(reinterpret_cast<char*>(&v), 8)) return false;
                out->push_back(Value::MakeInt(v));
            } else {
                int32_t len;
                if (!in.read(reinterpret_cast<char*>(&len), 4)) return false;
                std::string s(len, '\0');
                if (len > 0 && !in.read(s.data(), len)) return false;
                out->push_back(Value::MakeVarchar(std::move(s)));
            }
        }
        return true;
    }

    struct HeapEntry {
        std::vector<Value> row;
        size_t             run_idx;
        const std::vector<SortKey>* keys;

        // priority_queue is a MAX-heap; flip the compare to make it a MIN-heap.
        bool operator<(const HeapEntry& other) const {
            return CompareRows(row, other.row, *keys) > 0;
        }
    };

    std::unique_ptr<Executor>  child_;
    std::vector<SortKey>       keys_;
    size_t                     run_size_;
    std::string                tmp_prefix_;

    std::vector<std::string>                       run_files_;
    std::vector<std::unique_ptr<std::ifstream>>    readers_;
    std::priority_queue<HeapEntry>                 heap_;
};

}  // namespace minidb
