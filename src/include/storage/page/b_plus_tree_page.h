//===----------------------------------------------------------------------===//
//
//                         CMU-DB Project (15-445/645)
//                         ***DO NO SHARE PUBLICLY***
//
// Identification: src/include/page/b_plus_tree_page.h
//
// Copyright (c) 2018, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//
#pragma once

#include <cassert>
#include <climits>
#include <cstdlib>
#include <string>

#include "buffer/buffer_pool_manager.h"
// #include "storage/index/b_plus_tree.h"
#include "concurrency/transaction.h"
#include "storage/index/generic_key.h"

namespace bustub {

#define MappingType std::pair<KeyType, ValueType>

/**
 * Types
 */
enum class RWType { READ = 0, WRITE, UPDATE };
enum class OpType { INSERT = 0, REMOVE, READ, ITER };

#define INDEX_TEMPLATE_ARGUMENTS template <typename KeyType, typename ValueType, typename KeyComparator>

// define page type enum
enum class IndexPageType { INVALID_INDEX_PAGE = 0, LEAF_PAGE, INTERNAL_PAGE };

/**
 * Both internal and leaf page are inherited from this page.
 *
 * It actually serves as a header part for each B+ tree page and
 * contains information shared by both leaf page and internal page.
 *
 * Header format (size in byte, 24 bytes in total):
 * ----------------------------------------------------------------------------
 * | PageType (4) | LSN (4) | CurrentSize (4) | MaxSize (4) |
 * ----------------------------------------------------------------------------
 * | ParentPageId (4) | PageId(4) |
 * ----------------------------------------------------------------------------
 */
class BPlusTreePage {
 public:
  auto IsLeafPage() const -> bool;
  auto IsRootPage() const -> bool;
  void SetPageType(IndexPageType page_type);

  auto GetSize() const -> int;
  void SetSize(int size);
  void IncreaseSize(int amount);

  auto GetMaxSize() const -> int;
  void SetMaxSize(int max_size);
  auto GetMinSize() const -> int;

  auto GetParentPageId() const -> page_id_t;
  void SetParentPageId(page_id_t parent_page_id);

  auto GetPageId() const -> page_id_t;
  void SetPageId(page_id_t page_id);

  inline auto GetBelongPage() const -> Page * { return page_; }
  inline void SetBelongPage(Page *page) { page_ = page; }

  void SetLSN(lsn_t lsn = INVALID_LSN);
  inline auto NeedSplit() -> bool { return size_ == max_size_; };

  auto IsSafe(OpType op) -> bool;
  inline auto IsCurRoot() -> bool { return is_cur_root_; };
  inline void SetIsCurRoot(bool is_cur_root) { is_cur_root_ = is_cur_root; }
  // inline auto IsDeleted() -> bool { return is_deleted_; };
  // inline void SetIsDeleted(bool is_deleted) { is_deleted_ = is_deleted; }

 private:
  // member variable, attributes that both internal and leaf page share
  IndexPageType page_type_ __attribute__((__unused__));
  lsn_t lsn_ __attribute__((__unused__));
  int size_ __attribute__((__unused__));
  int max_size_ __attribute__((__unused__));
  page_id_t parent_page_id_ __attribute__((__unused__));
  page_id_t page_id_ __attribute__((__unused__));
  Page *page_;
  bool is_cur_root_ = false;
  // bool is_deleted_ = false;
};

}  // namespace bustub
