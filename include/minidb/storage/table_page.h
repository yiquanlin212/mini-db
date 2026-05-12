#pragma once

#include <cstdint>

#include "minidb/storage/page.h"
#include "minidb/storage/tuple.h"

namespace minidb {

// Slotted page layout (with chain link for TableHeap):
//
// +---------+----------+----------+--------+----------+--------+
// | Header  | slot[0]  | slot[1]  |  FREE  |  tuple_1 | tuple_0|
// |  12 B   |  8 B     |  8 B     | SPACE  |          |        |
// +---------+----------+----------+--------+----------+--------+
//
// Header (12 bytes):
//   [num_tuples : int32][free_offset : int32][next_page_id : int32]

constexpr int32_t PAGE_HEADER_SIZE = 12;
constexpr int32_t SLOT_SIZE = 8;

class TablePage {
public:
    static void Init(char* page_data);
    static int32_t GetNumTuples(const char* page_data);
    static int32_t GetFreeSpace(const char* page_data);

    static page_id_t GetNextPageId(const char* page_data);
    static void SetNextPageId(char* page_data, page_id_t next_page_id);

    static bool InsertTuple(char* page_data, const Tuple& tuple, int32_t* slot_id);
    static bool GetTuple(const char* page_data, int32_t slot_id, Tuple* tuple);
};

}  // namespace minidb
