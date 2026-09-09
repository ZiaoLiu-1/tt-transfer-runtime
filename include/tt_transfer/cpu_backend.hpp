#pragma once
#include "tt_transfer/runtime.hpp"

namespace tt_transfer {
class CpuBackend final : public Backend {
public:
    explicit CpuBackend(std::size_t bytes = 8192);
    std::size_t size() const noexcept override;
    void write(std::size_t offset, std::span<const std::byte> bytes) override;
    Bytes read(std::size_t offset, std::size_t bytes) override;
    void fence() override;
private:
    void check(std::size_t offset, std::size_t bytes) const;
    Bytes memory_;
};
} // namespace tt_transfer
