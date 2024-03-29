//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// buffer_pool_manager_instance.cpp
//
// Identification: src/buffer/buffer_pool_manager.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "buffer/buffer_pool_manager_instance.h"
#include <cstring>

#include "common/config.h"
#include "common/exception.h"
#include "common/logger.h"
#include "common/macros.h"

namespace bustub {

BufferPoolManagerInstance::BufferPoolManagerInstance(size_t pool_size, DiskManager *disk_manager, size_t replacer_k,
                                                     LogManager *log_manager)
    : pool_size_(pool_size), disk_manager_(disk_manager), log_manager_(log_manager) {
  // we allocate a consecutive memory space for the buffer pool
  pages_ = new Page[pool_size_];
  page_table_ = new ExtendibleHashTable<page_id_t, frame_id_t>(bucket_size_);
  replacer_ = new LRUKReplacer(pool_size, replacer_k);
  for (size_t i = 0; i < pool_size_; ++i) {
    free_list_.emplace_back(static_cast<int>(i));
  }
}

BufferPoolManagerInstance::~BufferPoolManagerInstance() {
  delete[] pages_;
  delete page_table_;
  delete replacer_;
}

auto BufferPoolManagerInstance::NewPgImp(page_id_t *page_id) -> Page * {
  std::lock_guard<std::mutex> lock(latch_);
  page_id_t new_page_id = AllocatePage();
  // LOG_DEBUG("new page %d", new_page_id);
  *page_id = new_page_id;
  Page *target = nullptr;
  frame_id_t target_frame;
  if (!free_list_.empty()) {
    // find in free list
    target_frame = free_list_.back();
    free_list_.pop_back();
    target = pages_ + target_frame;
  } else {
    // try evict
    if (!replacer_->Evict(&target_frame)) {
      LOG_DEBUG("new nullptr!!!");
      return nullptr;
    }
    target = pages_ + target_frame;
    // write back to disk
    if (target->is_dirty_) {
      disk_manager_->WritePage(target->page_id_, target->data_);
    }
    page_table_->Remove(target->page_id_);
  }
  // init the new page
  target->page_id_ = new_page_id;
  target->is_dirty_ = false;
  target->pin_count_ = 1;
  target->ResetMemory();
  // update page table
  page_table_->Insert(new_page_id, target_frame);
  // update the replacer
  replacer_->RecordAccess(target_frame);
  replacer_->SetEvictable(target_frame, false);
  return target;
}

auto BufferPoolManagerInstance::FetchPgImp(page_id_t page_id) -> Page * {
  std::lock_guard<std::mutex> lock(latch_);
  // LOG_DEBUG("Fetch page %d", page_id);
  Page *target = nullptr;
  frame_id_t target_frame;
  /* can find in buffer pool */
  if (page_table_->Find(page_id, target_frame)) {
    target = pages_ + target_frame;
    target->pin_count_++;
    replacer_->RecordAccess(target_frame);
    replacer_->SetEvictable(target_frame, false);
    return target;
  }

  /* can't find in buffer pool */
  if (!free_list_.empty()) {
    // find in free list
    target_frame = free_list_.back();
    free_list_.pop_back();
    target = pages_ + target_frame;
  } else {
    // try evict
    if (!replacer_->Evict(&target_frame)) {
      LOG_DEBUG("Fetch nullptr!!!");
      return nullptr;
    }
    target = pages_ + target_frame;
    // write back to disk
    if (target->is_dirty_) {
      disk_manager_->WritePage(target->page_id_, target->data_);
    }
    page_table_->Remove(target->page_id_);
  }
  target->ResetMemory();
  disk_manager_->ReadPage(page_id, target->data_);
  // init the page
  target->page_id_ = page_id;
  target->is_dirty_ = false;
  target->pin_count_ = 1;
  // update page table
  page_table_->Insert(page_id, target_frame);
  // update replacer
  replacer_->RecordAccess(target_frame);
  replacer_->SetEvictable(target_frame, false);
  return target;
}

auto BufferPoolManagerInstance::UnpinPgImp(page_id_t page_id, bool is_dirty) -> bool {
  std::lock_guard<std::mutex> lock(latch_);
  // LOG_DEBUG("Unpin page %d", page_id);
  frame_id_t target_frame;
  Page *target = nullptr;
  if (page_table_->Find(page_id, target_frame)) {
    target = pages_ + target_frame;
    if (target->pin_count_ <= 0) {
      return false;
    }
  } else {
    return false;
  }
  target->pin_count_--;
  if (target->pin_count_ <= 0) {
    replacer_->SetEvictable(target_frame, true);
  }
  target->is_dirty_ = is_dirty || target->is_dirty_;
  return true;
}

auto BufferPoolManagerInstance::FlushPgImp(page_id_t page_id) -> bool {
  std::lock_guard<std::mutex> lock(latch_);
  // LOG_DEBUG("Flush page %d", page_id);
  frame_id_t target_frame;
  Page *target = nullptr;
  if (page_table_->Find(page_id, target_frame)) {
    target = pages_ + target_frame;
  } else {
    return false;
  }
  disk_manager_->WritePage(target->page_id_, target->data_);
  target->is_dirty_ = false;
  return true;
}

void BufferPoolManagerInstance::FlushAllPgsImp() {
  std::lock_guard<std::mutex> lock(latch_);
  // LOG_DEBUG("Flush all page");
  for (size_t i = 0; i < pool_size_; ++i) {
    auto target = pages_ + i;
    if (target->page_id_ != INVALID_PAGE_ID && target->IsDirty()) {
      disk_manager_->WritePage(target->page_id_, target->data_);
      target->is_dirty_ = false;
    }
  }
}

auto BufferPoolManagerInstance::DeletePgImp(page_id_t page_id) -> bool {
  std::lock_guard<std::mutex> lock(latch_);
  // LOG_DEBUG("Unpin page %d", page_id);
  frame_id_t target_frame;
  Page *target = nullptr;
  if (page_table_->Find(page_id, target_frame)) {
    target = pages_ + target_frame;
    if (target->pin_count_ > 0) {
      return false;
    }
    // reset the target page
    target->ResetMemory();
    target->page_id_ = INVALID_PAGE_ID;
    target->is_dirty_ = false;
    target->pin_count_ = 0;
    // update page table
    page_table_->Remove(page_id);
    // update replacer
    replacer_->Remove(target_frame);
    // update free list
    free_list_.push_back(target_frame);
    DeallocatePage(page_id);
  }
  return true;
}

auto BufferPoolManagerInstance::AllocatePage() -> page_id_t { return next_page_id_++; }

}  // namespace bustub
