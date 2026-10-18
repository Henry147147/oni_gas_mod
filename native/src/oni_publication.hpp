#pragma once
#include "mixed_atmospheres/oni_publication.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace ma::oni::published_frames {
struct Frame {
    MAOniDomainStatus status{};
    std::vector<MAOniDomainCell> cells;
    MAOniDomainSolidHeatStatus solid_heat{};
    MAOniDomainSolidHeatEndpoint solid_endpoint{};
    MAOniDomainSolidCellState solid_cell{};
    std::uint64_t native_epoch{};
};
// Native GameData identities are compared only, never dereferenced. The copy
// hook records after native arrays are complete; the export hook binds their
// actual source GameData before returning the native packet to its caller.
void record(const void* game_data, std::optional<Frame>);
void bind(const void* game_data, const void* native_update);
void unbind() noexcept;
void reset(); // Keep already acquired leases alive.
[[nodiscard]] MAOniDomainPublicationInfo acquire(const void* native_update);
[[nodiscard]] std::shared_ptr<const Frame> read(std::uint64_t token);
void release(std::uint64_t token);
struct Statistics { std::size_t cached_frames{}, leases{}; };
[[nodiscard]] Statistics statistics();
}
