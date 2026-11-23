#pragma once
// The colony owner's input journal (MA_COLONY_INPUT_JOURNAL_DIR,
// docs/colony-owner.md) and its replay into a restored owner: shared by the
// continuation check (native/tests/oni_colony_replay.cpp) and the owner
// replay benchmark (native/apps/colony_benchmark.cpp).
#include "oni_colony.hpp"
#include <array>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ma::oni::colony::journal_replay {
inline void require(bool ok, const std::string& why) { if (!ok) throw std::runtime_error(why); }
inline std::vector<std::uint8_t> read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary); require(in.good(), "Cannot read " + path);
    return {std::istreambuf_iterator<char>(in), {}};
}
struct Cursor {
    const std::vector<std::uint8_t>& b; std::size_t at = 0;
    template <class T> T get() { require(at + sizeof(T) <= b.size(), "Truncated owner image"); T v; std::memcpy(&v, b.data() + at, sizeof(T)); at += sizeof(T); return v; }
    void skip(std::size_t n) { require(n <= b.size() - at, "Truncated owner image"); at += n; }
};
struct ActivityPacket { std::uint64_t sequence=0;double seconds=0;std::vector<std::uint32_t> actors,touched;std::array<std::size_t,5> cuts{}; };
struct OwnerParts { MAOniColonyConfig config{}; std::vector<MAOniColonyMaterial> materials; std::vector<std::uint8_t> state; std::vector<MAOniColonyTransfer> transfers;
    std::vector<MAOniColonyHeat> heat; std::vector<MAOniColonyTerrain> terrain; std::size_t freezes = 0, germs = 0; std::uint64_t completed = 0;
    std::uint32_t version=0;std::vector<ActivityPacket> activity; };
// Split an owner wrapper (8..15) into committed state and queued inputs.
inline OwnerParts parse(const std::vector<std::uint8_t>& image) {
    OwnerParts out; Cursor c{image};
    require(image.size() > 72 && std::memcmp(image.data(), "MACOLN01", 8) == 0, "Not an owner image");
    c.skip(8); auto version = c.get<std::uint32_t>();
    if (version == 13) { version = c.get<std::uint32_t>(); (void)c.get<std::uint32_t>(); }
    require((version >= 8 && version <= 12) || version==14 || version==15, "Unsupported owner wrapper");
    out.version=version;
    auto& s = out.state; const auto keep = [&](std::size_t from) { s.insert(s.end(), image.begin() + static_cast<std::ptrdiff_t>(from), image.begin() + static_cast<std::ptrdiff_t>(c.at)); };
    s.insert(s.end(), reinterpret_cast<const std::uint8_t*>(&version), reinterpret_cast<const std::uint8_t*>(&version) + 4);
    auto from = c.at;
    out.config = c.get<MAOniColonyConfig>();
    const auto materials = c.get<std::uint32_t>();
    for (std::uint32_t k = 0; k < materials; ++k) out.materials.push_back(c.get<MAOniColonyMaterial>());
    c.skip(c.get<std::uint32_t>()); c.skip(c.get<std::uint32_t>()); c.skip(std::size_t{c.get<std::uint32_t>()} * 32);
    c.skip(std::size_t{materials} * 5 * 16 + 5 * 16 + std::size_t{materials} * 16 + 3 * 16 + 16);
    const auto counters = c.at; out.completed = c.get<std::uint64_t>(); c.at = counters; c.skip(7 * 8 + 7 * 8);
    keep(from);
    const auto transfers = c.get<std::uint32_t>(); for (std::uint32_t k = 0; k < transfers; ++k) out.transfers.push_back(c.get<MAOniColonyTransfer>());
    const auto heat = c.get<std::uint32_t>(); for (std::uint32_t k = 0; k < heat; ++k) out.heat.push_back(c.get<MAOniColonyHeat>());
    const auto terrain = c.get<std::uint32_t>(); for (std::uint32_t k = 0; k < terrain; ++k) out.terrain.push_back(c.get<MAOniColonyTerrain>());
    out.freezes = c.get<std::uint32_t>(); c.skip(out.freezes * 16);
    (void)c.get<std::uint64_t>(); // accepted job watermark (advanced by queued inputs)
    const auto jobs = c.get<std::uint32_t>();
    for (std::uint32_t k = 0; k < jobs; ++k) {
        c.skip(4 + 8); c.skip(std::size_t{c.get<std::uint32_t>()} * 16);
        if (version >= 9) c.skip(c.get<std::uint32_t>());
    }
    if (version >= 10) {
        from = c.at; c.skip(4 + 8); c.skip(std::size_t{c.get<std::uint32_t>()} * 18); keep(from);
        out.germs = c.get<std::uint32_t>(); c.skip(out.germs * 16); // queued germ edits
        from = c.at; c.skip(std::size_t{c.get<std::uint32_t>()} * (8 + 6 * 16)); c.skip(8); keep(from);
    }
    if (version >= 11) { from = c.at; c.skip(c.get<std::uint32_t>()); keep(from); }
    if(version==15) { from=c.at;c.skip(4);keep(from); }
    if(version>=14) {
        from=c.at;c.skip(8);keep(from); // committed activity sequence
        c.skip(8+1);if(c.get<std::uint8_t>())c.skip(1); // accepted/requested policy and pending mode
        from=c.at;c.skip(std::size_t{c.get<std::uint32_t>()}*8);keep(from); // committed biology fractions
        const auto cells=[&] {
            const auto n=c.get<std::uint32_t>();require(n<=(image.size()-c.at)/4,"Truncated activity cells");
            std::vector<std::uint32_t> list;list.reserve(n);for(std::uint32_t i=0;i<n;++i)list.push_back(c.get<std::uint32_t>());return list;
        };
        const auto packet=[&] {
            ActivityPacket p;p.sequence=c.get<std::uint64_t>();p.seconds=c.get<double>();p.actors=cells();p.touched=cells();
            for(auto& cut:p.cuts)cut=static_cast<std::size_t>(c.get<std::uint64_t>());
            return p;
        };
        const auto n=c.get<std::uint32_t>();require(n<=1024,"Too many pending activity frames");
        for(std::uint32_t i=0;i<n;++i)out.activity.push_back(packet());
        if(c.get<std::uint8_t>())(void)packet();
        (void)cells(); // pending native contact wakes, already restored by owner
    }
    from = c.at; c.skip(static_cast<std::size_t>(c.get<std::uint64_t>())); keep(from);
    require(c.at + 64 == image.size(), "Owner image has unexpected trailing data");
    return out;
}
struct Input { char kind; std::vector<std::string> fields; std::vector<std::pair<std::uint64_t, double>> passives; std::vector<std::uint8_t> wire; };
struct Frame { double seconds = 0; std::uint64_t frames = 1; bool flush = false,after_checkpoint=false; std::vector<Input> inputs;
    bool activity=false;std::uint64_t activity_sequence=0;int mode=-1;std::vector<std::uint32_t> actors,touched;
    std::optional<std::vector<std::uint32_t>> vents;
    std::optional<std::vector<FaceForceEntry>> forces; // the driven face forces from this frame on
    std::optional<MAOniColonyOptions> options; // a player's options change just before this frame
    std::optional<std::uint32_t> energy; }; // the source energy rule set just before this frame
inline double hex(const std::string& text) { return std::strtod(text.c_str(), nullptr); }
inline std::vector<std::pair<std::uint64_t, Frame>> journal(const std::string& path,const std::string& checkpoint_digest={}) {
    std::ifstream in(path); require(in.good(), "Cannot read journal " + path);
    std::vector<std::pair<std::uint64_t, Frame>> frames; std::string line;bool after_checkpoint=false;
    std::optional<MAOniColonyOptions> options;
    // The owner does not save its source energy rule: the one in effect at a
    // matching checkpoint applies before its first replayed frame.
    std::uint32_t energy_in_effect=2;std::optional<std::uint32_t> energy;
    std::optional<std::vector<std::uint32_t>> vent_map, pending_vents;
    // Face forces are not saved either: those in effect at a matching
    // checkpoint apply before its first replayed frame.
    std::optional<std::vector<FaceForceEntry>> force_map, pending_forces;
    while (std::getline(in, line)) {
        std::istringstream words(line); std::vector<std::string> f; for (std::string w; words >> w;) f.push_back(w);
        if (f.empty() || f[0] == "session") continue;
        if(f[0]=="checkpoint") {
            require(f.size()==3,"Invalid checkpoint journal marker");
            // The saved image already holds an earlier options change.
            if(!after_checkpoint && f[2]==checkpoint_digest){frames.clear();options.reset();energy=energy_in_effect;pending_vents=vent_map;pending_forces=force_map;after_checkpoint=true;}
            continue;
        }
        if(f[0]=="forces") {
            require(f.size()>=2 && f.size()==2+6*std::stoul(f[1]),"Invalid face force journal entry");
            force_map.emplace();
            for(std::size_t i=2;i<f.size();i+=6)
                force_map->push_back({static_cast<std::uint32_t>(std::stoul(f[i])),static_cast<std::uint32_t>(std::stoul(f[i+1])),
                    std::stoi(f[i+2]),std::stoi(f[i+3]),hex(f[i+4]),hex(f[i+5])});
            pending_forces=force_map;continue;
        }
        if(f[0]=="vents") {
            vent_map.emplace();
            for(std::size_t i=1;i<f.size();++i)vent_map->push_back(static_cast<std::uint32_t>(std::stoul(f[i])));
            pending_vents=vent_map;continue;
        }
        if(f[0]=="energy") {
            require(f.size()==2,"Invalid source energy journal entry");
            energy_in_effect=static_cast<std::uint32_t>(std::stoul(f[1]));energy=energy_in_effect;continue;
        }
        if(f[0]=="options") {
            require(f.size()==13 || f.size()==14,"Invalid options journal entry");
            MAOniColonyOptions o{};o.struct_size=sizeof(o);o.abi_version=3;
            const auto u=[&](std::size_t k){return static_cast<std::uint32_t>(std::stoul(f[k]));};
            o.pressure_maximum_iterations=u(1);o.heat_maximum_iterations=u(2);o.flow_parts=u(3);o.sharpen_liquid_interfaces=u(4);
            o.maximum_timestep_s=hex(f[5]);o.trace_sink_threshold_kg=hex(f[6]);o.liquid_stiffness_ratio=hex(f[7]);o.pressure_relative_tolerance=hex(f[8]);
            o.gas_heat_mixing=hex(f[9]);o.interface_detail_fraction=hex(f[10]);o.phase_relaxation_time_s=hex(f[11]);o.evaporation_below_boiling=u(12);
            if(f.size()==14)o.fluid_tick_stride=u(13);
            options=o;continue;
        }
        if (f[0] == "frame") {
            Frame frame; frame.flush = f[3] == "1"; frame.seconds = hex(f[5]);frame.after_checkpoint=after_checkpoint;frame.options=std::exchange(options,{});frame.energy=std::exchange(energy,{});
            for (std::size_t k = 8; k + 1 < f.size(); k += 2) if (f[k] == "frames") frame.frames = std::stoull(f[k + 1]);
            frame.vents=std::exchange(pending_vents,{});
            frame.forces=std::exchange(pending_forces,{});
            frames.emplace_back(std::stoull(f[1]), std::move(frame)); continue;
        }
        require(!frames.empty(), "Journal input precedes its frame");
        auto& current=frames.back().second;
        if(f[0]=="A") {require(f.size()==3 && !current.activity,"Invalid activity journal metadata");current.activity=true;
            current.activity_sequence=std::stoull(f[1]);current.mode=std::stoi(f[2]);require(current.mode>=-1 && current.mode<=1,"Invalid activity journal policy");continue;}
        if(f[0]=="C" || f[0]=="D") {require(f.size()==2 && current.activity,"Activity cells precede their metadata");
            (f[0]=="C"?current.actors:current.touched).push_back(static_cast<std::uint32_t>(std::stoul(f[1])));continue;}
        auto& inputs = frames.back().second.inputs;
        if (f[0] == "P") { require(!inputs.empty(), "Journal passive has no job"); inputs.back().passives.push_back({std::stoull(f[1]), hex(f[2])}); continue; }
        if (f[0] == "W") {
            require(!inputs.empty() && f.size() == 2 && f[1].size() % 2 == 0, "Invalid journal moving payload");
            for (std::size_t k = 0; k < f[1].size(); k += 2) inputs.back().wire.push_back(static_cast<std::uint8_t>(std::stoul(f[1].substr(k, 2), nullptr, 16)));
            continue;
        }
        inputs.push_back({f[0][0], std::vector<std::string>(f.begin() + 1, f.end()), {}, {}});
    }
    return frames;
}
inline MAOniColonyTransfer transfer_of(const Input& in) {
    const auto& f = in.fields; require(in.kind == 'T' && f.size() == 12, "Invalid journal transfer");
    MAOniColonyTransfer t{};
    t.public_cell = static_cast<std::uint32_t>(std::stoul(f[0])); t.native_index = static_cast<std::uint32_t>(std::stoul(f[1]));
    t.kind = static_cast<std::uint32_t>(std::stoul(f[2])); t.radius = static_cast<std::uint32_t>(std::stoul(f[3]));
    t.disease = static_cast<std::uint32_t>(std::stoul(f[4])); t.mass_kg = hex(f[5]); t.temperature_k = hex(f[6]); t.disease_count = hex(f[7]);
    t.callback = std::stoi(f[11]); return t;
}
inline std::uint64_t job_of(const Input& in) { return std::stoull(in.fields[9]); }
inline bool same_request(const MAOniColonyTransfer& a, const MAOniColonyTransfer& b) {
    return a.public_cell == b.public_cell && a.native_index == b.native_index && a.kind == b.kind && a.radius == b.radius && a.disease == b.disease &&
        std::memcmp(&a.mass_kg, &b.mass_kg, 8) == 0 && std::memcmp(&a.temperature_k, &b.temperature_k, 8) == 0 &&
        std::memcmp(&a.disease_count, &b.disease_count, 8) == 0 && (a.callback == -1) == (b.callback == -1);
}

// Restores `saved` and feeds it the journal's frames from the first one after
// it, `count` of them. `release` releases the saved callback-owned requests
// instead of rebinding them (the previous save policy, a negative control).
// after_frame(number, frame) runs once each advanced frame has completed and
// its receipts are acknowledged. Returns the frames advanced.
template <class AfterFrame>
std::size_t replay(const std::vector<std::uint8_t>& saved, const std::vector<std::pair<std::uint64_t, Frame>>& frames,
    std::uint64_t count, bool release, std::uint64_t& ticket, AfterFrame&& after_frame) {
    using namespace ma::oni::colony;
    const auto parts = parse(saved);
    restore(parts.config, parts.materials, saved, 1); enable_precise_receipts(true);
    if (const auto awaiting = callback_rebind_state().first) {
        // Session-local callbacks never enter owner state; any bound value replays.
        std::vector<MAOniColonyCallbackRebind> bind(awaiting);
        for (std::uint32_t k = 0; k < awaiting; ++k) bind[k] = {static_cast<std::int32_t>(1000000 + k), release ? 0U : 1U};
        rebind_callbacks(bind);
    }
    const auto first = parts.completed + 1; std::size_t compared = 0;
    bool resumed = false;
    std::size_t restored_packet=0;bool tail_replayed=false;
    std::array<std::size_t,5> previous_cuts{};
    const std::array<std::size_t,5> saved_counts{parts.transfers.size(),parts.heat.size(),parts.terrain.size(),parts.freezes,parts.germs};
    for (const auto& [number, frame] : frames) {
        if (number < first && !(frame.flush && frame.after_checkpoint && number==parts.completed)) continue;
        if (number >= first + count) break;
        std::size_t skip_t = 0, skip_h = 0, skip_r = 0, skip_f = 0, skip_g = 0;
        if (!resumed) {
            // The restored queue is the start of the first resumed frame's inputs.
            std::vector<const Input*> t, h, r;
            for (const auto& in : frame.inputs) {if(in.kind=='T')t.push_back(&in);if(in.kind=='H')h.push_back(&in);if(in.kind=='R')r.push_back(&in);}
            if(parts.version<14) {
                require(parts.transfers.size() <= t.size() && parts.heat.size() <= h.size() && parts.terrain.size() <= r.size(),
                    "The restored queue holds inputs the uninterrupted frame did not commit");
                for (std::size_t k = 0; k < parts.transfers.size(); ++k)
                    require(same_request(parts.transfers[k], transfer_of(*t[k])), "Restored queued transfer " + std::to_string(k) + " differs from the uninterrupted input");
                skip_t = parts.transfers.size(); skip_h = parts.heat.size(); skip_r = parts.terrain.size(); skip_f = parts.freezes; skip_g = parts.germs;
            }
            resumed = true;
        }
        const bool restored_activity_frame=parts.version>=14 && !frame.flush && restored_packet<parts.activity.size();
        if(parts.version>=14) {
            require(frame.activity,"An activity owner requires an activity input journal");
            std::array<std::size_t,5> skip{};std::size_t transfer_begin=0;
            if(restored_activity_frame) {
                const auto& p=parts.activity[restored_packet];
                require(p.sequence==frame.activity_sequence && std::bit_cast<std::uint64_t>(p.seconds)==std::bit_cast<std::uint64_t>(frame.seconds),
                    "Restored activity frame differs from the journal");
                require(p.actors==frame.actors,"Restored actor census differs from the journal");
                transfer_begin=previous_cuts[0];
                for(std::size_t k=0;k<skip.size();++k){require(p.cuts[k]>=previous_cuts[k],"Restored activity cuts are unordered");skip[k]=p.cuts[k]-previous_cuts[k];}
                previous_cuts=p.cuts;++restored_packet;
            } else if(!tail_replayed) {
                const auto cuts=parts.activity.empty()?std::array<std::size_t,5>{}:parts.activity.back().cuts;
                transfer_begin=cuts[0];
                for(std::size_t k=0;k<skip.size();++k){require(saved_counts[k]>=cuts[k],"Restored activity cut exceeds its inputs");skip[k]=saved_counts[k]-cuts[k];}
                tail_replayed=true;
            }
            std::vector<const Input*> transfers_in;for(const auto& in:frame.inputs)if(in.kind=='T')transfers_in.push_back(&in);
            require(skip[0]<=transfers_in.size(),"Restored activity transfers exceed the journal frame");
            for(std::size_t k=0;k<skip[0];++k)require(same_request(parts.transfers[transfer_begin+k],transfer_of(*transfers_in[k])),
                "Restored activity transfer differs from the journal");
            skip_t=skip[0];skip_h=skip[1];skip_r=skip[2];skip_f=skip[3];skip_g=skip[4];
        }
        std::vector<MAOniColonyTransfer> ordinary; std::vector<MAOniColonyHeat> heat; std::vector<MAOniColonyTerrain> terrain;
        std::vector<MAOniColonyTransfer> native_transfers;
        std::vector<ReplayFreeze> freezes; std::vector<MAOniColonyGermEdit> germs;
        const auto flush = [&] { if (!ordinary.empty()) {
            if(restored_activity_frame)native_transfers.insert(native_transfers.end(),ordinary.begin(),ordinary.end());else queue_transfers(ordinary);
            ordinary.clear(); } };
        for (const auto& in : frame.inputs) {
            if (in.kind == 'T') {
                if (skip_t) { --skip_t; continue; }
                const auto t = transfer_of(in); const auto token = job_of(in);
                if (!token) { ordinary.push_back(t); continue; }
                require(!restored_activity_frame,"A native replay suffix cannot own a durable managed job");
                flush();
                if (!in.wire.empty()) (void)queue_moving_parcel(status().session, token, in.wire);
                else {
                    std::vector<MAOniColonyPassive> passives; for (const auto& [id, q] : in.passives) passives.push_back({id, q});
                    (void)queue_pipe_job(token, t, passives);
                }
            } else if (in.kind == 'H') { if (skip_h) { --skip_h; continue; }
                heat.push_back({static_cast<std::uint32_t>(std::stoul(in.fields[0])),
                    in.fields.size()>3?static_cast<std::uint32_t>(std::stoul(in.fields[3])):0u,hex(in.fields[1]),hex(in.fields[2])});
            } else if (in.kind == 'R') { if (skip_r) { --skip_r; continue; }
                terrain.push_back({static_cast<std::uint32_t>(std::stoul(in.fields[0])), static_cast<std::uint32_t>(std::stoul(in.fields[1]))});
            } else if (in.kind == 'F') { if (skip_f) { --skip_f; continue; } freezes.push_back({static_cast<std::uint32_t>(std::stoul(in.fields[0])), static_cast<std::uint32_t>(std::stoul(in.fields[1])), hex(in.fields[2])}); }
            else if (in.kind == 'G') { if (skip_g) { --skip_g; continue; } germs.push_back({static_cast<std::uint32_t>(std::stoul(in.fields[0])), static_cast<std::uint32_t>(std::stoul(in.fields[1])), hex(in.fields[2])}); }
        }
        flush();
        require(skip_t+skip_h+skip_r+skip_f+skip_g==0,"Restored inputs exceed the committed journal frame");
        if(frame.vents)set_vent_cells(*frame.vents);
        if(frame.forces)replace_face_forces(*frame.forces);
        if(frame.options)require(change_options(status().session,*frame.options),"Replayed options change was refused");
        if(frame.energy)set_source_energy(status().session,*frame.energy);
        if(frame.activity && frame.mode>=0)set_activity_tiers(status().session,frame.mode!=0);
        if(restored_activity_frame) {
            claim_activity_frame(status().session,frame.activity_sequence,frame.seconds);
            queue_replay_activity_native(status().session,ticket+1,native_transfers,heat,terrain,freezes,germs,frame.touched);
        } else {
            if (!heat.empty()) queue_heat(heat);
            if (!terrain.empty()) queue_terrain(terrain);
            if (!freezes.empty()) queue_replay_freezes(freezes);
            if (!germs.empty()) queue_germs(germs);
            if(frame.activity)require(queue_activity(status().session,frame.seconds,frame.actors,frame.touched)==frame.activity_sequence,
                "Replayed activity submission sequence differs");
        }
        if (frame.flush) { (void)flush_inputs(); }
        else { require(frame.frames == 1, "Coalesced native frames are not replayed"); advance(++ticket, frame.seconds, 2); wait(); }
        require(status().state == 1, "Replayed owner faulted: " + std::string(status().error));
        std::vector<MAOniColonyReceipt> receipts(1u << 16);
        for (std::uint32_t n; (n = copy_receipts(receipts.data(), static_cast<std::uint32_t>(receipts.size()))) != 0;) ack_receipts(receipts[n - 1].sequence);
        if (frame.flush) continue;
        after_frame(number, frame);
        ++compared;
    }
    return compared;
}
} // namespace ma::oni::colony::journal_replay
