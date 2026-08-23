#include "snapshot_scheduler.hpp"

#include <utility>

namespace supermarket::snapshot_scheduler
{
SnapshotScheduler::SnapshotScheduler(NextWake next_wake, Cycle cycle)
    : next_wake_(std::move(next_wake)), cycle_(std::move(cycle))
{
}

SnapshotScheduler::~SnapshotScheduler()
{
    stop();
}

void SnapshotScheduler::start()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if(running_) return;
    stop_requested_ = false;
    running_ = true;
    worker_ = std::thread(&SnapshotScheduler::run, this);
}

void SnapshotScheduler::stop()
{
    std::thread worker;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if(!running_) return;
        stop_requested_ = true;
        condition_.notify_all();
        worker = std::move(worker_);
    }

    if(worker.joinable()) worker.join();
    std::lock_guard<std::mutex> lock(mutex_);
    running_ = false;
}

void SnapshotScheduler::wake()
{
    std::lock_guard<std::mutex> lock(mutex_);
    wake_requested_ = true;
    condition_.notify_all();
}

void SnapshotScheduler::run()
{
    while(true)
    {
        try
        {
            if(cycle_) cycle_();
        }
        catch(...)
        {
            // A failed refresh must not terminate the control server.
        }

        std::optional<std::chrono::milliseconds> delay;
        try
        {
            if(next_wake_) delay = next_wake_();
        }
        catch(...)
        {
            // A failed schedule lookup leaves the worker waiting for an
            // explicit wake-up instead of terminating the control server.
        }

        std::unique_lock<std::mutex> lock(mutex_);
        if(stop_requested_) return;
        if(wake_requested_)
        {
            wake_requested_ = false;
            continue;
        }

        const auto awakened = delay
            ? condition_.wait_for(lock, *delay, [&] {
                  return stop_requested_ || wake_requested_;
              })
            : (condition_.wait(lock, [&] {
                   return stop_requested_ || wake_requested_;
               }), true);
        if(stop_requested_) return;
        if(awakened) wake_requested_ = false;
    }
}
}
