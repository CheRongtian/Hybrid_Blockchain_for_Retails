#ifndef SUPERMARKET_SNAPSHOT_SCHEDULER_HPP
#define SUPERMARKET_SNAPSHOT_SCHEDULER_HPP

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>

namespace supermarket::snapshot_scheduler
{
class SnapshotScheduler
{
public:
    using Cycle = std::function<void()>;
    using NextWake =
        std::function<std::optional<std::chrono::milliseconds>()>;

    SnapshotScheduler(NextWake next_wake, Cycle cycle);
    ~SnapshotScheduler();

    SnapshotScheduler(const SnapshotScheduler&) = delete;
    SnapshotScheduler& operator=(const SnapshotScheduler&) = delete;

    void start();
    void stop();
    void wake();

private:
    void run();

    NextWake next_wake_;
    Cycle cycle_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::thread worker_;
    bool running_ = false;
    bool stop_requested_ = false;
    bool wake_requested_ = false;
};
}

#endif
