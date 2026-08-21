#ifndef SUPERMARKET_SNAPSHOT_SCHEDULER_HPP
#define SUPERMARKET_SNAPSHOT_SCHEDULER_HPP

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace supermarket::snapshot_scheduler
{
class SnapshotScheduler
{
public:
    using Cycle = std::function<void()>;

    SnapshotScheduler(std::chrono::seconds interval, Cycle cycle);
    ~SnapshotScheduler();

    SnapshotScheduler(const SnapshotScheduler&) = delete;
    SnapshotScheduler& operator=(const SnapshotScheduler&) = delete;

    void start();
    void stop();

private:
    void run();

    std::chrono::seconds interval_;
    Cycle cycle_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::thread worker_;
    bool running_ = false;
    bool stop_requested_ = false;
};

std::chrono::seconds interval_from_environment();
}

#endif
