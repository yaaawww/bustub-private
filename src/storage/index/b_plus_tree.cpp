#include <cassert>
#include <new>
#include <string>

#include "common/config.h"
#include "common/exception.h"
#include "common/logger.h"
#include "common/rid.h"
#include "fmt/core.h"
#include "storage/index/b_plus_tree.h"
#include "storage/index/index_iterator.h"
#include "storage/page/b_plus_tree_internal_page.h"
#include "storage/page/b_plus_tree_leaf_page.h"
#include "storage/page/b_plus_tree_page.h"
#include "storage/page/header_page.h"

namespace bustub {
INDEX_TEMPLATE_ARGUMENTS
BPLUSTREE_TYPE::BPlusTree(std::string name, BufferPoolManager *buffer_pool_manager, const KeyComparator &comparator,
                          int leaf_max_size, int internal_max_size)
    : index_name_(std::move(name)),
      root_page_id_(INVALID_PAGE_ID),
      buffer_pool_manager_(buffer_pool_manager),
      comparator_(comparator),
      leaf_max_size_(leaf_max_size),
      internal_max_size_(internal_max_size) {
  LOG_DEBUG("Pool size is %zu", buffer_pool_manager->GetPoolSize());
  LOG_DEBUG("leaf max size %d, internal max size %d", leaf_max_size, internal_max_size);
}

/**
 * @brief
 * Helper function to decide whether current b+tree is empty
 * @return true
 * @return false
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::IsEmpty() const -> bool { return root_page_id_ == INVALID_PAGE_ID; }

/**
 * @brief
 * Helper function to find the leaf node page
 * @param key
 * @return LeafPage*
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::FindLeafPage(const KeyType &key) -> LeafPage * {
  auto curr_node_page = GetPage(root_page_id_);

  while (!curr_node_page->IsLeafPage()) {
    // guide by the internal page (key) [...).
    // do some comparations
    auto internal_node_page = reinterpret_cast<InternalPage *>(curr_node_page);
    page_id_t next_page_id = [&]() -> page_id_t {
      for (int i = 1; i <= internal_node_page->GetSize(); ++i) {
        KeyType cur_key = internal_node_page->KeyAt(i);
        if (comparator_(cur_key, key) == 1) {
          return internal_node_page->ValueAt(i - 1);
        }
      }
      return internal_node_page->ValueAt(internal_node_page->GetSize());
    }();  // find the next page id (down), (maybe lambda is good)

    UnpinPage(curr_node_page->GetPageId(), false);
    curr_node_page = GetPage(next_page_id);
  }

  return reinterpret_cast<LeafPage *>(curr_node_page);
}

/* Get the node page by page id */
/**
 * @brief
 *
 * @param parent_id
 * @return InternalPage*
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GetInternalPage(page_id_t internal_id) -> InternalPage * {
  return reinterpret_cast<InternalPage *>(buffer_pool_manager_->FetchPage(internal_id)->GetData());
}

/**
 * @brief
 *
 * @param next_id
 * @return LeafPage*
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GetLeafPage(page_id_t leaf_id) -> LeafPage * {
  return reinterpret_cast<LeafPage *>(buffer_pool_manager_->FetchPage(leaf_id)->GetData());
}

/**
 * @brief
 *
 * @param page_id
 * @return BPlusTreePage*
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GetPage(page_id_t page_id) -> BPlusTreePage * {
  return reinterpret_cast<BPlusTreePage *>(buffer_pool_manager_->FetchPage(page_id)->GetData());
}

/*****************************************************************************
 * SEARCH
 *****************************************************************************/
/*
 * Return the only value that associated with input key
 * This method is used for point query
 * @return : true means key exists
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GetValue(const KeyType &key, std::vector<ValueType> *result, Transaction *transaction) -> bool {
  LOG_DEBUG("get the value of %ld ", key.ToString());

  if (IsEmpty()) {
    return false;
  }
  // fetch the page
  auto leaf_node_page = FindLeafPage(key);
  for (int i = 0; i < leaf_node_page->GetSize(); ++i) {
    KeyType cur_key = leaf_node_page->KeyAt(i);
    if (comparator_(cur_key, key) == 0) {
      result->push_back(leaf_node_page->ValueAt(i));
      UnpinPage(leaf_node_page->GetPageId(), false);
      return true;
    }
  }
  UnpinPage(leaf_node_page->GetPageId(), false);
  return false;
}

/*****************************************************************************
 * INSERTION
 *****************************************************************************/
/*
 * Insert constant key & value pair into b+ tree
 * if current tree is empty, start new tree, update root page id and insert
 * entry, otherwise insert into leaf page.
 * @return: since we only support unique key, if user try to insert duplicate
 * keys return false, otherwise return true.
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Insert(const KeyType &key, const ValueType &value, Transaction *transaction) -> bool {
  LOG_DEBUG("Insert %ld ", key.ToString());

  if (IsEmpty()) {
    page_id_t new_root_page_id;
    auto new_root = reinterpret_cast<LeafPage *>(buffer_pool_manager_->NewPage(&new_root_page_id));
    LOG_DEBUG("new page id %d", new_root_page_id);
    root_page_id_ = new_root_page_id;
    UpdateRootPageId(0);
    new_root->Init(new_root_page_id, INVALID_PAGE_ID, leaf_max_size_);
    new_root->Insert(key, value, comparator_);
    UnpinPage(root_page_id_, true);
    return true;
  }

  auto leaf_node_page = FindLeafPage(key);
  bool success = leaf_node_page->Insert(key, value, comparator_);
  if (!success) {
    // duplicate!!
    UnpinPage(leaf_node_page->GetPageId(), false);
    return false;
  }
  // check if needing splitting the node
  if (leaf_node_page->NeedSplit()) {
    SplitLeaf(leaf_node_page);
    return true;
  }

  UnpinPage(leaf_node_page->GetPageId(), true);
  return true;
}

/**
 * @brief Split the leaf page
 *
 * @param over_node
 * @return INDEX_TEMPLATE_ARGUMENTS
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::SplitLeaf(LeafPage *over_node) {
  int size = over_node->GetSize();
  int mid_index = over_node->GetMaxSize() / 2;
  KeyType mid_key = over_node->KeyAt(mid_index);
  page_id_t over_node_id = over_node->GetPageId();
  page_id_t new_leaf_id;

  /* == get parent node == */
  InternalPage *parent_node = nullptr;

  // create new leaf node page
  auto new_leaf = reinterpret_cast<LeafPage *>(buffer_pool_manager_->NewPage(&new_leaf_id));
  LOG_DEBUG("new page id %d", new_leaf_id);

  if (!over_node->IsRootPage()) {
    new_leaf->Init(new_leaf_id, over_node->GetParentPageId(), leaf_max_size_);
  } else {
    // create new root node page
    page_id_t new_root_page_id;
    auto new_root = reinterpret_cast<InternalPage *>(buffer_pool_manager_->NewPage(&new_root_page_id));
    LOG_DEBUG("new page id %d", new_root_page_id);
    root_page_id_ = new_root_page_id;
    UpdateRootPageId(0);
    new_root->Init(new_root_page_id, INVALID_PAGE_ID, internal_max_size_);
    new_root->SetFirstPoint(over_node_id);

    over_node->SetParentPageId(root_page_id_);
    new_leaf->Init(new_leaf_id, root_page_id_, leaf_max_size_);

    parent_node = new_root;
  }

  /* == Address the parent level == */
  if (parent_node == nullptr) {
    parent_node = GetInternalPage(over_node->GetParentPageId());
  }
  parent_node->Insert(mid_key, new_leaf_id, comparator_);

  /* == Address the leaf level == */
  // Insert the new leaf in the list
  page_id_t next_page_id = over_node->GetNextPageId();
  new_leaf->SetNextPageId(next_page_id);
  new_leaf->SetPrevPageId(over_node_id);
  if (!over_node->IsLast()) {
    auto leaf = GetLeafPage(next_page_id);
    leaf->SetPrevPageId(new_leaf_id);
    UnpinPage(next_page_id, true);
  }
  over_node->SetNextPageId(new_leaf_id);

  /* = move data = */
  auto left_arr = over_node->GetArray();
  auto right_arr = new_leaf->GetArray();
  /* address the right array */
  std::copy(left_arr + mid_index, left_arr + size, right_arr);
  new_leaf->IncreaseSize(size - mid_index);
  /* address the left array */
  over_node->IncreaseSize(-1 * (size - mid_index));

  UnpinPage(over_node_id, true);
  UnpinPage(new_leaf_id, true);
  /* == check if need to split the parent == */
  if (parent_node->NeedSplit()) {
    page_id_t new_page_id;
    auto new_node = reinterpret_cast<InternalPage *>(buffer_pool_manager_->NewPage(&new_page_id));
    LOG_DEBUG("new page id %d", new_page_id);
    new_node->Init(new_page_id, INVALID_PAGE_ID, internal_max_size_);
    SplitInternal(parent_node, new_node);
  }
  UnpinPage(parent_node->GetPageId(), true);
}

/**
 * @brief Split the internal page
 *
 * @param over_node
 * @param new_internal
 * @return INDEX_TEMPLATE_ARGUMENTS
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::SplitInternal(InternalPage *over_node, InternalPage *new_internal) {
  int size = over_node->GetSize();
  int mid_index = over_node->GetMaxSize() / 2 + 1;
  KeyType mid_key = over_node->KeyAt(mid_index);
  page_id_t new_internal_id = new_internal->GetPageId();
  page_id_t over_node_id = over_node->GetPageId();

  /* == get parent node == */
  InternalPage *parent_node = nullptr;

  if (!over_node->IsRootPage()) {
    new_internal->SetParentPageId(over_node->GetParentPageId());
  } else {
    // create new root node page
    page_id_t new_root_page_id;
    auto new_root = reinterpret_cast<InternalPage *>(buffer_pool_manager_->NewPage(&new_root_page_id));
    LOG_DEBUG("new page id %d", new_root_page_id);
    root_page_id_ = new_root_page_id;
    UpdateRootPageId(0);
    new_root->Init(new_root_page_id, INVALID_PAGE_ID, internal_max_size_);
    new_root->SetFirstPoint(over_node_id);

    over_node->SetParentPageId(root_page_id_);
    new_internal->Init(new_internal_id, root_page_id_, internal_max_size_);

    parent_node = new_root;
  }

  /* == Address the parent level == */
  if (parent_node == nullptr) {
    parent_node = GetInternalPage(over_node->GetParentPageId());
  }
  parent_node->Insert(mid_key, new_internal_id, comparator_);

  /* == Address the low level (cut data into the new internal node.) == */
  auto left_arr = over_node->GetArray();
  auto right_arr = new_internal->GetArray();
  page_id_t mid_page_id = left_arr[mid_index].second;

  /* address the right array
    (we have to iterate it, because we want to update the ParentId for the child node.) */
  for (int i = 0; i < size - mid_index; ++i) {
    auto mapp_elem = left_arr[i + mid_index + 1];
    UpdateParentId(mapp_elem.second, new_internal_id);
    right_arr[i + 1] = mapp_elem;
  }
  UpdateParentId(mid_page_id, new_internal_id);
  new_internal->SetFirstPoint(mid_page_id);
  new_internal->IncreaseSize(size - mid_index);
  /* address the left array
    (we don't have to clear the data, we can just decrease the size of the node.) */
  over_node->IncreaseSize(-1 * (size - mid_index + 1));

  // UnpinPage(over_node_id, true);
  UnpinPage(new_internal_id, true);

  if (parent_node->NeedSplit()) {
    page_id_t new_page_id;
    auto new_node = reinterpret_cast<InternalPage *>(buffer_pool_manager_->NewPage(&new_page_id));
    LOG_DEBUG("new page id %d", new_page_id);
    new_node->Init(new_page_id, INVALID_PAGE_ID, internal_max_size_);
    SplitInternal(parent_node, new_node);
  }
  UnpinPage(parent_node->GetPageId(), true);
}

/**
 * @brief Update the ParentId, when create the new internal page
 *
 * @param page_id
 * @param p_page_id
 * @return INDEX_TEMPLATE_ARGUMENTS
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::UpdateParentId(page_id_t page_id, page_id_t p_page_id) {
  auto target_page = GetPage(page_id);
  target_page->SetParentPageId(p_page_id);
  UnpinPage(page_id, true);
}

/*****************************************************************************
 * REMOVE
 *****************************************************************************/
/*
 * Delete key & value pair associated with input key
 * If current tree is empty, return immdiately.
 * If not, User needs to first find the right leaf page as deletion target, then
 * delete entry from leaf page. Remember to deal with redistribute or merge if
 * necessary.
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::Remove(const KeyType &key, Transaction *transaction) {
  LOG_DEBUG("Remove %ld ", key.ToString());

  if (IsEmpty()) {
    return;
  }
  bool need_update = false;
  LeafPage *deleting_leaf = FindLeafPage(key);

  if (!deleting_leaf->Remove(key, comparator_, &need_update)) {
    return;
  }
  if (deleting_leaf->IsRootPage()) {
    return;
  }

  /* = Normal Delete = */
  if (!deleting_leaf->NeedRedsb()) {
    if (need_update) {
      UpdateParentKey(deleting_leaf->KeyAt(0), deleting_leaf->GetPageId());
    }
    UnpinPage(deleting_leaf->GetPageId(), true);
    return;
  }

  /* = Steal = */
  if (StealSibling(deleting_leaf)) {
    if (need_update) {
      UpdateParentKey(deleting_leaf->KeyAt(0), deleting_leaf->GetPageId());
    }
    UnpinPage(deleting_leaf->GetPageId(), true);
    return;
  }

  /* = Merge = */
  Merge(deleting_leaf);
}

/**
 * @brief Steal (borrow) from the right sibling, when L has only M/2-1 entry.
 *
 * @param deleting_leaf
 * @param value
 * @return true
 * @return false
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::StealSibling(LeafPage *deleting_leaf) -> bool {
  MappingType value;
  LeafPage *stealing_leaf = nullptr;
  if (deleting_leaf->IsLast()) {
    stealing_leaf = GetLeafPage(deleting_leaf->GetPrevPageId());
    if (!stealing_leaf->StealLast(&value)) {
      UnpinPage(stealing_leaf->GetPageId(), false);
      return false;
    }
    deleting_leaf->InsertFirst(&value);
    UpdateParentKey(deleting_leaf->KeyAt(0), deleting_leaf->GetPageId());
  } else {
    stealing_leaf = GetLeafPage(deleting_leaf->GetNextPageId());
    if (!stealing_leaf->StealFirst(&value)) {
      UnpinPage(stealing_leaf->GetPageId(), false);
      return false;
    }
    deleting_leaf->InsertLast(&value);
    // UpdateRootParentKey(value.first,
    // stealing_leaf->KeyAt(0),
    // deleting_leaf->GetParentPageId(),
    // stealing_leaf->GetParentPageId());
    UpdateParentKey(stealing_leaf->KeyAt(0), stealing_leaf->GetPageId());
  }
  UnpinPage(stealing_leaf->GetPageId(), true);
  return true;
}

/**
 * @brief Steal (borrow) from the neighbour internal page.
 *
 * @param deleting_internal
 * @return true
 * @return false
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::StealInternal(InternalPage *deleting_internal, InternalPage *parent_internal,
                                   InternalPage *neber_internal, int target_index, bool is_last) -> bool {
  std::pair<KeyType, page_id_t> value;
  if (is_last) {
    if (!neber_internal->StealLast(&value)) {
      return false;
    }
    deleting_internal->InsertFirst(&value);
    // Update the current page
    deleting_internal->SetKeyAt(1, parent_internal->KeyAt(target_index));
    // Update the parent page key
    parent_internal->SetKeyAt(target_index, value.first);
  } else {
    if (!neber_internal->StealFirst(&value)) {
      return false;
    }
    deleting_internal->InsertLast(&value);
    // Update the current page
    deleting_internal->SetKeyAt(deleting_internal->GetSize(), parent_internal->KeyAt(target_index + 1));
    // Update the parent page key
    parent_internal->SetKeyAt(target_index + 1, GetLeftMostKey(neber_internal));
  }
  UpdateParentId(value.second, deleting_internal->GetPageId());
  return true;
}

/**
 * @brief Merge the node to its neighbour
 *
 * @param merge_node
 * @return INDEX_TEMPLATE_ARGUMENTS
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::Merge(BPlusTreePage *rest_node) {
  if (rest_node->IsLeafPage()) {
    /* == Redistribute the leaf page ==
     * Two case: rest node is the last one or not.
     */
    auto rest_leaf = reinterpret_cast<LeafPage *>(rest_node);
    auto parent_page = GetInternalPage(rest_leaf->GetParentPageId());

    int target_index = parent_page->SearchPosition(rest_leaf->GetPageId());
    LeafPage *merging_leaf = nullptr;

    if (rest_leaf->IsLast()) {
      merging_leaf = GetLeafPage(rest_leaf->GetPrevPageId());
      merging_leaf->MergeFromRight(rest_leaf);
      // update list
      merging_leaf->SetNextPageId(INVALID_PAGE_ID);
      // remove the correspond parent item
      parent_page->Remove(target_index);
    } else {
      merging_leaf = GetLeafPage(rest_leaf->GetNextPageId());
      merging_leaf->MergeFromLeft(rest_leaf);
      // update list
      if (rest_leaf->IsFirst()) {
        merging_leaf->SetPrevPageId(INVALID_PAGE_ID);
      } else {
        auto prev_leaf = GetLeafPage(rest_leaf->GetPrevPageId());
        prev_leaf->SetNextPageId(merging_leaf->GetPageId());
        merging_leaf->SetPrevPageId(prev_leaf->GetPageId());
      }
      // remove the correspond parent item
      parent_page->Remove(target_index);
      // update the parent key
      UpdateParentKey(merging_leaf->KeyAt(0), merging_leaf->GetPageId());
    }

    UnpinPage(merging_leaf->GetPageId(), true);
    UnpinPage(rest_leaf->GetPageId(), false);
    DeletePage(rest_leaf->GetPageId());

    // check parent

    if (parent_page->NeedRedsb()) {
      if (parent_page->IsRootPage() && parent_page->GetSize() <= 0) {
        if (parent_page->GetSize() <= 0) { 
          merging_leaf->SetParentPageId(INVALID_PAGE_ID);
          UnpinPage(root_page_id_, false);
          DeletePage(root_page_id_);
          root_page_id_ = merging_leaf->GetPageId();
          UpdateRootPageId(0);
        } else {
          UnpinPage(root_page_id_, false);
          return;
        }
      } else {
        Merge(parent_page);
      }

    } else {
      UnpinPage(parent_page->GetPageId(), true);
    }

  } else {
    /* == Redistribute the internal page ==
     * borrow one from the neighbour internal page.
     * if failure, we should merge with the neighbor.
     * merge and update the fisrt key to the parent correspond key
     * remove the correspont item in parent when after merge.
     * update the parent key to the left most key of subtree.
     */
    // Here are some neccessy variable
    auto deleting_internal = reinterpret_cast<InternalPage *>(rest_node);
    auto parent_internal = GetInternalPage(deleting_internal->GetParentPageId());
    int target_index = parent_internal->SearchPosition(deleting_internal->GetPageId());
    int neber_index = target_index == parent_internal->GetSize() ? target_index - 1 : target_index + 1;
    auto neber_internal = GetInternalPage(parent_internal->ValueAt(neber_index));

    bool is_last = neber_index < target_index;

    /* = Steal = */
    if (StealInternal(deleting_internal, parent_internal, neber_internal, target_index, is_last)) {
      UnpinPage(deleting_internal->GetPageId(), true);
      UnpinPage(parent_internal->GetPageId(), true);
      UnpinPage(neber_internal->GetPageId(), true);
      return;
    }
    /* = Merge = */
    auto deleting_arr = deleting_internal->GetArray();
    auto neber_arr = neber_internal->GetArray();

    if (is_last) {
      // update the neber internal page's key
      deleting_internal->SetKeyAt(0, parent_internal->KeyAt(target_index));
      /* Merge two internal page */
      // std::copy(right_arr, right_arr + neber_internal->GetSize() + 1, right_arr + deleting_internal->GetSize() + 1);
      int neber_size = neber_internal->GetSize();
      for (int i = 0; i < deleting_internal->GetSize() + 1; ++i) {
        auto mapp_elem = deleting_arr[i];
        UpdateParentId(mapp_elem.second, neber_internal->GetPageId());
        neber_arr[i + neber_size + 1] = mapp_elem;
      }
      neber_internal->IncreaseSize(deleting_internal->GetSize() + 1);
      parent_internal->Remove(target_index);
    } else {
      // update the neber internal page's key
      int neber_size = neber_internal->GetSize();
      int deleting_size = deleting_internal->GetSize();
      neber_internal->SetKeyAt(0, parent_internal->KeyAt(target_index + 1));
      /* Merge two internal page */
      // std::copy(neber_arr, neber_arr + neber_size + 1, neber_arr + deleting_size + 1);
      for (int i = 0; i < neber_size + 1; ++i) {
        neber_arr[neber_size + deleting_size + 1 - i] = neber_arr[neber_size - i];
      }

      for (int i = 0; i < deleting_internal->GetSize() + 1; ++i) {
        auto mapp_elem = deleting_arr[i];
        UpdateParentId(mapp_elem.second, neber_internal->GetPageId());
        neber_arr[i] = mapp_elem;
      }
      neber_internal->IncreaseSize(deleting_internal->GetSize() + 1);
      parent_internal->Remove(target_index);
      parent_internal->SetKeyAt(target_index, GetLeftMostKey(deleting_internal));
    }

    UnpinPage(neber_internal->GetPageId(), true);
    UnpinPage(deleting_internal->GetPageId(), false);
    DeletePage(deleting_internal->GetPageId());

    // Recursion
    if (parent_internal->NeedRedsb()) {
      if (parent_internal->IsRootPage() ) {
        if (parent_internal->GetSize() <= 0) { 
          neber_internal->SetParentPageId(INVALID_PAGE_ID);
          UnpinPage(root_page_id_, false);
          DeletePage(root_page_id_);
          root_page_id_ = neber_internal->GetPageId();
          UpdateRootPageId(0);
        } else {
          UnpinPage(root_page_id_, false);
          return;
        }
      } else {
        Merge(parent_internal);
      }

    } else {
      UnpinPage(parent_internal->GetPageId(), true);
    }
  }
}

/**
 * @brief
 *
 * @return LeafPage*
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GetFirstLeaf() -> LeafPage * {
  BPlusTreePage *cur_page = GetInternalPage(root_page_id_);
  while (!cur_page->IsLeafPage()) {
    auto internal = reinterpret_cast<InternalPage *>(cur_page);
    cur_page = GetPage(internal->ValueAt(0));
    UnpinPage(internal->GetPageId(), false);
  }
  return reinterpret_cast<LeafPage *>(cur_page);
}

/**
 * @brief
 *
 * @return LeafPage*
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GetLastLeaf() -> LeafPage * {
  BPlusTreePage *cur_page = GetInternalPage(root_page_id_);
  while (!cur_page->IsLeafPage()) {
    auto internal = reinterpret_cast<InternalPage *>(cur_page);
    cur_page = GetPage(internal->ValueAt(cur_page->GetSize()));
    UnpinPage(internal->GetPageId(), false);
  }
  return reinterpret_cast<LeafPage *>(cur_page);
}

/**
 * @brief
 *
 * @param internal_page
 * @return KeyType
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GetLeftMostKey(InternalPage *internal_page) -> KeyType {
  BPlusTreePage *cur_page = GetInternalPage(internal_page->ValueAt(0));
  while (!cur_page->IsLeafPage()) {
    auto internal = reinterpret_cast<InternalPage *>(cur_page);
    cur_page = GetPage(internal->ValueAt(0));
    UnpinPage(internal->GetPageId(), false);
  }

  auto leaf = reinterpret_cast<LeafPage *>(cur_page);
  auto key = leaf->KeyAt(0);
  auto leaf_id = leaf->GetPageId();
  UnpinPage(leaf_id, false);

  return key;
}

INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::UpdateParentKey(const KeyType &new_key, page_id_t page_id) {
  auto cur_page = GetPage(page_id);
  page_id_t parent_id = cur_page->GetParentPageId();
  page_id_t cur_id = page_id;
  auto parent_page = GetInternalPage(parent_id);
  int pos = parent_page->SearchPosition(cur_id);

  while (pos == 0) {
    cur_page = parent_page;
    UnpinPage(cur_id, false);
    cur_id = parent_id;
    parent_id = cur_page->GetParentPageId();
    parent_page = GetInternalPage(parent_id);
    pos = parent_page->SearchPosition(cur_id);
  }

  parent_page->SetKeyAt(pos, new_key);
  UnpinPage(cur_id, false);
  UnpinPage(parent_id, true);
}

// INDEX_TEMPLATE_ARGUMENTS
// void BPLUSTREE_TYPE::UpdateRootParentKey(const KeyType &old_key, const KeyType &new_key, page_id_t left_parent_id,
// page_id_t right_parent_id) { while (left_parent_id != right_parent_id) { auto left_node =
// GetInternalPage(left_parent_id); auto right_node = GetInternalPage(right_parent_id); left_parent_id =
// left_node->GetParentPageId(); right_parent_id = right_node->GetParentPageId(); UnpinPage(left_node->GetPageId(),
// false); UnpinPage(right_node->GetPageId(), false);
// }
// UpdateParentKey(new_key, left_parent_id);
// }

/*****************************************************************************
 * INDEX ITERATOR
 *****************************************************************************/
/*
 * Input parameter is void, find the leaftmost leaf page first, then construct
 * index iterator
 * @return : index iterator
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Begin() -> INDEXITERATOR_TYPE {
  return std::move(IndexIterator<KeyType, ValueType, KeyComparator>(GetFirstLeaf(), buffer_pool_manager_));
}

/*
 * Input parameter is low key, find the leaf page that contains the input key
 * first, then construct index iterator
 * @return : index iterator
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Begin(const KeyType &key) -> INDEXITERATOR_TYPE {
  auto target_leaf = FindLeafPage(key);
  int targe_index = target_leaf->Search(key, comparator_);
  assert(targe_index != -1);
  return std::move(IndexIterator<KeyType, ValueType, KeyComparator>(target_leaf, buffer_pool_manager_, targe_index));
}

/*
 * Input parameter is void, construct an index iterator representing the end
 * of the key/value pair in the leaf node
 * @return : index iterator
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::End() -> INDEXITERATOR_TYPE {
  auto target_leaf = GetLastLeaf();
  return std::move(
      IndexIterator<KeyType, ValueType, KeyComparator>(target_leaf, buffer_pool_manager_, target_leaf->GetSize()));
}

/**
 * @return Page id of the root of this tree
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GetRootPageId() -> page_id_t { return root_page_id_; }

/*****************************************************************************
 * UTILITIES AND DEBUG
 *****************************************************************************/
/*
 * Update/Insert root page id in header page(where page_id = 0, header_page is
 * defined under include/page/header_page.h)
 * Call this method everytime root page id is changed.
 * @parameter: insert_record      defualt value is false. When set to true,
 * insert a record <index_name, root_page_id> into header page instead of
 * updating it.
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::UpdateRootPageId(int insert_record) {
  auto *header_page = static_cast<HeaderPage *>(buffer_pool_manager_->FetchPage(HEADER_PAGE_ID));
  if (insert_record != 0) {
    // create a new record<index_name + root_page_id> in header_page
    header_page->InsertRecord(index_name_, root_page_id_);
  } else {
    // update root_page_id in header_page
    header_page->UpdateRecord(index_name_, root_page_id_);
  }
  buffer_pool_manager_->UnpinPage(HEADER_PAGE_ID, true);
}

/*
 * This method is used for test only
 * Read data from file and insert one by one
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::InsertFromFile(const std::string &file_name, Transaction *transaction) {
  int64_t key;
  std::ifstream input(file_name);
  while (input) {
    input >> key;

    KeyType index_key;
    index_key.SetFromInteger(key);
    RID rid(key);
    Insert(index_key, rid, transaction);
  }
}

/*
 * This method is used for test only
 * Read data from file and remove one by one
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::RemoveFromFile(const std::string &file_name, Transaction *transaction) {
  int64_t key;
  std::ifstream input(file_name);
  while (input) {
    input >> key;
    KeyType index_key;
    index_key.SetFromInteger(key);
    Remove(index_key, transaction);
  }
}

/**
 * This method is used for debug only, You don't need to modify
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::Draw(BufferPoolManager *bpm, const std::string &outf) {
  if (IsEmpty()) {
    LOG_WARN("Draw an empty tree");
    return;
  }
  std::ofstream out(outf);
  out << "digraph G {" << std::endl;
  ToGraph(reinterpret_cast<BPlusTreePage *>(bpm->FetchPage(root_page_id_)->GetData()), bpm, out);
  out << "}" << std::endl;
  out.flush();
  out.close();
}

/**
 * This method is used for debug only, You don't need to modify
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::Print(BufferPoolManager *bpm) {
  if (IsEmpty()) {
    LOG_WARN("Print an empty tree");
    return;
  }
  ToString(reinterpret_cast<BPlusTreePage *>(bpm->FetchPage(root_page_id_)->GetData()), bpm);
}

/**
 * This method is used for debug only, You don't need to modify
 * @tparam KeyType
 * @tparam ValueType
 * @tparam KeyComparator
 * @param page
 * @param bpm
 * @param out
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::ToGraph(BPlusTreePage *page, BufferPoolManager *bpm, std::ofstream &out) const {
  std::string leaf_prefix("LEAF_");
  std::string internal_prefix("INT_");
  if (page->IsLeafPage()) {
    auto *leaf = reinterpret_cast<LeafPage *>(page);
    // Print node name
    out << leaf_prefix << leaf->GetPageId();
    // Print node properties
    out << "[shape=plain color=green ";
    // Print data of the node
    out << "label=<<TABLE BORDER=\"0\" CELLBORDER=\"1\" CELLSPACING=\"0\" CELLPADDING=\"4\">\n";
    // Print data
    out << "<TR><TD COLSPAN=\"" << leaf->GetSize() << "\">P=" << leaf->GetPageId() << "</TD></TR>\n";
    out << "<TR><TD COLSPAN=\"" << leaf->GetSize() << "\">"
        << "max_size=" << leaf->GetMaxSize() << ",min_size=" << leaf->GetMinSize() << ",size=" << leaf->GetSize()
        << "</TD></TR>\n";
    out << "<TR>";
    for (int i = 0; i < leaf->GetSize(); i++) {
      out << "<TD>" << leaf->KeyAt(i) << "</TD>\n";
    }
    out << "</TR>";
    // Print table end
    out << "</TABLE>>];\n";
    // Print Leaf node link if there is a next page
    if (leaf->GetNextPageId() != INVALID_PAGE_ID) {
      out << leaf_prefix << leaf->GetPageId() << " -> " << leaf_prefix << leaf->GetNextPageId() << ";\n";
      out << "{rank=same " << leaf_prefix << leaf->GetPageId() << " " << leaf_prefix << leaf->GetNextPageId() << "};\n";
    }

    // Print parent links if there is a parent
    if (leaf->GetParentPageId() != INVALID_PAGE_ID) {
      out << internal_prefix << leaf->GetParentPageId() << ":p" << leaf->GetPageId() << " -> " << leaf_prefix
          << leaf->GetPageId() << ";\n";
    }
  } else {
    auto *inner = reinterpret_cast<InternalPage *>(page);
    // Print node name
    out << internal_prefix << inner->GetPageId();
    // Print node properties
    out << "[shape=plain color=pink ";  // why not?
    // Print data of the node
    out << "label=<<TABLE BORDER=\"0\" CELLBORDER=\"1\" CELLSPACING=\"0\" CELLPADDING=\"4\">\n";
    // Print data
    out << "<TR><TD COLSPAN=\"" << inner->GetSize() + 1 << "\">P=" << inner->GetPageId() << "</TD></TR>\n";
    out << "<TR><TD COLSPAN=\"" << inner->GetSize() + 1 << "\">"
        << "max_size=" << inner->GetMaxSize() << ",min_size=" << inner->GetMinSize() << ",size=" << inner->GetSize()
        << "</TD></TR>\n";
    out << "<TR>";
    for (int i = 0; i <= inner->GetSize(); i++) {
      out << "<TD PORT=\"p" << inner->ValueAt(i) << "\">";
      if (i > 0) {
        out << inner->KeyAt(i);
      } else {
        out << " ";
      }
      out << "</TD>\n";
    }
    out << "</TR>";
    // Print table end
    out << "</TABLE>>];\n";
    // Print Parent link
    if (inner->GetParentPageId() != INVALID_PAGE_ID) {
      out << internal_prefix << inner->GetParentPageId() << ":p" << inner->GetPageId() << " -> " << internal_prefix
          << inner->GetPageId() << ";\n";
    }
    // Print leaves
    for (int i = 0; i <= inner->GetSize(); i++) {
      auto child_page = reinterpret_cast<BPlusTreePage *>(bpm->FetchPage(inner->ValueAt(i))->GetData());
      ToGraph(child_page, bpm, out);
      if (i > 0) {
        auto sibling_page = reinterpret_cast<BPlusTreePage *>(bpm->FetchPage(inner->ValueAt(i - 1))->GetData());
        if (!sibling_page->IsLeafPage() && !child_page->IsLeafPage()) {
          out << "{rank=same " << internal_prefix << sibling_page->GetPageId() << " " << internal_prefix
              << child_page->GetPageId() << "};\n";
        }
        bpm->UnpinPage(sibling_page->GetPageId(), false);
      }
    }
  }
  bpm->UnpinPage(page->GetPageId(), false);
}

/**
 * This function is for debug only, you don't need to modify
 * @tparam KeyType
 * @tparam ValueType
 * @tparam KeyComparator
 * @param page
 * @param bpm
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::ToString(BPlusTreePage *page, BufferPoolManager *bpm) const {
  if (page->IsLeafPage()) {
    auto *leaf = reinterpret_cast<LeafPage *>(page);
    std::cout << "Leaf Page: " << leaf->GetPageId() << " parent: " << leaf->GetParentPageId()
              << " next: " << leaf->GetNextPageId() << std::endl;
    for (int i = 0; i < leaf->GetSize(); i++) {
      std::cout << leaf->KeyAt(i) << ",";
    }
    std::cout << std::endl;
    std::cout << std::endl;
  } else {
    auto *internal = reinterpret_cast<InternalPage *>(page);
    std::cout << "Internal Page: " << internal->GetPageId() << " parent: " << internal->GetParentPageId() << std::endl;
    for (int i = 0; i < internal->GetSize(); i++) {
      std::cout << internal->KeyAt(i) << ": " << internal->ValueAt(i) << ",";
    }
    std::cout << std::endl;
    std::cout << std::endl;
    for (int i = 0; i < internal->GetSize(); i++) {
      ToString(reinterpret_cast<BPlusTreePage *>(bpm->FetchPage(internal->ValueAt(i))->GetData()), bpm);
    }
  }
  bpm->UnpinPage(page->GetPageId(), false);
}

template class BPlusTree<GenericKey<4>, RID, GenericComparator<4>>;
template class BPlusTree<GenericKey<8>, RID, GenericComparator<8>>;
template class BPlusTree<GenericKey<16>, RID, GenericComparator<16>>;
template class BPlusTree<GenericKey<32>, RID, GenericComparator<32>>;
template class BPlusTree<GenericKey<64>, RID, GenericComparator<64>>;

}  // namespace bustub
