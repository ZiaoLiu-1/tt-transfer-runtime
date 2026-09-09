#include "tt_transfer/cpu_backend.hpp"
#include <chrono>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

// One producer, identical write/fence/read cycles, each read checked in full.
// Queue construction/destruction excluded. Includes submission, handoff, copies,
// result allocation, completion waiting, and comparison; not device bandwidth.
int main(int argc, char** argv) {
    using namespace tt_transfer;
    using Clock = std::chrono::steady_clock;
    try {
        const auto rounds = argc > 1 ? std::stoull(argv[1]) : 20000ULL;
        const auto repeats = argc > 2 ? std::stoull(argv[2]) : 7ULL;
        if (rounds == 0 || rounds > 1000000 || repeats == 0 || repeats > 100) {
            throw std::invalid_argument("rounds 1..1000000 and repeats 1..100 required");
        }
        Bytes payload(1024);
        for (std::size_t i = 0; i < payload.size(); ++i) payload[i] = static_cast<std::byte>((i * 17 + 23) % 256);
        // Small unreported warmup for both code paths.
        {
            CpuBackend direct;
            Runtime queued(std::make_unique<CpuBackend>(), 16);
            for (int i = 0; i < 100; ++i) {
                direct.write(0, payload); direct.fence();
                if (direct.read(0, 1024) != payload) throw std::runtime_error("warmup mismatch");
                queued.write(0, payload); queued.fence();
                if (queued.read(0, 1024).completion.get() != payload) throw std::runtime_error("warmup mismatch");
            }
        }
        std::cout << "sample,mode,roundtrips,requests,payload_bytes_per_transfer,total_transfer_bytes,queue_capacity,queue_high_water,wall_ns\n";
        for (unsigned long long sample = 0; sample < repeats; ++sample) {
            // Alternate order to reduce systematic first/second bias.
            for (int order = 0; order < 2; ++order) {
                const bool fifo = (static_cast<int>(sample % 2) + order) % 2 != 0;
                CpuBackend direct;
                auto queued = fifo ? std::make_unique<Runtime>(std::make_unique<CpuBackend>(), 16) : nullptr;
                const auto start = Clock::now();
                for (unsigned long long i = 0; i < rounds; ++i) {
                    Bytes result;
                    if (fifo) {
                        queued->write(0, payload);
                        queued->fence();
                        result = queued->read(0, payload.size()).completion.get();
                    } else {
                        direct.write(0, payload);
                        direct.fence();
                        result = direct.read(0, payload.size());
                    }
                    if (result != payload) throw std::runtime_error("measured readback mismatch");
                }
                const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
                if (queued) queued->close_and_drain();
                const auto high_water = queued ? queued->stats().high_water : 0;
                if (queued && queued->stats().succeeded != rounds * 3) throw std::runtime_error("completion count mismatch");
                std::cout << sample << ',' << (fifo ? "fifo_cpu" : "sync_cpu") << ',' << rounds << ',' << rounds * 3
                          << ",1024," << rounds * 2048 << ',' << (fifo ? 16 : 0) << ',' << high_water << ',' << elapsed << '\n';
            }
        }
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
