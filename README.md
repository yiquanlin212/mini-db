# MiniDB

A from-scratch OLTP database engine in C++20. Implements the full stack of a transactional storage system: 4KB paged storage, LRU buffer pool, slotted-page table format, persistent B+ tree index, Volcano-model query execution with sort-merge join, and write-ahead logging with crash recovery.

Architecture and algorithms follow established systems (PostgreSQL, SQLite, CMU BusTub). Built as a self-directed deep dive into production-grade database internals.

## Performance highlights

All numbers measured on an Apple Silicon MacBook Pro, `-O3` Release build:

- **4.35M inserts/sec** end-to-end (typed encoding → tuple → heap → index maintenance)
- **43,932× speedup** of B+ tree IndexScan over SeqScan + Filter on a 100K-row table
- **2.32M joined rows/sec** on a 200K × 50K sort-merge join (86ms total)
- **0.265 μs** average point lookup latency on a 100K-row B+ tree
- **Crash-safe**: recovers 100% of committed transactions after total data-file loss; correctly drops uncommitted dirty writes
- **8.5× speedup** from group commit (fsync amortization) — same effect observed in every production OLTP system

## Architecture

```
+---------------------------------------------------+
|                Query Execution                    |
|     Volcano iterator model: Init() / Next()       |
| SeqScan | IndexScan | Filter | Sort | MergeJoin   |
+---------------------------------------------------+
|                Catalog & Schema                   |
|  TableInfo | IndexInfo | TupleCodec (typed rows)  |
+---------------------------------------------------+
|         Access Methods    |    Recovery           |
| TableHeap (slotted pages) | WAL + redo recovery   |
|     B+ Tree index         | Transaction manager   |
+---------------------------------------------------+
|                 Buffer Pool                       |
|     LRU eviction | pin/unpin | dirty tracking     |
+---------------------------------------------------+
|             Disk Manager (POSIX)                  |
|    pread / pwrite at page-aligned offsets         |
|              fsync for durability                 |
+---------------------------------------------------+
```

## Features

### Storage layer
- 4KB fixed-size pages, aligned with the OS page size to avoid torn writes
- POSIX `pread` / `pwrite` for true random-access I/O at page-aligned offsets
- LRU buffer pool with pin/unpin reference counting, O(1) cache operations via linked list plus hashmap, automatic flush of dirty pages on eviction

### Tuple and table storage
- Slotted-page layout: header plus slot directory grows forward, tuples grow backward into the same page (same design as Postgres, SQLite, MySQL InnoDB)
- Variable-length tuples via length-prefixed binary encoding
- Pages chained into a linked list; tables grow arbitrarily large
- Range-based C++ iterator over a full table heap

### B+ Tree index
- Disk-backed, persistent across restarts
- Recursive insert with split propagation up to root
- Correct asymmetric separator handling: leaf split copies the separator up, internal split pushes the middle key up (textbook B+ tree distinction)
- Leaves linked into a forward list to support range scans
- Verified at 1000-key stress level across random, sequential, and reverse-sequential workloads

### Query execution
- Volcano (iterator) execution model — the architecture used by PostgreSQL
- Composable executors: `SeqScan`, `IndexScan`, `Filter`, `ExternalMergeSort`, `SortMergeJoin`
- Expression tree with column references, constants, comparisons, AND / OR
- Plans assembled as `unique_ptr`-owned child trees

### External merge sort
- Two-phase algorithm: in-memory sort of fixed-size runs (spilled to disk) followed by K-way merge via min-heap
- Sorts datasets exceeding memory; the same algorithm Postgres uses for `ORDER BY` and sort-based aggregation

### Sort-merge join
- Inner join with correct many-to-many duplicate-key handling via right-side buffering
- Output is naturally ordered by the join key (useful for pipelined `ORDER BY`)

### Write-Ahead Log and Crash Recovery
- Binary log records: `BEGIN`, `INSERT`, `COMMIT`, `ABORT`, each with monotonic LSN
- Force-log-on-commit: the log up to and including the COMMIT record is `fsync`'d before commit returns
- Two-pass recovery: analysis pass collects committed transactions, redo pass replays only their inserts
- Demonstrated recovery of 100 committed inserts after the data file is deleted (durability)
- Demonstrated correct dropping of 30 uncommitted inserts after simulated crash (isolation)

## Performance

Measured on Apple Silicon MacBook Pro, `-O3` Release build:

| Workload | Result |
|---|---|
| Bulk insert with index maintenance | 4.35M ops/sec |
| Point lookup via B+ tree | 0.265 μs/op |
| SeqScan + Filter (100K rows, single match) | 11.6 ms |
| **IndexScan vs SeqScan + Filter speedup** | **43,932×** |
| Build B+ tree index on 100K keys | 48 ms |
| Sort-merge join 200K × 50K | 86 ms (2.32M rows/sec) |
| Group-committed inserts (10K per commit) | 2.7 μs / insert |
| One-commit-per-insert | 23.1 μs / insert |
| **Group commit speedup (fsync amortization)** | **8.5×** |
| WAL recovery of 100 committed inserts | < 10 ms |

## Build

Requires CMake 3.20+, Ninja, and a C++20 compiler (tested with Apple Clang 21).

```
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/mini_db
```

## Project layout

```
mini-db/
├── include/minidb/
│   ├── storage/        # Page, DiskManager, BufferPool, TableHeap
│   ├── index/          # B+ tree (leaf, internal, recursive ops)
│   ├── catalog/        # Schema, Catalog, TupleCodec, Value
│   ├── execution/      # Volcano executors, expressions
│   └── recovery/       # LogRecord, LogManager, Transaction, Recovery
├── src/                # implementations matching the headers above
├── CMakeLists.txt
└── README.md
```

## Design Decisions

**Why slotted pages over fixed-size records?** Real tables have variable-length columns. Reverse-growing tuples enable O(1) insert without moving prior tuples. This is the same design used by Postgres, SQLite, MySQL InnoDB, and Oracle.

**Why B+ tree over hash or LSM?** B+ tree supports range scans and `ORDER BY` in addition to point lookups. Hash indexes can't. LSM is better for write-heavy workloads but trades read latency. B+ tree is the OLTP industry standard.

**Why Volcano model over vectorized execution?** Volcano composes operators cleanly via a uniform `Init` / `Next` interface. Vectorized execution (DuckDB, ClickHouse) is faster for analytical queries but more invasive on the codebase; Volcano is the right choice for an OLTP-style system.

**Why Sort-Merge Join (not Hash Join)?** SMJ produces sorted output for free, which is useful when the next operator expects ordering. Hash Join is faster when one side fits in memory; a real optimizer would choose between them based on statistics. MiniDB implements SMJ as the more general algorithm.

**Why redo-only recovery, not full ARIES?** ARIES adds undo via compensation log records to support the STEAL buffer pool policy (uncommitted dirty pages may be written to disk). MiniDB's recovery test demonstrates durability by rebuilding from WAL after a simulated total data-file loss; this is sufficient to prove the WAL contract. Full ARIES with undo is planned future work.

**Why force-log-on-commit?** Without `fsync`, a crash within ~30 seconds of commit can lose acknowledged transactions due to OS page cache. The included benchmark measures the cost (~23 μs per commit on this hardware) and shows why every OLTP system implements group commit.

## Limitations / Future Work

- Single-threaded execution; no latches, no MVCC
- INSERT only — no UPDATE or DELETE yet
- No SQL parser; queries are built as C++ executor trees
- No cost-based optimizer; plans are manually constructed
- No checkpointing — recovery replays the entire log
- Type system limited to INTEGER and VARCHAR

## License

MIT
