#include "tt_transfer/umd_backend.hpp"

#include "umd/device/cluster.hpp"
#include "umd/device/soc_descriptor.hpp"

#include <optional>
#include <stdexcept>

namespace tt_transfer {
namespace {
constexpr std::uint64_t scratch_address = 0x1000;
constexpr std::size_t scratch_bytes = 8192;

tt::umd::ClusterOptions simulation_options(const std::filesystem::path& library) {
    if (!std::filesystem::is_regular_file(library)) {
        throw std::invalid_argument("simulator library must be an existing file");
    }
    tt::umd::ClusterOptions options;
    options.chip_type = tt::umd::ChipType::SIMULATION;
    options.target_devices = {0};
    options.num_host_mem_ch_per_mmio_device = 0;
    options.simulator_directory = std::filesystem::absolute(library);
    return options;
}

void check_range(std::size_t offset, std::size_t bytes) {
    if (offset > scratch_bytes || bytes > scratch_bytes - offset) {
        throw std::out_of_range("UMD access outside reserved L1 scratch region");
    }
}
}  // namespace

struct UmdBackend::Impl {
    explicit Impl(const std::filesystem::path& library) : cluster(simulation_options(library)) {
        const auto& descriptor = cluster.get_soc_descriptor(0);
        const auto cores = descriptor.get_cores(tt::CoreType::TENSIX);
        if (cores.empty() || descriptor.worker_l1_size <= 0 ||
            scratch_address + scratch_bytes > static_cast<std::uint64_t>(descriptor.worker_l1_size)) {
            throw std::runtime_error("descriptor has no legal TENSIX scratch region");
        }
        core = cores.front();
    }

    tt::umd::Cluster cluster;
    std::optional<tt::umd::CoreCoord> core;
};

UmdBackend::UmdBackend(const std::filesystem::path& library) : impl_(std::make_unique<Impl>(library)) {}
UmdBackend::~UmdBackend() = default;
std::size_t UmdBackend::size() const noexcept { return scratch_bytes; }

void UmdBackend::write(std::size_t offset, std::span<const std::byte> bytes) {
    check_range(offset, bytes.size());
    if (!bytes.empty()) {
        impl_->cluster.write_to_device(bytes.data(), bytes.size(), 0, *impl_->core, scratch_address + offset);
    }
}

Bytes UmdBackend::read(std::size_t offset, std::size_t bytes) {
    check_range(offset, bytes);
    Bytes result(bytes);
    if (bytes != 0) {
        impl_->cluster.read_from_device(result.data(), 0, *impl_->core, scratch_address + offset, bytes);
    }
    return result;
}

void UmdBackend::fence() {
    // Deliberately call the real API. SimulationChip::l1_membar is a no-op at
    // our pinned UMD revision, so this cannot validate a silicon memory barrier.
    impl_->cluster.l1_membar(0, {*impl_->core});
}

std::string UmdBackend::core_description() const { return impl_->core->str(); }
}  // namespace tt_transfer
