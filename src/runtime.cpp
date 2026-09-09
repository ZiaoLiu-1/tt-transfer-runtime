#include "tt_transfer/runtime.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace tt_transfer {
Runtime::Runtime(std::unique_ptr<Backend> backend, std::size_t queue_capacity)
    : backend_(std::move(backend)), capacity_(queue_capacity),
      extent_(backend_ ? backend_->size() : 0) {
    if (!backend_ || capacity_ == 0) {
        throw std::invalid_argument("backend and positive queue capacity required");
    }
    owner_ = std::thread(&Runtime::run, this);
}

Runtime::~Runtime() { close_and_drain(); }

void Runtime::validate(std::size_t offset, std::size_t count) const {
    if (count > max_payload) throw std::length_error("payload exceeds 4096 bytes");
    // Subtraction avoids overflow in offset + count, including SIZE_MAX inputs.
    if (offset > extent_ || count > extent_ - offset) {
        throw std::out_of_range("transfer outside backend scratch region");
    }
}

Ticket Runtime::write(std::size_t offset, Bytes bytes) {
    const auto count = bytes.size();
    return submit(Kind::write, offset, count, std::move(bytes));
}
Ticket Runtime::read(std::size_t offset, std::size_t bytes) {
    return submit(Kind::read, offset, bytes, {});
}
Ticket Runtime::fence() { return submit(Kind::fence, 0, 0, {}); }

Ticket Runtime::submit(Kind kind, std::size_t offset, std::size_t count, Bytes payload) {
    validate(offset, count);
    auto request = std::make_unique<Request>(Request{kind, offset, count, std::move(payload), {}});
    auto future = request->promise.get_future();
    std::unique_lock lock(mutex_);
    if (!stats_.closed && queue_.size() == capacity_) {
        ++stats_.waiting_submitters;
        space_.wait(lock, [this] { return stats_.closed || queue_.size() < capacity_; });
        --stats_.waiting_submitters;
    }
    if (failure_) std::rethrow_exception(failure_);
    if (stats_.closed) throw std::runtime_error("runtime closed");
    if (stats_.accepted == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error("request sequence exhausted");
    }
    // Push can allocate/throw: assign the sequence only after it succeeds.
    queue_.push_back(std::move(request));
    const auto sequence = ++stats_.accepted;
    stats_.queued = queue_.size();
    stats_.high_water = std::max(stats_.high_water, queue_.size());
    lock.unlock();
    work_.notify_one();
    return {sequence, std::move(future)};
}

void Runtime::run() {
    for (;;) {
        std::unique_ptr<Request> current;
        {
            std::unique_lock lock(mutex_);
            work_.wait(lock, [this] { return stats_.closed || !queue_.empty(); });
            if (queue_.empty()) return;
            current = std::move(queue_.front());
            queue_.pop_front();
            stats_.queued = queue_.size();
            stats_.executing = true;
        }
        space_.notify_one();
        try {
            Bytes result;
            switch (current->kind) {
            case Kind::write:
                backend_->write(current->offset, current->payload);
                break;
            case Kind::read:
                result = backend_->read(current->offset, current->count);
                if (result.size() != current->count) {
                    throw std::runtime_error("backend returned wrong read size");
                }
                break;
            case Kind::fence:
                backend_->fence();
                break;
            }
            current->promise.set_value(std::move(result));
            std::lock_guard lock(mutex_);
            ++stats_.succeeded;
            stats_.executing = false;
        } catch (...) {
            auto error = std::current_exception();
            std::deque<std::unique_ptr<Request>> pending;
            {
                std::lock_guard lock(mutex_);
                failure_ = error;
                stats_.closed = true;
                stats_.executing = false;
                pending.swap(queue_);
                stats_.queued = 0;
                stats_.failed += 1 + pending.size();
            }
            // Reject new submissions and wake blocked producers before completing
            // accepted promises. Each promise belongs to exactly one request.
            space_.notify_all();
            current->promise.set_exception(error);
            for (auto& request : pending) request->promise.set_exception(error);
            return;
        }
    }
}

void Runtime::close_and_drain() {
    {
        std::lock_guard lock(mutex_);
        stats_.closed = true;
    }
    space_.notify_all();
    work_.notify_all();
    // call_once makes simultaneous close calls safe; the queue mutex is released.
    std::call_once(join_once_, [this] { owner_.join(); });
}

Stats Runtime::stats() const {
    std::lock_guard lock(mutex_);
    return stats_;
}
} // namespace tt_transfer
