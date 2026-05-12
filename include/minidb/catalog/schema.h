#pragma once

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "minidb/storage/tuple.h"

namespace minidb {

enum class TypeId : int32_t {
    INTEGER = 1,
    VARCHAR = 2
};

struct Column {
    std::string name;
    TypeId      type;
};

class Schema {
public:
    Schema() = default;
    explicit Schema(std::vector<Column> cols) : columns_(std::move(cols)) {}

    int32_t NumColumns() const { return static_cast<int32_t>(columns_.size()); }
    const Column& GetColumn(int32_t i) const { return columns_[i]; }

    int32_t IndexOf(const std::string& name) const {
        for (int32_t i = 0; i < NumColumns(); ++i) {
            if (columns_[i].name == name) return i;
        }
        return -1;
    }

private:
    std::vector<Column> columns_;
};

class Value {
public:
    Value() : type_(TypeId::INTEGER), int_(0) {}
    static Value MakeInt(int64_t v)             { Value x; x.type_ = TypeId::INTEGER; x.int_ = v; return x; }
    static Value MakeVarchar(std::string v)     { Value x; x.type_ = TypeId::VARCHAR; x.str_ = std::move(v); return x; }

    TypeId GetType() const { return type_; }
    int64_t AsInt() const  { return int_; }
    const std::string& AsVarchar() const { return str_; }

    bool Equals(const Value& other) const {
        if (type_ != other.type_) return false;
        return type_ == TypeId::INTEGER ? int_ == other.int_ : str_ == other.str_;
    }

    std::string ToString() const {
        return type_ == TypeId::INTEGER ? std::to_string(int_) : str_;
    }

private:
    TypeId type_;
    int64_t int_ = 0;
    std::string str_;
};

// Encode/decode a typed row <-> binary tuple bytes.
// INTEGER -> 8 bytes; VARCHAR -> [4-byte length][payload].
class TupleCodec {
public:
    static Tuple Encode(const Schema& schema, const std::vector<Value>& row) {
        if (static_cast<int32_t>(row.size()) != schema.NumColumns()) {
            throw std::runtime_error("TupleCodec::Encode: column count mismatch");
        }
        std::vector<char> buf;
        for (int32_t i = 0; i < schema.NumColumns(); ++i) {
            const auto& col = schema.GetColumn(i);
            if (col.type != row[i].GetType()) {
                throw std::runtime_error("TupleCodec::Encode: type mismatch for col " + col.name);
            }
            if (col.type == TypeId::INTEGER) {
                int64_t v = row[i].AsInt();
                const char* p = reinterpret_cast<const char*>(&v);
                buf.insert(buf.end(), p, p + 8);
            } else {
                const std::string& s = row[i].AsVarchar();
                int32_t len = static_cast<int32_t>(s.size());
                const char* lp = reinterpret_cast<const char*>(&len);
                buf.insert(buf.end(), lp, lp + 4);
                buf.insert(buf.end(), s.begin(), s.end());
            }
        }
        return Tuple(buf.data(), buf.size());
    }

    static std::vector<Value> Decode(const Schema& schema, const Tuple& tuple) {
        std::vector<Value> out;
        out.reserve(schema.NumColumns());
        const char* p = tuple.GetData();
        const char* end = p + tuple.GetSize();
        for (int32_t i = 0; i < schema.NumColumns(); ++i) {
            const auto& col = schema.GetColumn(i);
            if (col.type == TypeId::INTEGER) {
                if (p + 8 > end) throw std::runtime_error("TupleCodec::Decode: short read int");
                int64_t v; std::memcpy(&v, p, 8); p += 8;
                out.push_back(Value::MakeInt(v));
            } else {
                if (p + 4 > end) throw std::runtime_error("TupleCodec::Decode: short read len");
                int32_t len; std::memcpy(&len, p, 4); p += 4;
                if (p + len > end) throw std::runtime_error("TupleCodec::Decode: short read str");
                out.push_back(Value::MakeVarchar(std::string(p, p + len)));
                p += len;
            }
        }
        return out;
    }

    static Value DecodeColumn(const Schema& schema, const Tuple& tuple, int32_t col_idx) {
        const char* p = tuple.GetData();
        const char* end = p + tuple.GetSize();
        for (int32_t i = 0; i <= col_idx; ++i) {
            const auto& col = schema.GetColumn(i);
            if (col.type == TypeId::INTEGER) {
                if (p + 8 > end) throw std::runtime_error("DecodeColumn: short read");
                if (i == col_idx) {
                    int64_t v; std::memcpy(&v, p, 8);
                    return Value::MakeInt(v);
                }
                p += 8;
            } else {
                if (p + 4 > end) throw std::runtime_error("DecodeColumn: short read");
                int32_t len; std::memcpy(&len, p, 4); p += 4;
                if (p + len > end) throw std::runtime_error("DecodeColumn: short read");
                if (i == col_idx) {
                    return Value::MakeVarchar(std::string(p, p + len));
                }
                p += len;
            }
        }
        throw std::runtime_error("DecodeColumn: col_idx out of range");
    }
};

}  // namespace minidb
