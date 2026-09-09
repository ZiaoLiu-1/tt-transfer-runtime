#include "tt_transfer/cpu_backend.hpp"
#include <algorithm>
#include <stdexcept>

namespace tt_transfer {
CpuBackend::CpuBackend(std::size_t bytes) : memory_(bytes) {}
std::size_t CpuBackend::size() const noexcept { return memory_.size(); }
void CpuBackend::check(std::size_t offset, std::size_t bytes) const {
    if (offset > memory_.size() || bytes > memory_.size() - offset) {
        throw std::out_of_range("CPU memory range");
    }
}
void CpuBackend::write(std::size_t offset, std::span<const std::byte> bytes) {
    check(offset, bytes.size());
    std::copy(bytes.begin(), bytes.end(), memory_.begin() + static_cast<std::ptrdiff_t>(offset));
}
Bytes CpuBackend::read(std::size_t offset, std::size_t bytes) {
    check(offset, bytes);
    const auto start = memory_.begin() + static_cast<std::ptrdiff_t>(offset);
    return Bytes(start, start + static_cast<std::ptrdiff_t>(bytes));
}
void CpuBackend::fence() {} // synchronous byte copies already completed
} // namespace tt_transfer
