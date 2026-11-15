#include "job_runner.hpp"

#include "json_writer.hpp"

#include <exception>

namespace smz {

namespace {

const char* outcome_name(JobResult::Outcome outcome) {
    switch (outcome) {
        case JobResult::Outcome::Succeeded: return "succeeded";
        case JobResult::Outcome::Cancelled: return "cancelled";
        case JobResult::Outcome::Failed: return "failed";
    }
    return "failed";
}

}  // namespace

void JobContext::report_progress(std::int64_t done, std::int64_t total) {
    events_.push(JsonObject()
                     .add("type", "job_progress")
                     .add("job", id_)
                     .add("done", done)
                     .add("total", total)
                     .finish(),
                 /*droppable=*/true);
}

bool JobContext::sleep_for(std::chrono::milliseconds duration) const {
    // Sleep in short slices so cancellation is noticed quickly.
    constexpr auto kSlice = std::chrono::milliseconds(10);
    const auto deadline = std::chrono::steady_clock::now() + duration;
    while (!is_cancelled()) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            return true;
        }
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        std::this_thread::sleep_for(remaining < kSlice ? remaining : kSlice);
    }
    return false;
}

JobRunner::JobRunner(EventQueue& events, std::size_t worker_count) : events_(events) {
    if (worker_count == 0) {
        worker_count = 1;
    }
    workers_.reserve(worker_count);
    for (std::size_t i = 0; i < worker_count; ++i) {
        workers_.emplace_back([this] { worker_loop(); });
    }
}

JobRunner::~JobRunner() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        for (auto& [id, flag] : active_) {
            flag->store(true, std::memory_order_relaxed);
        }
    }
    wake_.notify_all();
    for (auto& worker : workers_) {
        worker.join();
    }
}

std::int64_t JobRunner::submit(std::string kind, JobFunction function) {
    std::int64_t id = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        id = next_id_++;
        auto flag = std::make_shared<std::atomic<bool>>(false);
        active_[id] = flag;
        pending_.push_back(Job{id, std::move(kind), std::move(function), std::move(flag)});
    }
    wake_.notify_one();
    return id;
}

bool JobRunner::cancel(std::int64_t job_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = active_.find(job_id);
    if (it == active_.end()) {
        return false;
    }
    it->second->store(true, std::memory_order_relaxed);
    return true;
}

void JobRunner::worker_loop() {
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] { return stopping_ || !pending_.empty(); });
            if (pending_.empty()) {
                return;  // stopping and nothing left to finish
            }
            job = std::move(pending_.front());
            pending_.pop_front();
        }
        run(job);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            active_.erase(job.id);
        }
    }
}

void JobRunner::run(Job& job) {
    events_.push(JsonObject().add("type", "job_started").add("job", job.id).add("kind", job.kind).finish(),
                 /*droppable=*/false);

    JobResult result;
    JobContext context(job.id, job.cancel_flag, events_);
    if (context.is_cancelled()) {
        // Cancelled (or shutting down) before it started.
        result.outcome = JobResult::Outcome::Cancelled;
    } else {
        // A job must never take the worker thread down with it.
        try {
            result = job.function(context);
        } catch (const std::exception& e) {
            result = JobResult{JobResult::Outcome::Failed, e.what()};
        } catch (...) {
            result = JobResult{JobResult::Outcome::Failed, "unknown error"};
        }
    }

    events_.push(JsonObject()
                     .add("type", "job_finished")
                     .add("job", job.id)
                     .add("outcome", outcome_name(result.outcome))
                     .add("message", result.message)
                     .finish(),
                 /*droppable=*/false);
}

}  // namespace smz
