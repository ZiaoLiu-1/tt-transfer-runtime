#include "tt_transfer/cpu_backend.hpp"
#include "tt_transfer/runtime.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <barrier>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <iostream>
#include <latch>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {
using namespace std::chrono_literals;
using tt_transfer::Backend;
using tt_transfer::Bytes;
using tt_transfer::CpuBackend;
using tt_transfer::Runtime;
using tt_transfer::Ticket;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

template <class Predicate>
void await(Predicate predicate, const std::string& description) {
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (!predicate()) {
        require(std::chrono::steady_clock::now() < deadline, "timeout: " + description);
        std::this_thread::yield();
    }
}

template <class Future>
void ready(Future& future, const std::string& description) {
    require(future.wait_for(2s) == std::future_status::ready, "timeout: " + description);
}

Bytes result(Ticket& ticket) {
    ready(ticket.completion, "accepted request completion");
    return ticket.completion.get();
}

template <class Exception, class Callable>
void throws(Callable callable, const std::string& description) {
    try {
        callable();
    } catch (const Exception&) {
        return;
    }
    throw std::runtime_error("expected exception: " + description);
}

Bytes pattern(std::size_t count, unsigned seed) {
    Bytes bytes(count);
    for (std::size_t i = 0; i < count; ++i) {
        bytes[i] = static_cast<std::byte>((seed + i * 37U) % 256U);
    }
    return bytes;
}

struct InjectedFailure : std::runtime_error {
    InjectedFailure() : std::runtime_error("ordinary backend failure") {}
};

enum class Operation { write, read, fence };
struct Observation {
    Operation operation;
    std::size_t offset;
    std::size_t count;
    std::thread::id owner;
};

// The owner blocks at one selected hook. A latch proves it entered the hook;
// release uses a condition variable, and no test depends on a timed sleep.
struct Probe {
    explicit Probe(std::size_t gate = 0, std::size_t fail = 0, std::size_t short_read = 0)
        : gate_on(gate), fail_on(fail), short_read_on(short_read) {}
    const std::size_t gate_on;
    const std::size_t fail_on;
    const std::size_t short_read_on;
    std::latch entered{1};
    std::mutex mutex;
    std::condition_variable changed;
    bool released = false;
    std::vector<Observation> observations;
    std::atomic<std::size_t> calls{0};
    std::atomic<unsigned> active{0};
    std::atomic<bool> concurrent{false};
    std::atomic<bool> destroyed{false};
    std::atomic<bool> destroyed_while_active{false};

    void release() {
        {
            std::lock_guard lock(mutex);
            released = true;
        }
        changed.notify_all();
    }
    void await_entry() { await([this] { return entered.try_wait(); }, "backend gate entry"); }
    std::vector<Observation> trace() {
        std::lock_guard lock(mutex);
        return observations;
    }
};

class ProbeBackend final : public Backend {
public:
    explicit ProbeBackend(std::shared_ptr<Probe> probe, std::size_t bytes = 128)
        : probe_(std::move(probe)), memory_(bytes) {}
    ~ProbeBackend() override {
        probe_->destroyed_while_active.store(probe_->active.load() != 0);
        probe_->destroyed.store(true);
    }
    std::size_t size() const noexcept override { return memory_.size(); }
    void write(std::size_t offset, std::span<const std::byte> bytes) override {
        Hook hook(*probe_, Operation::write, offset, bytes.size());
        memory_.write(offset, bytes);
    }
    Bytes read(std::size_t offset, std::size_t bytes) override {
        Hook hook(*probe_, Operation::read, offset, bytes);
        auto output = memory_.read(offset, bytes);
        if (hook.number == probe_->short_read_on && !output.empty()) output.pop_back();
        return output;
    }
    void fence() override { Hook hook(*probe_, Operation::fence, 0, 0); }

private:
    struct Hook {
        Probe& probe;
        const std::size_t number;
        Hook(Probe& state, Operation operation, std::size_t offset, std::size_t count)
            : probe(state), number(probe.calls.fetch_add(1) + 1) {
            if (probe.active.fetch_add(1) != 0) probe.concurrent.store(true);
            try {
                std::unique_lock lock(probe.mutex);
                probe.observations.push_back({operation, offset, count, std::this_thread::get_id()});
                if (number == probe.gate_on) {
                    probe.entered.count_down();
                    probe.changed.wait(lock, [&] { return probe.released; });
                }
                if (number == probe.fail_on) throw InjectedFailure{};
            } catch (...) {
                probe.active.fetch_sub(1);
                throw;
            }
        }
        ~Hook() { probe.active.fetch_sub(1); }
    };
    std::shared_ptr<Probe> probe_;
    CpuBackend memory_;
};

// Release the backend before either close/join or test-worker destruction, also
// on assertion failure. packaged_task stores worker exceptions in its future.
// The CTest process timeout remains the outer guard against a runtime deadlock.
struct Harness {
    std::shared_ptr<Probe> probe;
    Runtime runtime;
    std::vector<std::thread> workers;

    Harness(std::size_t capacity, std::shared_ptr<Probe> state = std::make_shared<Probe>())
        : probe(std::move(state)), runtime(std::make_unique<ProbeBackend>(probe), capacity) {}
    ~Harness() {
        probe->release();
        runtime.close_and_drain();
        for (auto& worker : workers) worker.join();
    }
    template <class Callable>
    auto spawn(Callable callable) {
        using Return = std::invoke_result_t<Callable>;
        std::packaged_task<Return()> task(std::move(callable));
        auto future = task.get_future();
        workers.emplace_back(std::move(task));
        return future;
    }
};

void cpu_bytes() {
    Runtime runtime(std::make_unique<CpuBackend>(8192), 4);
    auto written = runtime.write(13, pattern(1024, 91));
    auto read = runtime.read(0, 1041);
    require(result(written).empty(), "write must return an empty completion");
    const auto actual = result(read);
    // Build the whole expected region independently, including untouched guards.
    std::array<std::byte, 1041> expected{};
    for (std::size_t i = 13; i < 1037; ++i) {
        expected[i] = static_cast<std::byte>((91U + (i - 13) * 37U) % 256U);
    }
    require(actual.size() == expected.size(), "CPU read byte count");
    require(std::equal(actual.begin(), actual.end(), expected.begin()), "CPU full bytes and guards");
    runtime.close_and_drain();
    require(runtime.stats().succeeded == 2, "both CPU requests completed");
}

void intermediate_order() {
    Runtime runtime(std::make_unique<CpuBackend>(), 4);
    const Bytes a{std::byte{0x01}, std::byte{0x7f}, std::byte{0x80}, std::byte{0xff}};
    const Bytes b{std::byte{0xff}, std::byte{0x00}, std::byte{0x12}, std::byte{0x34}};
    auto wa = runtime.write(64, a);
    auto ra = runtime.read(64, a.size());
    auto wb = runtime.write(64, b);
    auto rb = runtime.read(64, b.size());
    runtime.close_and_drain();
    require(wa.sequence < ra.sequence && ra.sequence < wb.sequence && wb.sequence < rb.sequence,
            "sequential accepted order");
    require(result(wa).empty() && result(wb).empty(), "write completion values");
    require(result(ra) == a, "intermediate read must observe A before B");
    require(result(rb) == b, "final read must observe B");
}

struct RecordedRequest {
    Operation operation;
    std::size_t offset;
    Bytes payload;
    Ticket ticket;
};

void two_producer_sequence_oracle() {
    // Hold the owner while producers issue six controlled concurrent rounds.
    // Each round finishes both admissions before the following round begins.
    std::barrier rounds(2);
    Harness harness(16, std::make_shared<Probe>(1));
    auto initial_fence = harness.runtime.fence();
    harness.probe->await_entry();
    auto producer = [&](unsigned id) {
        std::vector<RecordedRequest> requests;
        try {
            for (unsigned round = 0; round < 6; ++round) {
                rounds.arrive_and_wait();
                const std::size_t offset = id * 4U; // overlapping eight-byte ranges
                if ((round + id) % 2 == 0) {
                    auto payload = pattern(8, 17U + id * 67U + round * 13U);
                    auto ticket = harness.runtime.write(offset, payload);
                    requests.push_back({Operation::write, offset, std::move(payload), std::move(ticket)});
                } else {
                    auto ticket = harness.runtime.read(0, 12);
                    requests.push_back({Operation::read, 0, {}, std::move(ticket)});
                }
            }
        } catch (...) {
            rounds.arrive_and_drop();
            throw;
        }
        return requests;
    };
    auto first = harness.spawn([&] { return producer(0); });
    auto second = harness.spawn([&] { return producer(1); });
    ready(first, "producer zero admission rounds");
    ready(second, "producer one admission rounds");
    auto requests = first.get();
    auto other = second.get();
    for (auto& request : other) requests.push_back(std::move(request));
    std::sort(requests.begin(), requests.end(), [](const auto& a, const auto& b) {
        return a.ticket.sequence < b.ticket.sequence;
    });
    harness.probe->release();
    harness.runtime.close_and_drain();
    require(result(initial_fence).empty(), "initial fence completion");
    std::array<std::byte, 12> oracle{};
    std::uint64_t expected_sequence = initial_fence.sequence + 1;
    for (auto& request : requests) {
        require(request.ticket.sequence == expected_sequence++, "unique contiguous acceptance sequences");
        auto actual = result(request.ticket);
        if (request.operation == Operation::write) {
            require(actual.empty(), "producer write completion");
            for (std::size_t i = 0; i < request.payload.size(); ++i) {
                oracle[request.offset + i] = request.payload[i];
            }
        } else {
            require(actual.size() == oracle.size(), "producer read size");
            require(std::equal(actual.begin(), actual.end(), oracle.begin()),
                    "concurrent read must match accepted-sequence oracle");
        }
    }
    const auto stats = harness.runtime.stats();
    require(stats.accepted == 13 && stats.succeeded == 13 && stats.failed == 0,
            "all controlled producer requests completed exactly once");
    const auto trace = harness.probe->trace();
    require(trace.size() == 13, "one backend hook per accepted request");
    for (std::size_t i = 0; i < requests.size(); ++i) {
        require(trace[i + 1].operation == requests[i].operation && trace[i + 1].offset == requests[i].offset,
                "backend hook order must match accepted sequence");
        require(trace[i + 1].owner == trace[0].owner && trace[i + 1].owner != std::this_thread::get_id(),
                "concurrent producers still use one dedicated backend owner");
    }
    require(!harness.probe->concurrent.load(), "producer requests never overlap backend hooks");
}

void capacity_one_blocks_then_progresses() {
    Harness harness(1, std::make_shared<Probe>(1));
    auto executing = harness.runtime.write(0, pattern(8, 5));
    harness.probe->await_entry();
    auto pending = harness.runtime.read(0, 8);
    auto submission = harness.spawn([&] { return harness.runtime.fence(); });
    await([&] { return harness.runtime.stats().waiting_submitters == 1; }, "Q=1 waiting producer");
    const auto blocked = harness.runtime.stats();
    require(blocked.queued == 1 && blocked.high_water == 1 && blocked.executing && blocked.accepted == 2,
            "Q queued plus one executing is the resource bound");
    require(submission.wait_for(0s) == std::future_status::timeout, "full queue admission must block");
    harness.probe->release();
    ready(submission, "blocked producer resumes after owner pops");
    auto resumed = submission.get();
    harness.runtime.close_and_drain();
    require(result(executing).empty() && result(pending) == pattern(8, 5) && result(resumed).empty(),
            "all Q=1 requests complete correctly");
    const auto done = harness.runtime.stats();
    require(done.accepted == 3 && done.succeeded == 3 && done.high_water == 1 && done.waiting_submitters == 0,
            "Q=1 bound and resumed-producer accounting");
}

void close_drains_and_wakes() {
    Harness harness(1, std::make_shared<Probe>(1));
    auto executing = harness.runtime.write(0, pattern(8, 44));
    harness.probe->await_entry();
    auto pending = harness.runtime.read(0, 8);
    auto submission = harness.spawn([&] { return harness.runtime.write(0, pattern(8, 99)); });
    auto other_submission = harness.spawn([&] { return harness.runtime.fence(); });
    await([&] { return harness.runtime.stats().waiting_submitters == 2; }, "two producers waiting before close");
    auto closing = harness.spawn([&] { harness.runtime.close_and_drain(); });
    await([&] { return harness.runtime.stats().closed; }, "close sets terminal admission state");
    ready(submission, "close wakes blocked admission without backend progress");
    ready(other_submission, "close wakes every blocked admission without backend progress");
    throws<std::runtime_error>([&] { (void)submission.get(); }, "closed blocked submission");
    throws<std::runtime_error>([&] { (void)other_submission.get(); }, "other closed blocked submission");
    throws<std::runtime_error>([&] { (void)harness.runtime.fence(); }, "new request after close");
    require(closing.wait_for(0s) == std::future_status::timeout, "close must wait for gated executing work");
    require(pending.completion.wait_for(0s) == std::future_status::timeout, "pending read has not bypassed owner");
    harness.probe->release();
    ready(closing, "close joins owner after drain");
    closing.get();
    require(result(executing).empty() && result(pending) == pattern(8, 44), "close preserves accepted work");
    harness.runtime.close_and_drain();
    const auto stats = harness.runtime.stats();
    require(stats.accepted == 2 && stats.succeeded == 2 && stats.failed == 0 && stats.closed &&
                stats.queued == 0 && !stats.executing && stats.waiting_submitters == 0,
            "close leaves no accepted or blocked work unfinished");
}

void ordinary_failure_finishes_every_accepted_request() {
    Harness harness(3, std::make_shared<Probe>(2, 2));
    auto good = harness.runtime.write(0, pattern(8, 71));
    require(result(good).empty(), "request before injection succeeds");
    auto current = harness.runtime.read(0, 8);
    harness.probe->await_entry();
    auto pending_one = harness.runtime.fence();
    auto pending_two = harness.runtime.write(0, pattern(8, 21));
    auto pending_three = harness.runtime.read(0, 8);
    auto submission = harness.spawn([&] { return harness.runtime.fence(); });
    auto other_submission = harness.spawn([&] { return harness.runtime.read(0, 8); });
    await([&] { return harness.runtime.stats().waiting_submitters == 2; }, "two producers waiting before injection");
    harness.probe->release();
    std::array<Ticket*, 4> failed{&current, &pending_one, &pending_two, &pending_three};
    for (auto* ticket : failed) {
        auto first_observer = ticket->completion.share();
        auto second_observer = first_observer;
        ready(first_observer, "current/pending failure completion");
        throws<InjectedFailure>([&] { (void)first_observer.get(); }, "original injected exception, not broken_promise");
        throws<InjectedFailure>([&] { (void)second_observer.get(); }, "stable shared completion exception");
    }
    ready(submission, "failure wakes blocked submitter");
    ready(other_submission, "failure wakes every blocked submitter");
    throws<InjectedFailure>([&] { (void)submission.get(); }, "blocked admission sees terminal backend error");
    throws<InjectedFailure>([&] { (void)other_submission.get(); }, "other blocked admission sees terminal backend error");
    throws<InjectedFailure>([&] { (void)harness.runtime.read(0, 1); }, "new admission sees terminal backend error");
    harness.runtime.close_and_drain();
    const auto stats = harness.runtime.stats();
    require(stats.accepted == 5 && stats.succeeded == 1 && stats.failed == 4 && stats.closed &&
                stats.queued == 0 && !stats.executing && stats.waiting_submitters == 0,
            "each accepted request has one success or failure after terminal error");
    require(harness.probe->calls.load() == 2, "pending requests never reach backend after failure");
}

void invalid_requests_never_reach_backend() {
    Harness harness(2);
    const auto maximum = std::numeric_limits<std::size_t>::max();
    throws<std::length_error>([&] { (void)harness.runtime.write(0, Bytes(Runtime::max_payload + 1)); }, "large write");
    throws<std::length_error>([&] { (void)harness.runtime.read(0, Runtime::max_payload + 1); }, "large read");
    throws<std::length_error>([&] { (void)harness.runtime.read(1, maximum); }, "SIZE_MAX byte count");
    throws<std::out_of_range>([&] { (void)harness.runtime.read(maximum, 1); }, "overflowing address plus count");
    throws<std::out_of_range>([&] { (void)harness.runtime.write(maximum, Bytes{std::byte{1}}); }, "overflowing write address");
    throws<std::out_of_range>([&] { (void)harness.runtime.read(maximum, 0); }, "empty transfer beyond extent");
    throws<std::out_of_range>([&] { (void)harness.runtime.read(128, 1); }, "read at extent with nonzero length");
    throws<std::out_of_range>([&] { (void)harness.runtime.write(127, Bytes(2)); }, "write straddling extent");
    require(harness.probe->calls.load() == 0 && harness.runtime.stats().accepted == 0,
            "invalid inputs rejected before hooks and acceptance");
    auto empty_write = harness.runtime.write(128, {});
    auto empty_read = harness.runtime.read(128, 0);
    auto fence = harness.runtime.fence();
    harness.runtime.close_and_drain();
    require(result(empty_write).empty() && result(empty_read).empty() && result(fence).empty(),
            "zero-byte endpoint transfers are well-defined");
    require(harness.probe->calls.load() == 3, "only valid operations invoke hooks");
    throws<std::invalid_argument>([] { Runtime invalid(nullptr, 1); }, "null backend construction");
    throws<std::invalid_argument>([] { Runtime invalid(std::make_unique<CpuBackend>(), 0); }, "zero capacity construction");
}

void moved_input_and_results_outlive_runtime() {
    const auto expected = pattern(32, 113);
    auto probe = std::make_shared<Probe>(1);
    std::future<Bytes> surviving_future;
    Bytes surviving_result;
    {
        Harness harness(4, probe);
        Ticket written;
        {
            Bytes caller_input = expected;
            written = harness.runtime.write(16, std::move(caller_input));
            probe->await_entry();
            caller_input.assign(1024, std::byte{0x55});
        } // The original vector object dies before the backend accesses its bytes.
        auto first_read = harness.runtime.read(16, expected.size());
        auto second_read = harness.runtime.read(16, expected.size());
        surviving_future = std::move(second_read.completion);
        auto overwrite = harness.runtime.write(16, Bytes(expected.size(), std::byte{0xee}));
        probe->release();
        require(result(written).empty(), "moved write succeeds after caller object destruction");
        surviving_result = result(first_read);
        require(result(overwrite).empty(), "subsequent backend overwrite succeeds");
    } // Runtime destruction drains/joins and destroys the backend.
    require(probe->destroyed.load() && !probe->destroyed_while_active.load(), "backend destroyed only after hook exit");
    require(surviving_result == expected, "read result retains independent storage after overwrite and destruction");
    ready(surviving_future, "future remains usable after runtime destruction");
    require(surviving_future.get() == expected, "future owns read bytes after runtime destruction");
}

void fence_hook_and_single_owner() {
    Harness harness(3, std::make_shared<Probe>(2));
    auto write = harness.runtime.write(4, pattern(8, 3));
    auto fence = harness.runtime.fence();
    harness.probe->await_entry();
    auto read = harness.runtime.read(4, 8);
    require(result(write).empty(), "earlier write complete when fence hook entered");
    require(fence.completion.wait_for(0s) == std::future_status::timeout, "fence waits for backend hook return");
    require(read.completion.wait_for(0s) == std::future_status::timeout, "later read cannot pass fence hook");
    harness.probe->release();
    harness.runtime.close_and_drain();
    require(result(fence).empty() && result(read) == pattern(8, 3), "fence then read complete");
    const auto trace = harness.probe->trace();
    require(trace.size() == 3 && trace[0].operation == Operation::write && trace[1].operation == Operation::fence &&
                trace[2].operation == Operation::read, "one explicit barrier hook in FIFO order");
    for (const auto& observation : trace) {
        require(observation.owner == trace[0].owner && observation.owner != std::this_thread::get_id(),
                "all backend hooks use the same dedicated owner thread");
    }
    require(!harness.probe->concurrent.load(), "backend hooks never overlap");
}

void malformed_backend_read_fails_queue() {
    Harness harness(2, std::make_shared<Probe>(1, 0, 1));
    auto read = harness.runtime.read(0, 4);
    harness.probe->await_entry();
    auto pending = harness.runtime.fence();
    harness.probe->release();
    for (auto* ticket : {&read, &pending}) {
        ready(ticket->completion, "short-read failure completion");
        throws<std::runtime_error>([&] { (void)ticket->completion.get(); }, "malformed backend read fails accepted queue");
    }
    harness.runtime.close_and_drain();
    require(harness.runtime.stats().failed == 2 && harness.probe->calls.load() == 1,
            "wrong-size read is terminal and pending fence is not executed");
}

void simultaneous_close_is_idempotent() {
    std::latch started{2};
    Harness harness(1, std::make_shared<Probe>(1));
    auto executing = harness.runtime.write(0, pattern(8, 16));
    harness.probe->await_entry();
    auto pending = harness.runtime.read(0, 8);
    auto first = harness.spawn([&] { started.count_down(); harness.runtime.close_and_drain(); });
    auto second = harness.spawn([&] { started.count_down(); harness.runtime.close_and_drain(); });
    await([&] { return started.try_wait(); }, "both close callers started");
    await([&] { return harness.runtime.stats().closed; }, "simultaneous close admission state");
    require(first.wait_for(0s) == std::future_status::timeout && second.wait_for(0s) == std::future_status::timeout,
            "close callers wait for owner drain");
    harness.probe->release();
    ready(first, "first simultaneous close");
    ready(second, "second simultaneous close");
    first.get();
    second.get();
    require(result(executing).empty() && result(pending) == pattern(8, 16), "simultaneous close drains old work");
    require(harness.runtime.stats().succeeded == 2, "simultaneous close does not duplicate execution");
}
} // namespace

int main() {
    const std::array<std::pair<const char*, void (*)()>, 11> tests{{
        {"CPU bytes and untouched guards", cpu_bytes},
        {"write A/read A/write B/read B", intermediate_order},
        {"two producer accepted-sequence oracle", two_producer_sequence_oracle},
        {"Q=1 blocks and then progresses", capacity_one_blocks_then_progresses},
        {"close drains pending work and wakes submitters", close_drains_and_wakes},
        {"Nth backend exception completes every accepted request", ordinary_failure_finishes_every_accepted_request},
        {"invalid requests rejected before backend hooks", invalid_requests_never_reach_backend},
        {"moved input and result lifetime", moved_input_and_results_outlive_runtime},
        {"fence completion hook and single owner", fence_hook_and_single_owner},
        {"malformed backend read fails queue", malformed_backend_read_fails_queue},
        {"simultaneous close is idempotent", simultaneous_close_is_idempotent},
    }};
    std::size_t passed = 0;
    for (const auto& [name, test] : tests) {
        try {
            test();
            ++passed;
            std::cout << "PASS " << name << '\n';
        } catch (const std::exception& error) {
            std::cerr << "FAIL " << name << ": " << error.what() << '\n';
            return 1;
        }
    }
    std::cout << "RESULT " << passed << '/' << tests.size() << " passed\n";
}
