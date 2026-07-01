#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

#include "common/config.h"
#include "common/macros.h"
#include "storage/table/tuple.h"

namespace bustub {

/**
 * Page to hold the intermediate data for external merge sort and hash join.
 * Supports variable-length tuples.
 */
class IntermediateResultPage {
 public:
  /**
   * TODO(P3): Define and implement the methods for reading data from and writing data to the sort
   * page. Feel free to add other helper methods.
   */
  static constexpr uint32_t HEADER_SIZE = 8;
  static constexpr uint32_t SLOT_SIZE = 8;

  void Init() {
    SetTupleCount(0);
    SetFreeSpaceOffset(BUSTUB_PAGE_SIZE);
  }

  auto AppendTuple(const Tuple &tuple) -> bool {
    const auto tuple_size = tuple.GetLength() + sizeof(uint32_t);
    const auto tuple_count = GetTupleCount();
    const auto slot_offset = HEADER_SIZE + tuple_count * SLOT_SIZE;
    const auto free_space_offset = GetFreeSpaceOffset();

    if (slot_offset + SLOT_SIZE > free_space_offset) {
      return false;
    }

    if (tuple_size > free_space_offset - slot_offset - SLOT_SIZE) {
      return false;
    }

    const auto new_tuple_offset = free_space_offset - tuple_size;
    tuple.SerializeTo(GetDataMut() + new_tuple_offset);
    SetSlot(tuple_count, new_tuple_offset, tuple_size);
    SetTupleCount(tuple_count + 1);
    SetFreeSpaceOffset(new_tuple_offset);
    return true;
  }

  auto GetTuple(uint32_t tuple_idx) const -> Tuple {
    BUSTUB_ENSURE(tuple_idx < GetTupleCount(), "tuple index out of range");
    const auto [offset, size] = GetSlot(tuple_idx);
    Tuple tuple;
    tuple.DeserializeFrom(GetData() + offset);
    return tuple;
  }

  auto GetTupleCount() const -> uint32_t { return ReadUint32(0); }

 private:
  /**
   * TODO(P3): Define the private members. You may want to have some necessary metadata for
   * the sort page before the start of the actual data.
   */
  auto GetData() const -> const char * { return reinterpret_cast<const char *>(this); }

  auto GetDataMut() -> char * { return reinterpret_cast<char *>(this); }

  auto GetFreeSpaceOffset() const -> uint32_t { return ReadUint32(4); }

  void SetTupleCount(uint32_t tuple_count) { WriteUint32(0, tuple_count); }

  void SetFreeSpaceOffset(uint32_t free_space_offset) { WriteUint32(4, free_space_offset); }

  auto GetSlot(uint32_t slot_idx) const -> std::pair<uint32_t, uint32_t> {
    const auto slot_offset = HEADER_SIZE + slot_idx * SLOT_SIZE;
    return {ReadUint32(slot_offset), ReadUint32(slot_offset + sizeof(uint32_t))};
  }

  void SetSlot(uint32_t slot_idx, uint32_t tuple_offset, uint32_t tuple_size) {
    const auto slot_offset = HEADER_SIZE + slot_idx * SLOT_SIZE;
    WriteUint32(slot_offset, tuple_offset);
    WriteUint32(slot_offset + sizeof(uint32_t), tuple_size);
  }

  auto ReadUint32(uint32_t offset) const -> uint32_t {
    uint32_t value;
    std::memcpy(&value, GetData() + offset, sizeof(uint32_t));
    return value;
  }

  void WriteUint32(uint32_t offset, uint32_t value) { std::memcpy(GetDataMut() + offset, &value, sizeof(uint32_t)); }
};

}  // namespace bustub
