#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <future>
#include <memory>
#include <mutex>
#include <span>
#include <thread>
#include <vector>

namespace tt_transfer {
using Bytes = std::vector<std::byte>;

// All I/O hooks run on one owner thread. size() is immutable metadata read at
// construction. A hook must eventually return/throw for close/destruction to join.
class Backend {
public:
    virtual ~Backend() = default;
    virtual std::size_t size() const noexcept = 0;
    virtual void write(std::size_t offset, std::span<const std::byte> bytes) = 0;
    virtual Bytes read(std::size_t offset, std::size_t bytes) = 0;
    virtual void fence() = 0;
};

struct Ticket {
    std::uint64_t sequence;
    std::future<Bytes> completion; // empty result for write/fence
};

struct Stats {
    std::uint64_t accepted = 0;
    std::uint64_t succeeded = 0;
    std::uint64_t failed = 0;
    std::size_t queued = 0;
    std::size_t high_water = 0;
    std::size_t waiting_submitters = 0;
    bool executing = false;
    bool closed = false;
};

class Runtime {
public:
    static constexpr std::size_t max_payload = 4096;
    explicit Runtime(std::unique_ptr<Backend> backend, std::size_t queue_capacity);
    ~Runtime();
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    // Blocking admission. Sequence is assigned under the queue mutex only when
    // accepted. Unaccepted call arguments remain outside the Q+1 resource bound.
    Ticket write(std::size_t offset, Bytes bytes);
    Ticket read(std::size_t offset, std::size_t bytes);
    Ticket fence();
    // Idempotent; concurrent close calls are serialized by call_once. Never join
    // from a backend callback. Callers must cease using this object before destruction.
    void close_and_drain();
    Stats stats() const;

private:
    enum class Kind { write, read, fence };
    struct Request {
        Kind kind;
        std::size_t offset;
        std::size_t count;
        Bytes payload;
        std::promise<Bytes> promise;
    };
    Ticket submit(Kind kind, std::size_t offset, std::size_t count, Bytes payload);
    void validate(std::size_t offset, std::size_t count) const;
    void run();

    std::unique_ptr<Backend> backend_; // destroyed only after owner has joined
    const std::size_t capacity_;
    const std::size_t extent_;
    mutable std::mutex mutex_;
    std::condition_variable work_;
    std::condition_variable space_;
    std::deque<std::unique_ptr<Request>> queue_;
    Stats stats_;
    std::exception_ptr failure_;
    std::once_flag join_once_;
    std::thread owner_;
};
} // namespace tt_transfer
