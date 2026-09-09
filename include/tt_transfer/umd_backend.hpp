#pragma once

#include "tt_transfer/runtime.hpp"

#include <filesystem>
#include <memory>
#include <string>

namespace tt_transfer {

// One live UMD Cluster / simulator session. Calls must be serialized. The demo
// performs direct calls first, then moves this same instance into Runtime.
class UmdBackend final : public Backend {
public:
    explicit UmdBackend(const std::filesystem::path& simulator_library);
    ~UmdBackend() override;
    UmdBackend(const UmdBackend&) = delete;
    UmdBackend& operator=(const UmdBackend&) = delete;

    std::size_t size() const noexcept override;
    void write(std::size_t offset, std::span<const std::byte> bytes) override;
    Bytes read(std::size_t offset, std::size_t bytes) override;
    void fence() override;
    std::string core_description() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace tt_transfer
