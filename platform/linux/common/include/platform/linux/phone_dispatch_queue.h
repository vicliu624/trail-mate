#pragma once
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>

namespace platform::linux_runtime
{
// Worker-to-application rendezvous. Only pump() executes application operations.
// Stop and pump belong to the application thread; call belongs to workers.
class PhoneDispatchQueue
{
  public:
    bool call(std::function<bool()> operation, std::chrono::milliseconds timeout = std::chrono::seconds(2))
    {
        auto request = std::make_shared<Request>();
        request->operation = std::move(operation);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopped_ || queued_.size() >= 16) return false;
            queued_.push_back(request);
        }
        std::unique_lock<std::mutex> lock(request->mutex);
        if (!request->changed.wait_for(lock, timeout, [&]
                                       { return request->state != State::Queued; }))
        {
            request->state = State::Cancelled;
            return false; // A cancelled operation must never run on a later UI tick.
        }
        // Once started, retain ownership until completion; returning a timeout here
        // would permit an acknowledged failure to mutate state later or use freed captures.
        request->changed.wait(lock, [&]
                              { return request->state != State::Running; });
        return request->state == State::Done && request->result;
    }
    size_t pump(size_t maximum = 8)
    {
        size_t processed = 0;
        while (processed < maximum)
        {
            std::shared_ptr<Request> request;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (queued_.empty()) break;
                request = std::move(queued_.front());
                queued_.pop_front();
            }
            ++processed;
            {
                std::lock_guard<std::mutex> lock(request->mutex);
                if (request->state != State::Queued) continue;
                request->state = State::Running;
            }
            request->changed.notify_all();
            bool result = false;
            try
            {
                result = request->operation && request->operation();
            }
            catch (...)
            {
            }
            {
                std::lock_guard<std::mutex> lock(request->mutex);
                request->result = result;
                request->state = State::Done;
            }
            request->changed.notify_all();
        }
        return processed;
    }
    void stop()
    {
        std::deque<std::shared_ptr<Request>> pending;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopped_ = true;
            pending.swap(queued_);
        }
        for (auto& request : pending)
        {
            {
                std::lock_guard<std::mutex> lock(request->mutex);
                request->state = State::Cancelled;
            }
            request->changed.notify_all();
        }
    }

  private:
    enum class State
    {
        Queued,
        Running,
        Done,
        Cancelled
    };
    struct Request
    {
        std::mutex mutex;
        std::condition_variable changed;
        std::function<bool()> operation;
        State state = State::Queued;
        bool result = false;
    };
    std::mutex mutex_;
    std::deque<std::shared_ptr<Request>> queued_;
    bool stopped_ = false;
};
} // namespace platform::linux_runtime
