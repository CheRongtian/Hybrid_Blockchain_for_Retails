#include "snapshot_scheduler.hpp"

#include <cerrno>
#include <cstdlib>
#include <limits>
#include <utility>

namespace supermarket::snapshot_scheduler
{
namespace
{
constexpr long long DEFAULT_INTERVAL_SECONDS = 120;

std::chrono::seconds safe_interval(std::chrono::seconds interval)
{
    return interval.count() > 0 ? interval : std::chrono::seconds(1);
}
}

SnapshotScheduler::SnapshotScheduler(std::chrono::seconds interval, Cycle cycle)
    : interval_(safe_interval(interval)), cycle_(std::move(cycle))
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

        std::unique_lock<std::mutex> lock(mutex_);
        if(condition_.wait_for(lock, interval_, [&] {
               return stop_requested_;
           }))
            return;
    }
}

std::chrono::seconds interval_from_environment()
{
    const char* raw = std::getenv("SNAPSHOT_AUTO_REFRESH_INTERVAL_SECONDS");
    if(!raw || *raw == '\0')
        return std::chrono::seconds(DEFAULT_INTERVAL_SECONDS);

    errno = 0;
    char* end = nullptr;
    const long long value = std::strtoll(raw, &end, 10);
    if(errno == ERANGE || end == raw || !end || *end != '\0' || value <= 0)
        return std::chrono::seconds(DEFAULT_INTERVAL_SECONDS);

    using Rep = std::chrono::seconds::rep;
    if(value > static_cast<long long>(std::numeric_limits<Rep>::max()))
        return std::chrono::seconds(DEFAULT_INTERVAL_SECONDS);
    return std::chrono::seconds(static_cast<Rep>(value));
}
}
