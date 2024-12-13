/*************************************************************************
 * Copyright (C) [2019] by Cambricon, Inc. All rights reserved
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *************************************************************************/

#include "cnstream_frame.hpp"

#include <map>
#include <memory>
#include <string>

#include "cnstream_module.hpp"

namespace cnstream {

static std::mutex s_eos_lock_;
static std::map<std::string, std::atomic<bool>> s_stream_eos_map_;

static RwLock s_remove_lock_;
// TODO:这个map记录各stream_id是否可用还是是否移除呢？
static std::map<std::string, bool> s_stream_removed_map_;

bool CheckStreamEosReached(const std::string &stream_id, bool sync) {
  // 同步模式(sync=true)时，当stream_id在s_stream_eos_map_中且为非eos状态会一直循环等待其状态为eos
  if (sync) {
    while (1) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      std::lock_guard<std::mutex> guard(s_eos_lock_);
      auto iter = s_stream_eos_map_.find(stream_id);
      if (iter != s_stream_eos_map_.end()) {
        // stream_id对应流为eos状态从map中将其移除
        if (iter->second == true) {
          s_stream_eos_map_.erase(iter);
          LOGI(CORE) << "check stream eos reached, stream_id =  " << stream_id;
          return true;
        }
      } else {
        return false;
      }
    }
    return false;
  } else {
    std::lock_guard<std::mutex> guard(s_eos_lock_);
    auto iter = s_stream_eos_map_.find(stream_id);
    if (iter != s_stream_eos_map_.end()) {
      if (iter->second == true) {
        s_stream_eos_map_.erase(iter);
        return true;
      }
    }
    return false;
  }
}

/**
 * 逻辑梳理：
 *  1.如果s_stream_removed_map_包含stream_id这个key且value为false时删除该key-value对。
 *  2.除1以外的其他情况则执行：s_stream_removed_map_[stream_id] = value
 */
void SetStreamRemoved(const std::string &stream_id, bool value) {
  RwLockWriteGuard guard(s_remove_lock_);
  auto iter = s_stream_removed_map_.find(stream_id);
  if (iter != s_stream_removed_map_.end()) {
    if (value != true) {
      s_stream_removed_map_.erase(iter);
      return;
    }
    iter->second = true;
  } else {
    s_stream_removed_map_[stream_id] = value;
  }
  // LOGI(CORE) << "_____SetStreamRemoved " << stream_id << ":" << s_stream_removed_map_[stream_id];
}

// 检查stream_id对应流状态
bool IsStreamRemoved(const std::string &stream_id) {
  RwLockReadGuard guard(s_remove_lock_);
  auto iter = s_stream_removed_map_.find(stream_id);
  if (iter != s_stream_removed_map_.end()) {
    // LOGI(CORE) << "_____IsStreamRemoved " << stream_id << ":" << s_stream_removed_map_[stream_id];
    return s_stream_removed_map_[stream_id];
  }
  return false;
}

std::shared_ptr<CNFrameInfo> CNFrameInfo::Create(const std::string &stream_id, bool eos,
                                                 std::shared_ptr<CNFrameInfo> payload) {
  if (stream_id == "") {
    LOGE(CORE) << "CNFrameInfo::Create() stream_id is empty string.";
    return nullptr;
  }
  std::shared_ptr<CNFrameInfo> ptr(new (std::nothrow) CNFrameInfo());
  if (!ptr) {
    LOGE(CORE) << "CNFrameInfo::Create() new CNFrameInfo failed.";
    return nullptr;
  }
  ptr->stream_id = stream_id;
  ptr->payload = payload;
  if (eos) {
    ptr->flags |= static_cast<size_t>(cnstream::CNFrameFlag::CN_FRAME_FLAG_EOS);
    if (!ptr->payload) {
      std::lock_guard<std::mutex> guard(s_eos_lock_);
      s_stream_eos_map_[stream_id] = false;
    }
    return ptr;
  }

  return ptr;
}

CNS_IGNORE_DEPRECATED_PUSH
CNFrameInfo::~CNFrameInfo() {
  if (this->IsEos()) {
    if (!this->payload) {
      std::lock_guard<std::mutex> guard(s_eos_lock_);
      s_stream_eos_map_[stream_id] = true;
    }
    return;
  }
}
CNS_IGNORE_DEPRECATED_POP

/**
 * @brief 这里将data无需经过的module对应位设置为1, pipeline中不存在的module对应位设置为0，pipleline中data需要流向的module
 *  对应位设置为0.
 * 
 * @example [framework中为64位长，这里用8位做示例]
 *  pipieline所有模块构成的mask: all_modules_mask= 00101101
 *  graph中的某一个head/root node对应的mask: route_mask = 00001100
 *  流过该head/root node后对应的modules_mask = all_modules_mask ^ route_mask (即：00100001)
 */
void CNFrameInfo::SetModulesMask(uint64_t mask) {
  RwLockWriteGuard guard(mask_lock_);
  modules_mask_ = mask;
}

uint64_t CNFrameInfo::GetModulesMask() {
  // 加锁主要是当data同时被多条支路的module处理时存在线程不安全问题
  RwLockReadGuard guard(mask_lock_);
  return modules_mask_;
}

// NOTE: 标记data已流过对应module
uint64_t CNFrameInfo::MarkPassed(Module *module) {
  RwLockWriteGuard guard(mask_lock_);
  modules_mask_ |= (uint64_t)1 << module->GetId();
  return modules_mask_;
}

}  // namespace cnstream
