#pragma once

#include <memory>

#include "minidb/recovery/log_manager.h"

namespace minidb {

enum class TxnState { ACTIVE, COMMITTED, ABORTED };

class Transaction {
public:
    txn_id_t GetId()    const { return id_; }
    TxnState GetState() const { return state_; }

private:
    friend class TransactionManager;
    Transaction(txn_id_t id) : id_(id), state_(TxnState::ACTIVE) {}
    txn_id_t id_;
    TxnState state_;
};

class TransactionManager {
public:
    explicit TransactionManager(LogManager* log) : log_(log) {}

    std::unique_ptr<Transaction> Begin() {
        auto txn = std::unique_ptr<Transaction>(new Transaction(next_txn_id_++));
        log_->AppendBegin(txn->id_);
        return txn;
    }

    void Commit(Transaction* txn) {
        log_->AppendCommit(txn->id_);
        log_->Flush();
        txn->state_ = TxnState::COMMITTED;
    }

    void Abort(Transaction* txn) {
        log_->AppendAbort(txn->id_);
        log_->Flush();
        txn->state_ = TxnState::ABORTED;
    }

    txn_id_t GetNextTxnId() const { return next_txn_id_; }
    void SetNextTxnId(txn_id_t next) { next_txn_id_ = next; }

private:
    LogManager* log_;
    txn_id_t    next_txn_id_ = 1;
};

}  // namespace minidb
