// :bustub-keep-private:
//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// arc_replacer.cpp
//
// Identification: src/buffer/arc_replacer.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "buffer/arc_replacer.h"
#include <optional>
#include "common/config.h"

namespace bustub {

/**
 *
 * TODO(P1): Add implementation
 *
 * @brief a new ArcReplacer, with lists initialized to be empty and target size to 0
 * @param num_frames the maximum number of frames the ArcReplacer will be required to cache
 */
ArcReplacer::ArcReplacer(size_t num_frames) : replacer_size_(num_frames) {}

/**
 * TODO(P1): Add implementation
 *
 * @brief Performs the Replace operation as described by the writeup
 * that evicts from either mfu_ or mru_ into its corresponding ghost list
 * according to balancing policy.
 *
 * If you wish to refer to the original ARC paper, please note that there are
 * two changes in our implementation:
 * 1. When the size of mru_ equals the target size, we don't check
 * the last access as the paper did when deciding which list to evict from.
 * This is fine since the original decision is stated to be arbitrary.
 * 2. Entries that are not evictable are skipped. If all entries from the desired side
 * (mru_ / mfu_) are pinned, we instead try victimize the other side (mfu_ / mru_),
 * and move it to its corresponding ghost list (mfu_ghost_ / mru_ghost_).
 *
 * @return frame id of the evicted frame, or std::nullopt if cannot evict
 */
auto ArcReplacer::Evict() -> std::optional<frame_id_t> {
  std::lock_guard<std::mutex> lock(latch_);
  if (curr_size_ == 0) return std::nullopt;
  std::shared_ptr<FrameStatus> victim_status = nullptr;
  // 若|T1|>p，优先从T1选，否则从T2选，同时若目标列表全部被Pin住了，要尝试另一边
  bool evict_from_mru = (mru_.size() >= mru_target_size_ && !mru_.empty()) || mfu_.empty();
  // 在list尾部找第一个可踢出的页
  auto find_victim = [&](std::list<frame_id_t> &list) -> bool {
    for (auto it = list.rbegin(); it != list.rend(); ++it) {
      if (alive_map_[*it]->evictable_) {
        victim_status = alive_map_[*it];
        list.erase(std::next(it).base());
        return true;
      }
    }
    return false;
  };
  if (evict_from_mru) {
    // MRU全被Pin住了就去MFU找
    if (!find_victim(mru_)) find_victim(mfu_);
  } else {
    // 同上反之
    if (!find_victim(mfu_)) find_victim(mru_);
  }
  if (!victim_status) return std::nullopt;
  // 把被踢出的移入幽灵列表
  frame_id_t fid = victim_status->frame_id_;
  page_id_t pid = victim_status->page_id_;
  if (victim_status->arc_status_ == ArcStatus::MRU) {
    victim_status->arc_status_ = ArcStatus::MRU_GHOST;
    mru_ghost_.push_front(pid);
    victim_status->ghost_it_ = mru_ghost_.begin();
  } else {
    victim_status->arc_status_ = ArcStatus::MFU_GHOST;
    mfu_ghost_.push_front(pid);
    victim_status->ghost_it_ = mfu_ghost_.begin();
  }
  // 从内存档案删除，记入幽灵档案
  alive_map_.erase(fid);
  ghost_map_[pid] = victim_status;
  curr_size_--;
  return fid;
}

/**
 * TODO(P1): Add implementation
 *
 * @brief Record access to a frame, adjusting ARC bookkeeping accordingly
 * by bring the accessed page to the front of mfu_ if it exists in any of the lists
 * or the front of mru_ if it does not.
 *
 * Performs the operations EXCEPT REPLACE described in original paper, which is
 * handled by `Evict()`.
 *
 * Consider the following four cases, handle accordingly:
 * 1. Access hits mru_ or mfu_
 * 2/3. Access hits mru_ghost_ / mfu_ghost_
 * 4. Access misses all the lists
 *
 * This routine performs all changes to the four lists as preperation
 * for `Evict()` to simply find and evict a victim into ghost lists.
 *
 * Note that frame_id is used as identifier for alive pages and
 * page_id is used as identifier for the ghost pages, since page_id is
 * the unique identifier to the page after it's dead.
 * Using page_id for alive pages should be the same since it's one to one mapping,
 * but using frame_id is slightly more intuitive.
 *
 * @param frame_id id of frame that received a new access.
 * @param page_id id of page that is mapped to the frame.
 * @param access_type type of access that was received. This parameter is only needed for
 * leaderboard tests.
 */
void ArcReplacer::RecordAccess(frame_id_t frame_id, page_id_t page_id, [[maybe_unused]] AccessType access_type) {
  // case1:命中内存（T1或T2）
  if (alive_map_.count(frame_id)) {
    auto status = alive_map_[frame_id];
    // 从当前列表移除
    if (status->arc_status_ == ArcStatus::MRU) {
      mru_.erase(status->alive_it_);
    } else {
      mfu_.erase(status->alive_it_);
    }
    // 统一进入T2头部
    status->arc_status_ = ArcStatus::MFU;
    mfu_.push_front(frame_id);
    status->alive_it_ = mfu_.begin();
    return;
  }
  // case2/3:命中了幽灵列表
  if (ghost_map_.count(page_id)) {
    auto status = ghost_map_[page_id];
    // 命中B1：说明MRU需要更多空间，增大p
    if (status->arc_status_ == ArcStatus::MRU_GHOST) {
      size_t delta = (mfu_ghost_.empty()) ? 1 : std::max((size_t)1, mfu_ghost_.size() / mru_ghost_.size());
      mru_target_size_ = std::min(replacer_size_, mru_target_size_ + delta);
      mru_ghost_.erase(status->ghost_it_);
    }
    // 命中B2：说明MFU需要更多空间，减小p
    else {
      size_t delta = (mru_ghost_.empty()) ? 1 : std::max((size_t)1, mru_ghost_.size() / mfu_ghost_.size());
      mru_target_size_ = (mru_target_size_ >= delta) ? (mru_target_size_ - delta) : 0;
      mfu_ghost_.erase(status->ghost_it_);
    }
    // 从幽灵列表移除
    ghost_map_.erase(page_id);
    // 创建新状态放入MFU
    auto new_status = std::make_shared<FrameStatus>(page_id, frame_id, false, ArcStatus::MFU);
    mfu_.push_front(frame_id);
    new_status->alive_it_ = mfu_.begin();
    alive_map_[frame_id] = new_status;
    return;
  }
  // case4:完全未命中
  // 按照原论文，此处要维护幽灵列表的大小
  MaintainGhostSize();
  auto new_status = std::make_shared<FrameStatus>(page_id, frame_id, false, ArcStatus::MRU);
  mru_.push_front(frame_id);
  new_status->alive_it_ = mru_.begin();
  alive_map_[frame_id] = new_status;
}

/**
 * TODO(P1): Add implementation
 *
 * @brief Toggle whether a frame is evictable or non-evictable. This function also
 * controls replacer's size. Note that size is equal to number of evictable entries.
 *
 * If a frame was previously evictable and is to be set to non-evictable, then size should
 * decrement. If a frame was previously non-evictable and is to be set to evictable,
 * then size should increment.
 *
 * If frame id is invalid, throw an exception or abort the process.
 *
 * For other scenarios, this function should terminate without modifying anything.
 *
 * @param frame_id id of frame whose 'evictable' status will be modified
 * @param set_evictable whether the given frame is evictable or not
 */
void ArcReplacer::SetEvictable(frame_id_t frame_id, bool set_evictable) {
  std::lock_guard<std::mutex> lock(latch_);
  // 若frame_id根本不在内存里就无需处理
  auto it = alive_map_.find(frame_id);
  if (it == alive_map_.end()) return;
  auto status = it->second;
  // 状态没变无需处理
  if (status->evictable_ == set_evictable) return;
  // 更新状态
  status->evictable_ = set_evictable;
  if (set_evictable) {
    curr_size_++;
  } else {
    curr_size_--;
  }
}

/**
 * TODO(P1): Add implementation
 *
 * @brief Remove an evictable frame from replacer.
 * This function should also decrement replacer's size if removal is successful.
 *
 * Note that this is different from evicting a frame, which always remove the frame
 * decided by the ARC algorithm.
 *
 * If Remove is called on a non-evictable frame, throw an exception or abort the
 * process.
 *
 * If specified frame is not found, directly return from this function.
 *
 * @param frame_id id of frame to be removed
 */
void ArcReplacer::Remove(frame_id_t frame_id) {
  std::lock_guard<std::mutex> lock(latch_);
  if (alive_map_.count(frame_id)) {
    auto status = alive_map_[frame_id];
    // 若不可被踢出则报错
    if (!status->evictable_) throw std::runtime_error("Remove non-evictable frame");
    // 从T1或T2中删除
    if (status->arc_status_ == ArcStatus::MRU)
      mru_.erase(status->alive_it_);
    else
      mfu_.erase(status->alive_it_);
    // 从内存档案中删除
    alive_map_.erase(frame_id);
    curr_size_--;
  }
}

/**
 * TODO(P1): Add implementation
 *
 * @brief Return replacer's size, which tracks the number of evictable frames.
 *
 * @return size_t
 */
auto ArcReplacer::Size() -> size_t {
  std::lock_guard<std::mutex> lock(latch_);
  return curr_size_;
}

void ArcReplacer::MaintainGhostSize() {
  size_t t1 = mru_.size();
  size_t t2 = mfu_.size();
  size_t b1 = mru_ghost_.size();
  size_t b2 = mfu_ghost_.size();
  // 若T1+B1超过了最大容量，就删去B1里最后一个页面
  if (t1 + b1 >= replacer_size_) {
    if (!mru_ghost_.empty()) {
      ghost_map_.erase(mru_ghost_.back());
      mru_ghost_.pop_back();
    }
    // T1自己占满内存的情况会在Evict函数中被处理，此处无需处理
  } else if (t1 + t2 + b1 + b2 >= replacer_size_) {
    if (t1 + t2 + b1 + b2 >= 2 * replacer_size_) {
      if (!mfu_ghost_.empty()) {
        ghost_map_.erase(mfu_ghost_.back());
        mfu_ghost_.pop_back();
      }
    }
  }
}

}  // namespace bustub
