/******************************************************************************
 * Copyright 2018 The Apollo Authors. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *****************************************************************************/

#include "cyber/scheduler/policy/choreography.h"

#include <unordered_map>
#include <utility>
#include <vector>

#include "cyber/common/types.h"
#include "cyber/event/perf_event_cache.h"
#include "cyber/scheduler/processor.h"

namespace apollo {
namespace cyber {
namespace scheduler {

using apollo::cyber::croutine::RoutineState;
using apollo::cyber::event::PerfEventCache;
using apollo::cyber::event::SchedPerf;

std::shared_ptr<CRoutine> ChoreographyContext::NextRoutine() {
  if (unlikely(stop_)) {
    return nullptr;
  }

  std::lock_guard<std::mutex> lock(mtx_cr_queue_);
  for (auto it = cr_queue_.begin(); it != cr_queue_.end();) {
    auto cr = it->second;
    // FIXME: Remove Acquire() and Release() if there is no race condtion.
    if (!cr->Acquire()) {
      continue;
    }

    if (cr->state() == RoutineState::FINISHED) {
      it = cr_queue_.erase(it);
      continue;
    }

    if (cr->UpdateState() == RoutineState::READY) {
      PerfEventCache::Instance()->AddSchedEvent(SchedPerf::NEXT_RT, cr->id(),
                                                cr->processor_id());
      return cr;
    }

    cr->Release();
    ++it;
  }

  notified_.clear();
  return nullptr;
}

bool ChoreographyContext::Enqueue(const std::shared_ptr<CRoutine>& cr) {
  PerfEventCache::Instance()->AddSchedEvent(SchedPerf::RT_CREATE, cr->id(),
                                            cr->processor_id());
  std::lock_guard<std::mutex> lk(mtx_cr_queue_);
  cr_queue_.emplace(cr->priority(), cr);
  return true;
}

void ChoreographyContext::Notify() {
  if (!notified_.test_and_set(std::memory_order_acquire)) {
    cv_wq_.notify_one();
    return;
  }
}

void ChoreographyContext::Wait() {
  std::unique_lock<std::mutex> lk(mtx_wq_);
  cv_wq_.wait_for(lk, std::chrono::milliseconds(1));
}

void ChoreographyContext::RemoveCRoutine(uint64_t crid) {
  std::lock_guard<std::mutex> lock(mtx_cr_queue_);
  for (auto it = cr_queue_.begin(); it != cr_queue_.end();) {
    auto cr = it->second;
    if (cr->id() == crid) {
      cr->Stop();
      it = cr_queue_.erase(it);
      cr->Release();
      return;
    }

    cr->Release();
    ++it;
  }
}
}  // namespace scheduler
}  // namespace cyber
}  // namespace apollo
