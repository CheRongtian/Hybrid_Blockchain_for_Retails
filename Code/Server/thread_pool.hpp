#ifndef SUPPLY_CHAIN_THREAD_POOL_HPP
#define SUPPLY_CHAIN_THREAD_POOL_HPP

#include "../ConMemPool/conmem_pool.hpp"
#include "../MemoryPool/mempool.hpp"

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

class ThreadPool
{
public:
    using Task = std::function<void()>;

    explicit ThreadPool(std::size_t worker_count = 0,
                        std::size_t max_queue_size = 256);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    bool submit(Task task);
    std::size_t worker_count() const noexcept;

private:
    struct CallableHolder
    {
        Task task;
    };

    struct WorkItem
    {
        CallableHolder* callable;
    };

    static std::size_t normalized_worker_count(std::size_t requested) noexcept;
    void worker_loop();
    static void destroy_callable(CallableHolder* callable) noexcept;

    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::queue<WorkItem*> queue_;
    MemoryPool work_item_pool_;
    std::vector<std::thread> workers_;
    const std::size_t max_queue_size_;
    bool stopping_ = false;
};

#endif
