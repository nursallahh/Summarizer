#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace smz {

// Thread-safe queue of events (each event is one JSON object).
// Worker threads push; the UI thread drains it once per frame.
//
// The queue is bounded so memory cannot grow without limit when the UI stops polling
// (e.g. the Unity editor is paused). When full, "droppable" events such as progress
// updates are discarded and counted; important events (started/finished) are always kept.
class EventQueue {
public:
    explicit EventQueue(std::size_t capacity = 100'000) : capacity_(capacity) {}

    void push(std::string event_json, bool droppable) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (droppable && events_.size() >= capacity_) {
            ++dropped_;
            return;
        }
        events_.push_back(std::move(event_json));
    }

    // Removes up to max_events events. Returns them in order plus the number of events
    // dropped since the previous drain.
    std::pair<std::vector<std::string>, std::int64_t> drain(std::size_t max_events) {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::size_t count = std::min(max_events, events_.size());
        std::vector<std::string> out;
        out.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            out.push_back(std::move(events_.front()));
            events_.pop_front();
        }
        const std::int64_t dropped = dropped_;
        dropped_ = 0;
        return {std::move(out), dropped};
    }

    std::size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return events_.size();
    }

private:
    mutable std::mutex mutex_;
    std::deque<std::string> events_;
    std::size_t capacity_;
    std::int64_t dropped_ = 0;
};

}  // namespace smz
