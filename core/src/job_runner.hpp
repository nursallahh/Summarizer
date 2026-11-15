#pragma once

#include "event_queue.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace smz {

// Handed to a running job. Lets it report progress and notice cancellation.
class JobContext {
public:
    JobContext(std::int64_t id, std::shared_ptr<std::atomic<bool>> cancel_flag, EventQueue& events)
        : id_(id), cancel_flag_(std::move(cancel_flag)), events_(events) {}

    std::int64_t id() const { return id_; }

    bool is_cancelled() const { return cancel_flag_->load(std::memory_order_relaxed); }

    void report_progress(std::int64_t done, std::int64_t total);

    // Emits a job-specific event (a complete JSON object). Never dropped, so use it for results,
    // not for high-frequency updates.
    void emit(std::string event_json) { events_.push(std::move(event_json), /*droppable=*/false); }

    // Sleeps up to `duration`, waking early if the job is cancelled.
    // Returns false if the job was cancelled.
    bool sleep_for(std::chrono::milliseconds duration) const;

private:
    std::int64_t id_;
    std::shared_ptr<std::atomic<bool>> cancel_flag_;
    EventQueue& events_;
};

// What a job reports when it ends.
struct JobResult {
    enum class Outcome { Succeeded, Cancelled, Failed };
    Outcome outcome = Outcome::Succeeded;
    std::string message;
};

using JobFunction = std::function<JobResult(JobContext&)>;

// Runs jobs on a fixed pool of worker threads.
// Every job emits job_started, any number of job_progress, and exactly one job_finished event.
// The destructor cancels all jobs and waits for the workers to exit.
class JobRunner {
public:
    JobRunner(EventQueue& events, std::size_t worker_count);
    ~JobRunner();

    JobRunner(const JobRunner&) = delete;
    JobRunner& operator=(const JobRunner&) = delete;

    // Queues a job and returns its id (ids start at 1).
    std::int64_t submit(std::string kind, JobFunction function);

    // Requests cancellation. Returns false if the job is unknown or already finished.
    bool cancel(std::int64_t job_id);

private:
    struct Job {
        std::int64_t id;
        std::string kind;
        JobFunction function;
        std::shared_ptr<std::atomic<bool>> cancel_flag;
    };

    void worker_loop();
    void run(Job& job);

    EventQueue& events_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<Job> pending_;
    std::unordered_map<std::int64_t, std::shared_ptr<std::atomic<bool>>> active_;  // queued or running
    std::int64_t next_id_ = 1;
    bool stopping_ = false;
    std::vector<std::thread> workers_;
};

}  // namespace smz
