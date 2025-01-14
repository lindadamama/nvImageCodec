/*
 * SPDX-FileCopyrightText: Copyright (c) 2022-2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
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
 */

#include "thread_pool.h"
#include <imgproc/device_guard.h>
#include <cstdlib>
#include <nvtx3/nvtx3.hpp>
#include <utility>
#include "log.h"

#ifdef _WIN32
    #include <windows.h>
#elif defined(__linux__)
    #include <pthread.h>
    #include <sys/syscall.h>
    #include <unistd.h>
#endif

namespace nvimgcodec {

namespace {

void setThreadName(const char* name) {
    // Limit the thread name to 15 characters plus null terminator
    char tmp_name[16];
    strncpy(tmp_name, name, sizeof(tmp_name) - 1);
    tmp_name[sizeof(tmp_name) - 1] = '\0';  // Ensure null termination

#ifdef _WIN32
    DWORD threadId = GetCurrentThreadId();
    nvtxNameOsThreadA(threadId, tmp_name);
    HRESULT hr = SetThreadDescription(GetCurrentThread(), std::wstring(tmp_name, tmp_name + strlen(tmp_name)).c_str());
    if (FAILED(hr)) {
        std::cerr << "Failed to set thread description on Windows." << std::endl;
    }
#elif defined(__linux__)
    nvtxNameOsThreadA(syscall(SYS_gettid), tmp_name);
    pthread_setname_np(pthread_self(), tmp_name);
#else
    std::cerr << "Setting thread name not supported on this platform." << std::endl;
#endif
}

}  // namespace

ThreadPool::ThreadPool(int num_thread, int device_id, bool set_affinity, const char* name)
    : threads_(num_thread)
    , running_(true)
    , work_complete_(true)
    , started_(false)
    , active_threads_(0)
{
    if (num_thread == 0) {
        throw std::runtime_error("Thread pool must have non-zero size");
    }
#if NVML_ENABLED
    // only for the CPU pipeline
    if (device_id != CPU_ONLY_DEVICE_ID) {
        nvml::Init();
    }
#endif
    // Start the threads in the main loop
    for (int i = 0; i < num_thread; ++i) {
        std::stringstream ss;
        ss << "[NVIMGCODEC][TP" << i << "]" << name;
        threads_[i] = std::thread(std::bind(&ThreadPool::threadMain, this, i, device_id, set_affinity, ss.str()));
    }
    tl_errors_.resize(num_thread);
}

ThreadPool::~ThreadPool()
{
    wait(false);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
    }
    condition_.notify_all();

    for (auto& thread : threads_) {
        thread.join();
    }
#if NVML_ENABLED
    nvml::Shutdown();
#endif
}

void ThreadPool::addWork(Work work)
{
    bool is_started = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        work_queue_.push_back(std::move(work));
        work_complete_ = false;
        is_started = started_;
    }
    if (is_started) {
        condition_.notify_one();
    }
}

void ThreadPool::run()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        started_ = true;
    }
    condition_.notify_all(); // other threads will be waken up if needed
}

void ThreadPool::wait(bool checkForErrors)
{
    std::unique_lock<std::mutex> lock(mutex_);
    if (!started_)
        return;
    completed_.wait(lock, [this] { return this->work_complete_; });
    started_ = false;
    if (checkForErrors) {
        // Check for errors
        for (size_t i = 0; i < threads_.size(); ++i) {
            if (!tl_errors_[i].empty()) {
                // Throw the first error that occurred
                std::stringstream ss;
                ss << "Error in thread " << i << ": " << tl_errors_[i].front();
                std::string error = ss.str();
                tl_errors_[i].pop();
                throw std::runtime_error(error);
            }
        }
    }
}

int ThreadPool::getThreadsNum() const
{
    return threads_.size();
}

std::vector<std::thread::id> ThreadPool::getThreadIds() const
{
    std::vector<std::thread::id> tids;
    tids.reserve(threads_.size());
    for (const auto& thread : threads_)
        tids.emplace_back(thread.get_id());
    return tids;
}

void ThreadPool::threadMain(int thread_id, int device_id, bool set_affinity, const std::string& name)
{
    setThreadName(name.c_str());
    DeviceGuard g(device_id);
    try {
#if NVML_ENABLED
        if (set_affinity) {
            const char* env_affinity = std::getenv("NVIMGCODEC_AFFINITY_MASK");
            int core = -1;
            if (env_affinity) {
                const auto& vec = string_split(env_affinity, ',');
                if ((size_t)thread_id < vec.size()) {
                    core = std::stoi(vec[thread_id]);
                } else {
                    NVIMGCODEC_LOG_WARNING(Logger::get_default(),
                        "NVIMGCODEC environment variable is set, "
                        "but does not have enough entries: thread_id (",
                        thread_id, ") vs #entries (", vec.size(), "). Ignoring...");
                }
            }
            nvml::SetCPUAffinity(core);
        }
#endif
    } catch (std::exception& e) {
        tl_errors_[thread_id].push(e.what());
    } catch (...) {
        tl_errors_[thread_id].push("Caught unknown exception");
    }

    while (running_) {
        // Block on the condition to wait for work
        std::unique_lock<std::mutex> lock(mutex_);
        condition_.wait(lock, [this] { return !running_ || (!work_queue_.empty() && started_); });
        // If we're no longer running, exit the run loop
        if (!running_)
            break;

        // Get work from the queue & mark
        // this thread as active
        auto it = work_queue_.begin();

        // if no suitable work for this thread, go to sleep
        if (it == work_queue_.end())
            continue;

        Work work = std::move(*it);
        work_queue_.erase(it);
        ++active_threads_;

        // Unlock the lock
        lock.unlock();

        // If an error occurs, we save it in tl_errors_. When
        // WaitForWork is called, we will check for any errors
        // in the threads and return an error if one occured.
        try {
            work(thread_id);
        } catch (std::exception& e) {
            lock.lock();
            tl_errors_[thread_id].push(e.what());
            lock.unlock();
        } catch (...) {
            lock.lock();
            tl_errors_[thread_id].push("Caught unknown exception");
            lock.unlock();
        }

        // Mark this thread as idle & check for complete work
        lock.lock();
        --active_threads_;
        if (work_queue_.empty() && active_threads_ == 0) {
            work_complete_ = true;
            lock.unlock();
            completed_.notify_one();
        }
    }
}

} // namespace nvimgcodec
