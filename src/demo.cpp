#include "tt_transfer/cpu_backend.hpp"
#include <array>
#include <iostream>
#include <thread>

int main() {
    using namespace tt_transfer;
    try {
        Runtime runtime(std::make_unique<CpuBackend>(), 4);
        std::array<std::exception_ptr, 2> failures{};
        std::array<std::thread, 2> producers;
        for (std::size_t p = 0; p < producers.size(); ++p) {
            producers[p] = std::thread([&, p] {
                try {
                    Bytes expected(1024);
                    for (std::size_t i = 0; i < expected.size(); ++i) {
                        expected[i] = static_cast<std::byte>((i * 17 + p * 31) % 256);
                    }
                    // A fence does not make write/read an atomic transaction.
                    // Disjoint scratch regions make arbitrary interleaving safe.
                    const auto offset = p * expected.size();
                    auto write = runtime.write(offset, expected);
                    auto fence = runtime.fence();
                    auto read = runtime.read(offset, expected.size());
                    write.completion.get();
                    fence.completion.get();
                    if (read.completion.get() != expected) throw std::runtime_error("byte mismatch");
                } catch (...) { failures[p] = std::current_exception(); }
            });
        }
        for (auto& producer : producers) producer.join();
        runtime.close_and_drain();
        for (const auto& failure : failures) if (failure) std::rethrow_exception(failure);
        const auto stats = runtime.stats();
        std::cout << "PASS CPU two_producers=2 payload_bytes=1024 accepted=" << stats.accepted
                  << " succeeded=" << stats.succeeded << " queue_high_water=" << stats.high_water << '\n';
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
