//===----------------------------------------------------------------------===//
//
//                         CMU-DB Project (15-445/645)
//                         ***DO NO SHARE PUBLICLY***
//
// Identification: src/include/index/b_plus_tree.h
//
// Copyright (c) 2018, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//
#pragma once

#include <queue>
#include <string>
#include <vector>

#include "common/config.h"
#include "concurrency/transaction.h"
#include "storage/index/index_iterator.h"
#include "storage/page/b_plus_tree_internal_page.h"
#include "storage/page/b_plus_tree_leaf_page.h"
#include "storage/page/b_plus_tree_page.h"

namespace bustub {

#define BPLUSTREE_TYPE BPlusTree<KeyType, ValueType, KeyComparator>

// enum class RWType { READ = 0, WRITE, UPDATE };
// enum class OpType { INSERT = 0, REMOVE, READ };

/**
 * Main class providing the API for the Interactive B+ Tree.
 *
 * Implementation of simple b+ tree data structure where internal pages direct
 * the search and leaf pages contain actual data.
 * (1) We only support unique key
 * (2) support insert & remove
 * (3) The structure should shrink and grow dynamically
 * (4) Implement index iterator for range scan
 */
INDEX_TEMPLATE_ARGUMENTS
class BPlusTree {
  using InternalPage = BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator>;
  using LeafPage = BPlusTreeLeafPage<KeyType, ValueType, KeyComparator>;

 public:
  explicit BPlusTree(std::string name, BufferPoolManager *buffer_pool_manager, const KeyComparator &comparator,
                     int leaf_max_size = LEAF_PAGE_SIZE, int internal_max_size = INTERNAL_PAGE_SIZE);

  // Returns true if this B+ tree has no keys and values.
  auto IsEmpty() const -> bool;

  // Insert a key-value pair into this B+ tree.
  auto Insert(const KeyType &key, const ValueType &value, Transaction *transaction = nullptr) -> bool;

  // Remove a key and its value from this B+ tree.
  void Remove(const KeyType &key, Transaction *transaction = nullptr);

  // return the value associated with a given key
  auto GetValue(const KeyType &key, std::vector<ValueType> *result, Transaction *transaction = nullptr) -> bool;

  // return the page id of the root node
  auto GetRootPageId() -> page_id_t;

  // index iterator
  auto Begin() -> INDEXITERATOR_TYPE;
  auto Begin(const KeyType &key) -> INDEXITERATOR_TYPE;
  auto End() -> INDEXITERATOR_TYPE;

  // print the B+ tree
  void Print(BufferPoolManager *bpm);

  // draw the B+ tree
  void Draw(BufferPoolManager *bpm, const std::string &outf);

  // read data from file and insert one by one
  void InsertFromFile(const std::string &file_name, Transaction *transaction = nullptr);

  // read data from file and remove one by one
  void RemoveFromFile(const std::string &file_name, Transaction *transaction = nullptr);

 private:
  void UpdateRootPageId(int insert_record = 0);

  /* Debug Routines for FREE!! */
  void ToGraph(BPlusTreePage *page, BufferPoolManager *bpm, std::ofstream &out) const;

  void ToString(BPlusTreePage *page, BufferPoolManager *bpm) const;

  auto FindLeafPage(const KeyType &key, bool left_most, OpType op, Transaction *transaction) -> LeafPage *;
  auto CrabingPage(page_id_t page_id, page_id_t previous, OpType op, Transaction *transaction) -> BPlusTreePage *;
  void FreePage(page_id_t cur_id, RWType rw, Transaction *transaction);

  void SplitLeaf(LeafPage *over_node, Transaction *transaction);

  void SplitInternal(InternalPage *over_node, Transaction *transaction);

  void MergeLeaf(LeafPage *rest_leaf, InternalPage *parent_internal, LeafPage *merging_leaf, int target_index,
                 int neber_index, bool is_last, Transaction *transaction);

  void RedsbInternal(InternalPage *deleting_internal, Transaction *transaction);

  void MergeInternal(InternalPage *deleting_internal, InternalPage *parent_internal, InternalPage *neber_internal,
                     int target_index, int neber_index, bool is_last, Transaction *transaction);

  void UpdateParentId(page_id_t page_id, page_id_t p_page_id);

  auto StealSibling(LeafPage *deleting_leaf, Transaction *transaction) -> bool;

  auto StealSibling(LeafPage *deleting_leaf, InternalPage *parent_internal, LeafPage *neber_leaf, int target_index,
                    int neber_index, bool is_last) -> bool;

  auto StealInternal(InternalPage *deleting_internal, InternalPage *parent_internal, InternalPage *neber_internal,
                     int target_index, bool is_last) -> bool;

  /* some basic operations */
  auto GetLeafPage(page_id_t leaf_id, RWType rw) -> LeafPage *;
  auto GetInternalPage(page_id_t internal_id, RWType rw) -> InternalPage *;
  auto GetPage(page_id_t page_id, RWType rw) -> BPlusTreePage *;
  auto GetRootPage(OpType op, Transaction *transaction) -> BPlusTreePage *;
  auto GetLeftMostKey(InternalPage *internal_page) -> KeyType;
  auto CreateLeafPage(page_id_t *new_page_id, page_id_t parent_page_id, RWType rw) -> LeafPage *;
  auto CreateInternalPage(page_id_t *new_page_id, page_id_t parent_page_id, RWType rw) -> InternalPage *;
  auto CreatePage(page_id_t *new_page_id, RWType rw) -> BPlusTreePage *;
  void UnpinPage(Page *page, page_id_t page_id, bool is_dirty, RWType rw);
  void DeletePage(Page *page, page_id_t page_id, bool is_dirty, RWType rw);

  /* == member variable == */
  std::string index_name_;
  page_id_t root_page_id_;
  BufferPoolManager *buffer_pool_manager_;
  KeyComparator comparator_;
  int leaf_max_size_;
  int internal_max_size_;
  page_id_t first_leaf_id_ = INVALID_PAGE_ID;
  page_id_t last_leaf_id_ = INVALID_PAGE_ID;
  std::mutex root_latch_;
};

}  // namespace bustub
