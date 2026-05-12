#include <chrono>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include "minidb/catalog/catalog.h"
#include "minidb/execution/executor.h"
#include "minidb/recovery/log_manager.h"
#include "minidb/recovery/recovery.h"
#include "minidb/recovery/transaction.h"
#include "minidb/storage/buffer_pool_manager.h"
#include "minidb/storage/disk_manager.h"

using namespace minidb;

static int CountRows(Catalog& catalog, const std::string& table_name) {
    auto* tbl = catalog.GetTable(table_name);
    if (!tbl) return -1;
    SeqScanExecutor seq(tbl);
    seq.Init();
    std::vector<Value> row;
    int n = 0;
    while (seq.Next(&row)) ++n;
    return n;
}

static Schema MakeAccountSchema() {
    return Schema({
        {"id",   TypeId::INTEGER},
        {"name", TypeId::VARCHAR},
    });
}

int main() {
    std::cout << "===================================================" << std::endl;
    std::cout << "  WAL + Crash Recovery Demonstration" << std::endl;
    std::cout << "===================================================" << std::endl;

    std::cout << "\n--- Scenario 1: Committed transaction survives total data file loss ---" << std::endl;
    {
        std::remove("s1.log");
        std::remove("s1.db");

        {
            DiskManager dm("s1.db");
            BufferPoolManager bpm(100, &dm);
            Catalog catalog(&bpm);
            LogManager log("s1.log");
            TransactionManager txn_mgr(&log);

            catalog.CreateTable("accounts", MakeAccountSchema());

            auto txn = txn_mgr.Begin();
            for (int64_t i = 1; i <= 100; ++i) {
                catalog.InsertRowLogged("accounts",
                    {Value::MakeInt(i), Value::MakeVarchar("acct_" + std::to_string(i))},
                    txn.get(), &log);
            }
            txn_mgr.Commit(txn.get());
            std::cout << "Session 1: Inserted 100 rows, txn COMMITTED (log fsync'd to disk)." << std::endl;
            std::cout << "           Rows visible in memory: " << CountRows(catalog, "accounts") << std::endl;
        }

        std::cout << "  *** Simulated crash: data file destroyed, only log survives ***" << std::endl;
        std::remove("s1.db");

        {
            DiskManager dm("s1.db");
            BufferPoolManager bpm(100, &dm);
            Catalog catalog(&bpm);
            LogManager log("s1.log");
            TransactionManager txn_mgr(&log);

            catalog.CreateTable("accounts", MakeAccountSchema());

            auto stats = Recovery::Run("s1.log", &catalog, &txn_mgr);
            std::cout << "Session 2: Running recovery from log..." << std::endl;
            std::cout << "  records scanned: " << stats.records_scanned << std::endl;
            std::cout << "  txns committed:  " << stats.txns_committed << std::endl;
            std::cout << "  inserts redone:  " << stats.inserts_redone << std::endl;

            int n = CountRows(catalog, "accounts");
            std::cout << "Rows after recovery: " << n << "  (expected 100)  ["
                      << (n == 100 ? "PASS" : "FAIL") << "]" << std::endl;
        }
    }

    std::cout << "\n--- Scenario 2: Uncommitted transaction is correctly DROPPED ---" << std::endl;
    {
        std::remove("s2.log");
        std::remove("s2.db");

        {
            DiskManager dm("s2.db");
            BufferPoolManager bpm(100, &dm);
            Catalog catalog(&bpm);
            LogManager log("s2.log");
            TransactionManager txn_mgr(&log);

            catalog.CreateTable("accounts", MakeAccountSchema());

            auto txn1 = txn_mgr.Begin();
            for (int64_t i = 1; i <= 50; ++i) {
                catalog.InsertRowLogged("accounts",
                    {Value::MakeInt(i), Value::MakeVarchar("good_" + std::to_string(i))},
                    txn1.get(), &log);
            }
            txn_mgr.Commit(txn1.get());
            std::cout << "Session 1: txn1 inserted 50 'good' rows and COMMITTED." << std::endl;

            auto txn2 = txn_mgr.Begin();
            for (int64_t i = 51; i <= 80; ++i) {
                catalog.InsertRowLogged("accounts",
                    {Value::MakeInt(i), Value::MakeVarchar("dirty_" + std::to_string(i))},
                    txn2.get(), &log);
            }
            log.Flush();
            std::cout << "           txn2 inserted 30 'dirty' rows but NEVER committed (crash)." << std::endl;
            std::cout << "           Visible in memory before crash: " << CountRows(catalog, "accounts") << std::endl;
        }

        std::remove("s2.db");
        std::cout << "  *** Simulated crash: data file destroyed, log survives ***" << std::endl;

        {
            DiskManager dm("s2.db");
            BufferPoolManager bpm(100, &dm);
            Catalog catalog(&bpm);
            LogManager log("s2.log");
            TransactionManager txn_mgr(&log);

            catalog.CreateTable("accounts", MakeAccountSchema());

            auto stats = Recovery::Run("s2.log", &catalog, &txn_mgr);
            std::cout << "Session 2: Running recovery..." << std::endl;
            std::cout << "  records scanned:  " << stats.records_scanned << std::endl;
            std::cout << "  txns committed:   " << stats.txns_committed
                      << "  (txn1)" << std::endl;
            std::cout << "  txns uncommitted: " << stats.txns_uncommitted
                      << "  (txn2 - dirty inserts will be SKIPPED)" << std::endl;
            std::cout << "  inserts redone:   " << stats.inserts_redone << std::endl;
            std::cout << "  inserts skipped:  " << stats.inserts_skipped << std::endl;

            int n = CountRows(catalog, "accounts");
            std::cout << "Rows after recovery: " << n << "  (expected 50)  ["
                      << (n == 50 ? "PASS" : "FAIL") << "]" << std::endl;

            int dirty = 0;
            SeqScanExecutor seq(catalog.GetTable("accounts"));
            seq.Init();
            std::vector<Value> row;
            while (seq.Next(&row)) {
                if (row[1].AsVarchar().rfind("dirty_", 0) == 0) ++dirty;
            }
            std::cout << "Dirty rows present:  " << dirty << "  (expected 0)  ["
                      << (dirty == 0 ? "PASS" : "FAIL") << "]" << std::endl;
        }
    }

    std::cout << "\n--- Scenario 3: Group commit amortizes fsync cost ---" << std::endl;
    {
        std::remove("s3.log");
        std::remove("s3.db");

        DiskManager dm("s3.db");
        BufferPoolManager bpm(500, &dm);
        Catalog catalog(&bpm);
        LogManager log("s3.log");
        TransactionManager txn_mgr(&log);

        catalog.CreateTable("accounts", MakeAccountSchema());

        constexpr int BIG = 10000;
        auto t0 = std::chrono::high_resolution_clock::now();
        {
            auto txn = txn_mgr.Begin();
            for (int64_t i = 1; i <= BIG; ++i) {
                catalog.InsertRowLogged("accounts",
                    {Value::MakeInt(i), Value::MakeVarchar("x")},
                    txn.get(), &log);
            }
            txn_mgr.Commit(txn.get());
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        auto big_us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

        constexpr int SMALL = 500;
        auto t2 = std::chrono::high_resolution_clock::now();
        for (int64_t i = BIG + 1; i <= BIG + SMALL; ++i) {
            auto txn = txn_mgr.Begin();
            catalog.InsertRowLogged("accounts",
                {Value::MakeInt(i), Value::MakeVarchar("y")},
                txn.get(), &log);
            txn_mgr.Commit(txn.get());
        }
        auto t3 = std::chrono::high_resolution_clock::now();
        auto small_us = std::chrono::duration_cast<std::chrono::microseconds>(t3 - t2).count();

        double big_per_insert   = static_cast<double>(big_us) / BIG;
        double small_per_insert = static_cast<double>(small_us) / SMALL;
        std::cout << BIG   << " inserts, 1 commit:     " << big_us   << " us  ("
                  << big_per_insert   << " us/insert,   1 fsync)" << std::endl;
        std::cout << SMALL << " inserts, 1 commit ea.: " << small_us << " us  ("
                  << small_per_insert << " us/insert, " << SMALL << " fsyncs)" << std::endl;
        std::cout << "Per-insert speedup from batching: "
                  << (small_per_insert / big_per_insert) << "x" << std::endl;
        std::cout << "(This is why every OLTP database supports group commit.)" << std::endl;
    }

    std::cout << "\nDone." << std::endl;
    return 0;
}
