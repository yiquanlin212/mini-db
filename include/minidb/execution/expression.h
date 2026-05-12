#pragma once

#include <memory>
#include <string>
#include <vector>

#include "minidb/catalog/schema.h"

namespace minidb {

class Expression {
public:
    virtual ~Expression() = default;
    virtual Value Evaluate(const std::vector<Value>& row) const = 0;
};

using ExprPtr = std::shared_ptr<Expression>;

class ColumnExpr : public Expression {
public:
    explicit ColumnExpr(int32_t col_idx) : col_idx_(col_idx) {}
    Value Evaluate(const std::vector<Value>& row) const override { return row[col_idx_]; }
private:
    int32_t col_idx_;
};

class ConstantExpr : public Expression {
public:
    explicit ConstantExpr(Value v) : v_(std::move(v)) {}
    Value Evaluate(const std::vector<Value>&) const override { return v_; }
private:
    Value v_;
};

enum class CmpOp { EQ, NE, LT, LE, GT, GE };

class CompareExpr : public Expression {
public:
    CompareExpr(ExprPtr l, CmpOp op, ExprPtr r) : l_(std::move(l)), r_(std::move(r)), op_(op) {}
    Value Evaluate(const std::vector<Value>& row) const override {
        Value a = l_->Evaluate(row);
        Value b = r_->Evaluate(row);
        if (a.GetType() == TypeId::INTEGER && b.GetType() == TypeId::INTEGER) {
            int64_t x = a.AsInt(), y = b.AsInt();
            bool ok = false;
            switch (op_) {
                case CmpOp::EQ: ok = x == y; break;
                case CmpOp::NE: ok = x != y; break;
                case CmpOp::LT: ok = x <  y; break;
                case CmpOp::LE: ok = x <= y; break;
                case CmpOp::GT: ok = x >  y; break;
                case CmpOp::GE: ok = x >= y; break;
            }
            return Value::MakeInt(ok ? 1 : 0);
        }
        const std::string& sa = a.AsVarchar();
        const std::string& sb = b.AsVarchar();
        bool ok = (op_ == CmpOp::EQ) ? sa == sb : sa != sb;
        return Value::MakeInt(ok ? 1 : 0);
    }
private:
    ExprPtr l_, r_;
    CmpOp   op_;
};

enum class LogicOp { AND, OR };

class LogicExpr : public Expression {
public:
    LogicExpr(ExprPtr l, LogicOp op, ExprPtr r) : l_(std::move(l)), r_(std::move(r)), op_(op) {}
    Value Evaluate(const std::vector<Value>& row) const override {
        bool a = l_->Evaluate(row).AsInt() != 0;
        bool b = r_->Evaluate(row).AsInt() != 0;
        bool ok = (op_ == LogicOp::AND) ? (a && b) : (a || b);
        return Value::MakeInt(ok ? 1 : 0);
    }
private:
    ExprPtr l_, r_;
    LogicOp op_;
};

inline ExprPtr Col(int32_t i)               { return std::make_shared<ColumnExpr>(i); }
inline ExprPtr ConstInt(int64_t v)          { return std::make_shared<ConstantExpr>(Value::MakeInt(v)); }
inline ExprPtr ConstStr(std::string v)      { return std::make_shared<ConstantExpr>(Value::MakeVarchar(std::move(v))); }
inline ExprPtr Cmp(ExprPtr l, CmpOp op, ExprPtr r) { return std::make_shared<CompareExpr>(l, op, r); }
inline ExprPtr And_(ExprPtr l, ExprPtr r)   { return std::make_shared<LogicExpr>(l, LogicOp::AND, r); }
inline ExprPtr Or_(ExprPtr l, ExprPtr r)    { return std::make_shared<LogicExpr>(l, LogicOp::OR, r); }

}  // namespace minidb
