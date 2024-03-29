//===----------------------------------------------------------------------===//
//
//                         CMU-DB Project (15-445/645)
//                         ***DO NO SHARE PUBLICLY***
//
// Identification: src/include/index/index_iterator.h
//
// Copyright (c) 2018, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//
/**
 * index_iterator.h
 * For range scan of b+ tree
 */
#pragma once
#include "buffer/buffer_pool_manager.h"
#include "storage/page/b_plus_tree_leaf_page.h"
#include "storage/page/b_plus_tree_page.h"

namespace bustub {

#define INDEXITERATOR_TYPE IndexIterator<KeyType, ValueType, KeyComparator>

INDEX_TEMPLATE_ARGUMENTS
class IndexIterator {
  using LeafPage = BPlusTreeLeafPage<KeyType, ValueType, KeyComparator>;

 public:
  // you may define your own constructor based on your member variables
  IndexIterator();
  IndexIterator(LeafPage *leaf, BufferPoolManager *bpm, int cur_pos = 0);

  ~IndexIterator();  // NOLINT

  auto IsEnd() -> bool;

  auto operator*() -> const MappingType &;

  auto operator++() -> IndexIterator &;

  auto operator==(const IndexIterator &itr) const -> bool {
    return itr.leaf_->GetPageId() == leaf_->GetPageId() && itr.cur_pos_ == cur_pos_;
  }

  auto operator!=(const IndexIterator &itr) const -> bool {
    return itr.leaf_->GetPageId() != leaf_->GetPageId() || itr.cur_pos_ != cur_pos_;
  }

 private:
  LeafPage *leaf_;
  int cur_leaf_size_;
  int cur_pos_;
  BufferPoolManager *buffer_pool_manager_;
};

}  // namespace bustub
