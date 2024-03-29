//===----------------------------------------------------------------------===//
//
//                         CMU-DB Project (15-445/645)
//                         ***DO NO SHARE PUBLICLY***
//
// Identification: src/include/page/b_plus_tree_leaf_page.h
//
// Copyright (c) 2018, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//
#pragma once

#include <utility>
#include <vector>

#include "buffer/buffer_pool_manager.h"
#include "common/config.h"
#include "storage/page/b_plus_tree_page.h"

namespace bustub {

#define B_PLUS_TREE_LEAF_PAGE_TYPE BPlusTreeLeafPage<KeyType, ValueType, KeyComparator>
#define LEAF_PAGE_HEADER_SIZE 29
#define LEAF_PAGE_SIZE ((BUSTUB_PAGE_SIZE - LEAF_PAGE_HEADER_SIZE) / sizeof(MappingType))

/**
 * Store indexed key and record id(record id = page id combined with slot id,
 * see include/common/rid.h for detailed implementation) together within leaf
 * page. Only support unique key.
 *
 * Leaf page format (keys are stored in order):
 *  ----------------------------------------------------------------------
 * | HEADER | KEY(1) + RID(1) | KEY(2) + RID(2) | ... | KEY(n) + RID(n)
 *  ----------------------------------------------------------------------
 *
 *  Header format (size in byte, 28 bytes in total):
 *  ---------------------------------------------------------------------
 * | PageType (4) | LSN (4) | CurrentSize (4) | MaxSize (4) |
 *  ---------------------------------------------------------------------
 *  -----------------------------------------------
 * | ParentPageId (4) | PageId (4) | NextPageId (4)
 *  -----------------------------------------------
 */
INDEX_TEMPLATE_ARGUMENTS
class BPlusTreeLeafPage : public BPlusTreePage {
  using LeafPage = BPlusTreeLeafPage<KeyType, ValueType, KeyComparator>;

 public:
  // After creating a new leaf page from buffer pool, must call initialize
  // method to set default values
  void Init(page_id_t page_id, page_id_t parent_id = INVALID_PAGE_ID, int max_size = LEAF_PAGE_SIZE);
  // helper methods
  auto GetNextPageId() const -> page_id_t;
  void SetNextPageId(page_id_t next_page_id);
  auto GetPrevPageId() const -> page_id_t;
  void SetPrevPageId(page_id_t prev_page_id);
  auto KeyAt(int index) const -> KeyType;
  auto ValueAt(int index) const -> ValueType;
  auto MappingAt(int index) const -> const MappingType &;
  auto Search(const KeyType &key, const KeyComparator &comparator) const -> int;
  auto Insert(const KeyType &key, const ValueType &value, const KeyComparator &comparator) -> bool;
  void InsertFirst(const MappingType *value);
  void InsertLast(const MappingType *value);
  auto Remove(const KeyType &key, const KeyComparator &comparator) -> bool;
  auto StealFirst(MappingType *value) -> bool;
  auto StealLast(MappingType *value) -> bool;
  void MergeFromLeft(LeafPage *rest_leaf);
  void MergeFromRight(LeafPage *merging_leaf);
  inline auto IsLast() -> bool { return next_page_id_ == INVALID_PAGE_ID; }
  inline auto GetArray() -> MappingType * { return array_; }
  inline auto NeedRedsb() -> bool { return GetSize() < GetMaxSize() / 2; }

 private:
  page_id_t next_page_id_;
  // Flexible array member for page data.
  MappingType array_[1];
};
}  // namespace bustub
