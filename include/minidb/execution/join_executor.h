#pragma once

#include <memory>
#include <vector>

#include "minidb/catalog/schema.h"
#include "minidb/execution/executor.h"

namespace minidb {

// Sort-Merge Inner Join on a single integer key.
// REQUIRES inputs to be pre-sorted by their join keys.
// Handles many-to-many duplicates correctly.
class SortMergeJoinExecutor : public Executor {
public:
    SortMergeJoinExecutor(std::unique_ptr<Executor> left,
                          std::unique_ptr<Executor> right,
                          int32_t left_key_col,
                          int32_t right_key_col)
        : left_(std::move(left)),
          right_(std::move(right)),
          left_key_col_(left_key_col),
          right_key_col_(right_key_col) {
        std::vector<Column> cols;
        const auto& ls = left_->OutputSchema();
        const auto& rs = right_->OutputSchema();
        for (int32_t i = 0; i < ls.NumColumns(); ++i) cols.push_back(ls.GetColumn(i));
        for (int32_t i = 0; i < rs.NumColumns(); ++i) {
            Column c = rs.GetColumn(i);
            c.name = "r_" + c.name;
            cols.push_back(c);
        }
        output_schema_ = Schema(std::move(cols));
    }

    void Init() override {
        left_->Init();
        right_->Init();
        left_has_ = left_->Next(&left_row_);
        right_has_ = right_->Next(&right_row_);
        right_buffer_.clear();
        emit_left_.clear();
        emit_right_idx_ = 0;
        in_match_block_ = false;
    }

    bool Next(std::vector<Value>* out_row) override {
        // Continue an in-progress match block
        if (in_match_block_) {
            if (emit_right_idx_ < right_buffer_.size()) {
                *out_row = Concat(emit_left_, right_buffer_[emit_right_idx_++]);
                return true;
            }
            // Done emitting current left row against buffer; try next left row
            left_has_ = left_->Next(&left_row_);
            if (left_has_ && left_row_[left_key_col_].AsInt() == block_key_) {
                emit_left_ = left_row_;
                emit_right_idx_ = 0;
                *out_row = Concat(emit_left_, right_buffer_[emit_right_idx_++]);
                return true;
            }
            in_match_block_ = false;
            right_buffer_.clear();
        }

        // Main merge phase
        while (left_has_ && right_has_) {
            int64_t lk = left_row_[left_key_col_].AsInt();
            int64_t rk = right_row_[right_key_col_].AsInt();
            if (lk < rk) {
                left_has_ = left_->Next(&left_row_);
            } else if (lk > rk) {
                right_has_ = right_->Next(&right_row_);
            } else {
                // Match. Buffer all right rows sharing this key.
                block_key_ = rk;
                right_buffer_.clear();
                while (right_has_ && right_row_[right_key_col_].AsInt() == block_key_) {
                    right_buffer_.push_back(std::move(right_row_));
                    right_has_ = right_->Next(&right_row_);
                }
                emit_left_ = left_row_;
                emit_right_idx_ = 0;
                in_match_block_ = true;
                *out_row = Concat(emit_left_, right_buffer_[emit_right_idx_++]);
                return true;
            }
        }
        return false;
    }

    const Schema& OutputSchema() const override { return output_schema_; }

private:
    static std::vector<Value> Concat(const std::vector<Value>& a,
                                     const std::vector<Value>& b) {
        std::vector<Value> out;
        out.reserve(a.size() + b.size());
        out.insert(out.end(), a.begin(), a.end());
        out.insert(out.end(), b.begin(), b.end());
        return out;
    }

    std::unique_ptr<Executor> left_;
    std::unique_ptr<Executor> right_;
    int32_t                   left_key_col_;
    int32_t                   right_key_col_;
    Schema                    output_schema_;

    std::vector<Value> left_row_;
    std::vector<Value> right_row_;
    bool               left_has_ = false;
    bool               right_has_ = false;

    bool                            in_match_block_ = false;
    int64_t                         block_key_ = 0;
    std::vector<std::vector<Value>> right_buffer_;
    std::vector<Value>              emit_left_;
    size_t                          emit_right_idx_ = 0;
};

}  // namespace minidb
