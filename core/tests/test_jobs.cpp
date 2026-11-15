// Tests for contexts, background jobs and events, through the public C API
// plus direct tests of the internal event queue and JSON writer.

#include "smz/smz_api.h"

#include "event_queue.hpp"
#include "json_writer.hpp"
#include "test_util.hpp"

#include <chrono>
#include <string>
#include <thread>

using test::check;
using test::count_occurrences;
using clock_type = std::chrono::steady_clock;

namespace {

// Polls until a job_finished event for `job_id` arrives or the timeout passes.
// Returns everything polled, concatenated.
std::string poll_until_finished(smz_context* ctx, std::int64_t job_id, std::chrono::milliseconds timeout) {
    const std::string finished = "\"type\":\"job_finished\",\"job\":" + std::to_string(job_id) + ",";
    const auto deadline = clock_type::now() + timeout;
    std::string all;
    while (clock_type::now() < deadline) {
        char* json = nullptr;
        if (smz_poll_events(ctx, 1000, &json) != SMZ_OK) {
            break;
        }
        all += json;
        smz_free_string(json);
        if (all.find(finished) != std::string::npos) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return all;
}

void test_argument_checks() {
    check(smz_create(nullptr) == SMZ_ERR_INVALID_ARGUMENT, "smz_create(NULL) is rejected");
    smz_destroy(nullptr);
    check(true, "smz_destroy(NULL) does not crash");

    char* json = nullptr;
    std::int64_t id = 0;
    check(smz_poll_events(nullptr, 10, &json) == SMZ_ERR_INVALID_ARGUMENT, "poll with NULL context is rejected");
    check(smz_start_demo_job(nullptr, 10, 10, &id) == SMZ_ERR_INVALID_ARGUMENT, "demo job with NULL context is rejected");
    check(smz_cancel_job(nullptr, 1) == SMZ_ERR_INVALID_ARGUMENT, "cancel with NULL context is rejected");

    smz_context* ctx = nullptr;
    check(smz_create(&ctx) == SMZ_OK && ctx != nullptr, "smz_create succeeds");
    check(smz_poll_events(ctx, 0, &json) == SMZ_ERR_INVALID_ARGUMENT, "poll with max_events = 0 is rejected");
    check(smz_poll_events(ctx, 10, nullptr) == SMZ_ERR_INVALID_ARGUMENT, "poll with NULL output is rejected");
    check(smz_start_demo_job(ctx, 0, 10, &id) == SMZ_ERR_INVALID_ARGUMENT, "demo job with 0 steps is rejected");
    check(smz_start_demo_job(ctx, 10, 0, &id) == SMZ_ERR_INVALID_ARGUMENT, "demo job with 0 steps/s is rejected");
    check(smz_cancel_job(ctx, 12345) == SMZ_ERR_NOT_FOUND, "cancelling an unknown job returns NOT_FOUND");
    smz_destroy(ctx);
}

void test_empty_poll() {
    smz_context* ctx = nullptr;
    smz_create(&ctx);
    char* json = nullptr;
    check(smz_poll_events(ctx, 10, &json) == SMZ_OK, "poll on a fresh context succeeds");
    check(std::string(json) == "{\"events\":[],\"dropped\":0,\"pending\":0}", "fresh context has no events");
    smz_free_string(json);
    smz_destroy(ctx);
}

void test_demo_job_completes() {
    smz_context* ctx = nullptr;
    smz_create(&ctx);
    std::int64_t id = 0;
    check(smz_start_demo_job(ctx, 100, 10'000, &id) == SMZ_OK && id > 0, "demo job starts and gets an id");

    const std::string all = poll_until_finished(ctx, id, std::chrono::seconds(5));
    check(count_occurrences(all, "\"type\":\"job_started\"") == 1, "exactly one job_started");
    check(count_occurrences(all, "\"type\":\"job_progress\"") == 100, "exactly 100 job_progress events");
    check(count_occurrences(all, "\"type\":\"job_finished\"") == 1, "exactly one job_finished");
    check(all.find("\"outcome\":\"succeeded\"") != std::string::npos, "job outcome is succeeded");
    check(all.find("job_started") < all.find("job_progress"), "job_started comes before progress");
    check(all.find("\"done\":100,\"total\":100") != std::string::npos, "last progress is 100/100");
    check(smz_cancel_job(ctx, id) == SMZ_ERR_NOT_FOUND, "a finished job cannot be cancelled");
    smz_destroy(ctx);
}

void test_rate_is_respected() {
    // 200 steps at 1000/s should take about 0.2 s, not finish instantly and not take seconds.
    smz_context* ctx = nullptr;
    smz_create(&ctx);
    std::int64_t id = 0;
    const auto start = clock_type::now();
    smz_start_demo_job(ctx, 200, 1000, &id);
    const std::string all = poll_until_finished(ctx, id, std::chrono::seconds(5));
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(clock_type::now() - start).count();
    check(count_occurrences(all, "\"type\":\"job_progress\"") == 200, "rate test: all 200 progress events arrive");
    check(elapsed_ms >= 150 && elapsed_ms < 2000, "rate test: 200 steps at 1000/s take about 0.2 s");
    smz_destroy(ctx);
}

void test_cancel() {
    smz_context* ctx = nullptr;
    smz_create(&ctx);
    std::int64_t id = 0;
    smz_start_demo_job(ctx, 1'000'000, 1000, &id);  // would take ~17 minutes
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    const auto start = clock_type::now();
    check(smz_cancel_job(ctx, id) == SMZ_OK, "running job can be cancelled");
    const std::string all = poll_until_finished(ctx, id, std::chrono::seconds(2));
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(clock_type::now() - start).count();
    check(all.find("\"outcome\":\"cancelled\"") != std::string::npos, "cancelled job reports outcome cancelled");
    check(elapsed_ms < 500, "cancellation is noticed within 0.5 s");
    smz_destroy(ctx);
}

void test_destroy_while_running() {
    smz_context* ctx = nullptr;
    smz_create(&ctx);
    std::int64_t id = 0;
    for (int i = 0; i < 5; ++i) {
        smz_start_demo_job(ctx, 1'000'000, 1000, &id);  // more jobs than workers: some still queued
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const auto start = clock_type::now();
    smz_destroy(ctx);
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(clock_type::now() - start).count();
    check(elapsed_ms < 1000, "destroy with running and queued jobs returns within 1 s");
}

void test_many_jobs() {
    smz_context* ctx = nullptr;
    smz_create(&ctx);
    std::int64_t last = 0;
    for (int i = 0; i < 10; ++i) {
        smz_start_demo_job(ctx, 50, 100'000, &last);
    }
    std::string all;
    const auto deadline = clock_type::now() + std::chrono::seconds(5);
    while (clock_type::now() < deadline && count_occurrences(all, "\"type\":\"job_finished\"") < 10) {
        char* json = nullptr;
        smz_poll_events(ctx, 1000, &json);
        all += json;
        smz_free_string(json);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    check(count_occurrences(all, "\"type\":\"job_finished\"") == 10, "10 jobs all finish");
    check(count_occurrences(all, "\"type\":\"job_progress\"") == 500, "10 jobs x 50 progress events all arrive");
    smz_destroy(ctx);
}

void test_event_queue_bounds() {
    smz::EventQueue queue(3);
    queue.push("{\"a\":1}", true);
    queue.push("{\"a\":2}", true);
    queue.push("{\"a\":3}", true);
    queue.push("{\"a\":4}", true);   // dropped: queue full and droppable
    queue.push("{\"a\":5}", false);  // kept: never drop important events
    auto [events, dropped] = queue.drain(10);
    check(events.size() == 4, "full queue keeps non-droppable events");
    check(dropped == 1, "full queue counts dropped progress events");
    check(events.back() == "{\"a\":5}", "important event is kept at the end");

    auto [again, dropped_again] = queue.drain(10);
    check(again.empty() && dropped_again == 0, "drain resets the queue and the dropped counter");
}

void test_json_escaping() {
    const std::string json = smz::JsonObject()
                                 .add("path", "C:\\Ödevler\\\"çizim\".pdf")
                                 .add("line", "a\nb\tc")
                                 .add("ctl", std::string("x\x01y"))
                                 .add("n", std::int64_t{-5})
                                 .finish();
    check(json == "{\"path\":\"C:\\\\Ödevler\\\\\\\"çizim\\\".pdf\",\"line\":\"a\\nb\\tc\",\"ctl\":\"x\\u0001y\",\"n\":-5}",
          "JSON strings are escaped and UTF-8 passes through");
}

}  // namespace

int main() {
    test_argument_checks();
    test_empty_poll();
    test_demo_job_completes();
    test_rate_is_respected();
    test_cancel();
    test_destroy_while_running();
    test_many_jobs();
    test_event_queue_bounds();
    test_json_escaping();
    return test::finish();
}
