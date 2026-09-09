#include "umd/device/chip_helpers/simulation_tlb_allocator.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
constexpr std::size_t MiB = std::size_t{1} << 20;
constexpr std::size_t GiB = std::size_t{1} << 30;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void baseline_allocate_free() {
    tt::umd::SimulationTlbAllocator allocator(0x10000000, tt::ARCH::WORMHOLE_B0);
    const int first = allocator.allocate_tlb_index(1024);
    require(first == 0, "baseline first index");
    require(allocator.get_tlb_size_from_index(first) == MiB, "baseline smallest class");
    allocator.deallocate_tlb_index(first);
    require(allocator.allocate_tlb_index(1024) == first, "baseline slot reuse");
    allocator.deallocate_tlb_index(first);
}
void mixed_size_release_sequence() {
    tt::umd::SimulationTlbAllocator allocator(0, tt::ARCH::WORMHOLE_B0);
    const int small = allocator.allocate_tlb_index(4096);
    const int medium = allocator.allocate_tlb_index(MiB + 1);
    const int large = allocator.allocate_tlb_index(2 * MiB + 1);
    require(small == 0 && medium == 156 && large == 166, "mixed-size independent pools");
    allocator.deallocate_tlb_index(medium);
    require(allocator.allocate_tlb_index(2 * MiB) == medium, "mixed-size released slot reuse");
    require(allocator.get_tlb_size_from_index(large) == 16 * MiB, "large size class");
    std::vector<int> small_pool;
    for (int i = 1; i < 156; ++i) small_pool.push_back(allocator.allocate_tlb_index(1));
    const int escalated = allocator.allocate_tlb_index(1);
    require(escalated == 157, "full smallest class escalates to next free class");
    allocator.deallocate_tlb_index(small);
    require(allocator.allocate_tlb_index(1) == small, "released smallest class preferred again");
    for (const auto index : small_pool) allocator.deallocate_tlb_index(index);
    allocator.deallocate_tlb_index(small);
    allocator.deallocate_tlb_index(medium);
    allocator.deallocate_tlb_index(large);
    allocator.deallocate_tlb_index(escalated);
}
void nonzero_blackhole_bar4() {
    constexpr std::uint64_t bar0 = 0x10000000ULL;
    constexpr std::uint64_t bar4 = 0x1200000000ULL;
    tt::umd::SimulationTlbAllocator allocator(bar0, tt::ARCH::BLACKHOLE, bar4);
    const int first = allocator.allocate_tlb_index(2 * MiB + 1);
    const int second = allocator.allocate_tlb_index(4 * GiB);
    require(first == 202 && second == 203, "Blackhole 4 GiB indices");
    require(allocator.get_tlb_address_from_index(first) == bar4, "first BAR4 address");
    require(allocator.get_tlb_address_from_index(second) == bar4 + 4 * GiB, "next BAR4 address");
    require(allocator.get_tlb_size_from_index(second) == 4 * GiB, "4 GiB window size");
    allocator.deallocate_tlb_index(first);
    allocator.deallocate_tlb_index(second);
}
}  // namespace

int main() {
    try {
        baseline_allocate_free();
        std::cout << "PASS upstream_baseline_allocate_free\n";
        mixed_size_release_sequence();
        std::cout << "PASS project_mixed_size_release_sequence\n";
        nonzero_blackhole_bar4();
        std::cout << "PASS project_nonzero_blackhole_bar4\n";
        std::cout << "linked_real_umd_allocator=1 examples_passed=3 allocated_4gib_ram=0\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
