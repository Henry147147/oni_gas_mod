#include "oni_publication.hpp"
#include <algorithm>
#include <limits>
#include <map>
#include <mutex>
#include <stdexcept>

namespace ma::oni::published_frames {
namespace {
constexpr std::size_t maximum_cached_frames = 8, maximum_leases = 16;
struct Entry { std::shared_ptr<const Frame> frame; std::uint64_t sequence; };
std::mutex mutex;
std::map<const void*, Entry, std::less<>> frames;
std::map<std::uint64_t, std::shared_ptr<const Frame>> leases;
std::uint64_t sequence = 0, next_token = 1, generation = 1;
struct Binding { const void* native_update{}; std::shared_ptr<const Frame> frame; std::uint64_t generation{}; };
thread_local Binding binding;
}
void record(const void* game_data, std::optional<Frame> input) {
    if (!game_data) throw std::invalid_argument("Missing native GameData identity");
    if (!input) { std::lock_guard lock(mutex); frames.erase(game_data); return; }
    const auto& s = input->status;
    if (s.struct_size != sizeof(s) || s.abi_version != MA_ONI_DOMAIN_ABI_VERSION || s.state != 1 || !s.session ||
        s.cells != input->cells.size() || input->cells.empty() || input->cells.size() > 4096 || !input->native_epoch)
        throw std::invalid_argument("Invalid completed native publication");
    const auto& h = input->solid_heat; const auto& e = input->solid_endpoint; const auto& c = input->solid_cell;
    if (h.struct_size != sizeof(h) || h.abi_version != 1 || h.state != s.state || h.reserved ||
        h.session != s.session || h.completed_frame != s.completed_frames ||
        e.struct_size != sizeof(e) || e.abi_version != 1 || e.state != s.state || e.reserved ||
        e.session != s.session || e.completed_frame != s.completed_frames || e.contact_frame > s.completed_frames ||
        c.struct_size != sizeof(c) || c.abi_version != 1 || c.state != s.state || c.reserved ||
        c.session != s.session || c.completed_frame != s.completed_frames || c.native_ticket != s.native_ticket ||
        (e.contact_frame && e.solid_public_cell != c.solid_public_cell))
        throw std::invalid_argument("Native publication combines different completed frames");
    auto frame = std::make_shared<const Frame>(std::move(*input));
    std::lock_guard lock(mutex);
    if (sequence == std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("Native publication sequence exhausted");
    frames.insert_or_assign(game_data, Entry{std::move(frame), sequence + 1}); ++sequence;
    if (frames.size() > maximum_cached_frames) {
        const auto oldest = std::min_element(frames.begin(), frames.end(), [](const auto& a, const auto& b) { return a.second.sequence < b.second.sequence; });
        frames.erase(oldest);
    }
}
void bind(const void* game_data, const void* native_update) {
    binding = {};
    if (!game_data || !native_update) return;
    std::lock_guard lock(mutex);
    const auto found = frames.find(game_data);
    if (found != frames.end()) binding = {native_update, found->second.frame, generation};
}
void unbind() noexcept { binding = {}; }
void reset() {
    std::lock_guard lock(mutex);
    if (generation == std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("Native publication generation exhausted");
    frames.clear(); binding = {}; ++generation;
}
MAOniDomainPublicationInfo acquire(const void* native_update) {
    if (!native_update) throw std::invalid_argument("Missing native publication packet");
    if (binding.native_update != native_update || !binding.frame)
        throw std::logic_error("Native publication does not have an owned completed source");
    const auto& frame = *binding.frame;
    std::lock_guard lock(mutex);
    if (binding.generation != generation) throw std::logic_error("Native publication belongs to a previous owner");
    if (leases.size() == maximum_leases) throw std::length_error("Native publication lease capacity exhausted");
    if (next_token == std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("Native publication lease sequence exhausted");
    const auto token = next_token; leases.emplace(token, binding.frame); ++next_token;
    return {sizeof(MAOniDomainPublicationInfo), MA_ONI_DOMAIN_PUBLICATION_ABI_VERSION, 1, 0, token, frame.status.session,
        frame.status.completed_frames, frame.native_epoch, static_cast<std::uint32_t>(frame.cells.size()), 0, frame.status.time_seconds};
}
std::shared_ptr<const Frame> read(std::uint64_t token) {
    std::lock_guard lock(mutex); const auto found = leases.find(token);
    if (found == leases.end()) throw std::invalid_argument("Unknown or released native publication lease");
    return found->second;
}
void release(std::uint64_t token) {
    std::lock_guard lock(mutex);
    if (leases.erase(token) != 1) throw std::invalid_argument("Unknown or released native publication lease");
}
Statistics statistics() { std::lock_guard lock(mutex); return {frames.size(), leases.size()}; }
}
