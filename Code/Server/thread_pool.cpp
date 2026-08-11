#include "thread_pool.hpp"

#include <algorithm>
#include <exception>
#include <iostream>
#include <new>
#include <utility>

std::size_t ThreadPool::normalized_worker_count(std::size_t requested) noexcept
{
    if(requested != 0) return requested;

    const unsigned int detected = std::thread::hardware_concurrency();
    const std::size_t available = detected == 0 ? 4 : detected;
    return std::max<std::size_t>(2, std::min<std::size_t>(8, available));
}

ThreadPool::ThreadPool(std::size_t worker_count, std::size_t max_queue_size)
    : work_item_pool_(sizeof(WorkItem), alignof(WorkItem),
                      std::max<std::size_t>(64, normalized_worker_count(worker_count) * 2)),
      max_queue_size_(std::max<std::size_t>(1, max_queue_size))
{
    const std::size_t count = normalized_worker_count(worker_count);
    workers_.reserve(count);
    for(std::size_t i = 0; i < count; ++i)
        workers_.emplace_back(&ThreadPool::worker_loop, this);
}

ThreadPool::~ThreadPool()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    condition_.notify_all();

    for(std::thread& worker : workers_)
    {
        if(worker.joinable()) worker.join();
    }
}

void ThreadPool::destroy_callable(CallableHolder* callable) noexcept
{
    if(!callable) return;
    callable->~CallableHolder();
    conmem::deallocate(callable, sizeof(CallableHolder));
}

bool ThreadPool::submit(Task task)
{
    if(!task) return false;

    void* callable_memory = conmem::allocate(sizeof(CallableHolder));
    if(!callable_memory) return false;

    CallableHolder* callable = nullptr;
    try
    {
        callable = new(callable_memory) CallableHolder{std::move(task)};
    }
    catch(...)
    {
        conmem::deallocate(callable_memory, sizeof(CallableHolder));
        return false;
    }

    void* item_memory = nullptr;
    try
    {
        item_memory = work_item_pool_.Allocate();
    }
    catch(...)
    {
        destroy_callable(callable);
        return false;
    }

    WorkItem* item = new(item_memory) WorkItem{callable};
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if(stopping_ || queue_.size() >= max_queue_size_)
        {
            item->~WorkItem();
            work_item_pool_.Free(item);
            destroy_callable(callable);
            return false;
        }
        queue_.push(item);
    }
    condition_.notify_one();
    return true;
}

std::size_t ThreadPool::worker_count() const noexcept
{
    return workers_.size();
}

void ThreadPool::worker_loop()
{
    while(true)
    {
        WorkItem* item = nullptr;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            condition_.wait(lock, [this] {
                return stopping_ || !queue_.empty();
            });

            if(stopping_ && queue_.empty()) return;
            item = queue_.front();
            queue_.pop();
        }

        CallableHolder* callable = item->callable;
        try
        {
            callable->task();
        }
        catch(const std::exception& error)
        {
            std::cerr << "Thread pool task failed: " << error.what() << '\n';
        }
        catch(...)
        {
            std::cerr << "Thread pool task failed with an unknown exception\n";
        }

        item->~WorkItem();
        work_item_pool_.Free(item);
        destroy_callable(callable);
    }
}
