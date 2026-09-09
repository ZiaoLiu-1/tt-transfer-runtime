#include "tt_transfer/umd_backend.hpp"

#include <array>
#include <chrono>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
using Clock = std::chrono::steady_clock;
using tt_transfer::Bytes;
double microseconds(Clock::time_point start) {
    return std::chrono::duration<double, std::micro>(Clock::now() - start).count();
}
Bytes pattern(unsigned seed) {
    Bytes bytes(1024);
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        bytes[i] = static_cast<std::byte>((i * 37U + seed) % 256U);
    }
    return bytes;
}
void require_equal(const Bytes& got, const Bytes& expected, const char* stage) {
    if (got != expected) throw std::runtime_error(std::string("byte mismatch: ") + stage);
}
}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: umd_demo /absolute/path/libttsim_wh_aarch64.so\n";
        return 2;
    }
    try {
        auto start = Clock::now();
        auto backend = std::make_unique<tt_transfer::UmdBackend>(argv[1]);
        const double constructor_us = microseconds(start);
        std::cout << "core=" << backend->core_description()
                  << " scratch=0x1000 extent_bytes=" << backend->size() << '\n';

        const auto expected = pattern(19);
        start = Clock::now();
        backend->write(0, expected);
        backend->fence();
        auto direct_result = backend->read(0, expected.size());
        const double direct_us = microseconds(start);
        require_equal(direct_result, expected, "direct");
        std::cout << "PASS direct_readback bytes=1024\n";

        // Ownership handoff is sequential: exactly one Cluster is alive. No
        // producer holds the backend once the owner thread has been created.
        tt_transfer::Runtime runtime(std::move(backend), 8);
        const auto fifo_expected = pattern(83);
        start = Clock::now();
        auto write = runtime.write(0, fifo_expected);
        auto fence = runtime.fence();
        auto read = runtime.read(0, fifo_expected.size());
        write.completion.get();
        fence.completion.get();
        auto fifo_result = read.completion.get();
        const double fifo_us = microseconds(start);
        require_equal(fifo_result, fifo_expected, "FIFO");
        std::cout << "PASS fifo_readback bytes=1024\n";

        // Fence does not make write/read a transaction. Each producer uses a
        // disjoint 1 KiB region, so any accepted interleaving has a clear oracle.
        std::array<std::exception_ptr, 2> errors{};
        auto producer = [&](std::size_t id) {
            try {
                const auto bytes = pattern(static_cast<unsigned>(113 + id));
                auto w = runtime.write(id * 1024, bytes);
                auto f = runtime.fence();
                auto r = runtime.read(id * 1024, bytes.size());
                w.completion.get();
                f.completion.get();
                require_equal(r.completion.get(), bytes, "two producers");
            } catch (...) {
                errors[id] = std::current_exception();
            }
        };
        std::thread p0(producer, 0);
        std::thread p1(producer, 1);
        p0.join();
        p1.join();
        runtime.close_and_drain();
        for (const auto& error : errors) if (error) std::rethrow_exception(error);
        std::cout << "PASS two_producer_readback bytes_each=1024 disjoint_regions=2\n";
        std::cout << "METRIC constructor_us=" << constructor_us
                  << " direct_roundtrip_us=" << direct_us
                  << " fifo_roundtrip_us=" << fifo_us << '\n';
        std::cout << "BOUNDARY host_wall_time_only; simulator_l1_barrier_is_noop; no_silicon_claim\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ERROR " << error.what() << '\n';
        return 1;
    }
}
