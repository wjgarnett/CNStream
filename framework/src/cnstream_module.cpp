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
#include "cnstream_module.hpp"

#include <map>
#include <memory>
#include <string>
#include <thread>

#include "cnstream_pipeline.hpp"
#include "profiler/pipeline_profiler.hpp"

namespace cnstream {

Module::~Module() {
  RwLockReadGuard guard(container_lock_);
  if (container_) {
    container_->ReturnModuleIdx(id_);
  }
}

void Module::SetContainer(Pipeline* container) {
  if (container) {
    {
      RwLockWriteGuard guard(container_lock_);
      container_ = container;
    }
    GetId();
  } else {
    RwLockWriteGuard guard(container_lock_);
    container_ = nullptr;
    id_ = kInvalidModuleId;
  }
}

size_t Module::GetId() {
  // 确保只有效分配一次id,保证其唯一性
  if (id_ == kInvalidModuleId) {
    RwLockReadGuard guard(container_lock_);
    if (container_) id_ = container_->GetModuleIdx();
  }
  return id_;
}

bool Module::PostEvent(EventType type, const std::string& msg) {
  Event event;
  // TODO:这里为啥没有指定thread_id字段？
  event.type = type;
  event.message = msg;
  event.module_name = name_;

  return PostEvent(event);
}

bool Module::PostEvent(Event e) {
  RwLockReadGuard guard(container_lock_);
  if (container_) {
    return container_->GetEventBus()->PostEvent(e);
  } else {
    LOGW(CORE) << "[" << GetName() << "] module's container is not set";
    return false;
  }
}

/**
 * NOTE: Module处理数据向下游传输有两种方式：
 *  1.由框架负责传输，DoTransmitData()是默认的数据传输实现
 *  2.由Module::process负责传输，此时Module::hasTransmit_为true
 */
int Module::DoTransmitData(std::shared_ptr<CNFrameInfo> data) {
  if (data->IsEos() && data->payload && IsStreamRemoved(data->stream_id)) {
    // FIMXE
    // NOTE: 这里应该认为stream_id到解码结束不可用，需要将其从map中删除了
    SetStreamRemoved(data->stream_id, false);
  }
  RwLockReadGuard guard(container_lock_);

  // TODO: 啥意思？
  if (container_) {
    return container_->ProvideData(this, data);
  } else {
    // TODO:啥时候Module实例不属于任何container_???
    if (HasTransmit()) NotifyObserver(data);
    return 0;
  }
}

/**
 * NOTE:
 *  DoProcess由基类实现，其具体实现将调用派生类的Process方法
 *  DoProcess将根据Module是否具有传输data的权利及流状态情形做适当处理
 */
int Module::DoProcess(std::shared_ptr<CNFrameInfo> data) {
  bool removed = IsStreamRemoved(data->stream_id);
  if (!removed) {
    // For the case that module is implemented by a pipeline
    if (data->payload && IsStreamRemoved(data->payload->stream_id)) {
      SetStreamRemoved(data->stream_id, true);
      removed = true;
    }
  }

  if (!HasTransmit()) { // pipeline传输data情形
    if (!data->IsEos()) {
      if (!removed) {
        int ret = Process(data);
        if (ret != 0) {
          return ret;
        }
      }
      return DoTransmitData(data);
    } else {
      this->OnEos(data->stream_id);
      return DoTransmitData(data);
    }
  } else { // module传输data情形
    if (removed) {
      data->flags |= static_cast<size_t>(CNFrameFlag::CN_FRAME_FLAG_REMOVED);
    }
    return Process(data); // TODO:此处Module有传输data的权利应该是直接在Process内实现了传输?
  }
  return -1;
}

// NOTE: Module::TransmitData方法似乎没有直接被调用啊，SourceModule::SendData才会调用该方法
bool Module::TransmitData(std::shared_ptr<CNFrameInfo> data) {
  if (!HasTransmit()) {
    return true;
  }
  if (!DoTransmitData(data)) {
    return true;
  }
  return false;
}

ModuleProfiler* Module::GetProfiler() {
  RwLockReadGuard guard(container_lock_);
  if (container_ && container_->GetProfiler()) return container_->GetProfiler()->GetModuleProfiler(GetName());
  return nullptr;
}

ModuleFactory* ModuleFactory::factory_ = nullptr;

}  // namespace cnstream
