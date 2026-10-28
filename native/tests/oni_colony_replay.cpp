// Owner continuation under identical inputs (docs/colony-owner.md#saves).
//
//   ma_oni_colony_replay SAVED.owner JOURNAL.txt DISEASES.bin REFERENCE_PREFIX FRAMES [--menu]
//   ma_oni_colony_replay --self-test EMPTY_DIRECTORY
//
// SAVED.owner is the owner image of an actual save, JOURNAL.txt the
// uninterrupted branch's committed inputs (MA_COLONY_INPUT_JOURNAL_DIR),
// DISEASES.bin the game's disease table from the same journal directory and
// REFERENCE_PREFIX-<frame>.owner the uninterrupted branch's held images
// (MA_COLONY_CONTINUATION_DUMP). The saved owner is restored in this fresh
// process (a cold load) and, with --menu, stopped and restored again in it
// (a menu reload); each restored owner then receives exactly the
// uninterrupted branch's inputs. Every frame's complete owner state (wrapper
// ledgers and counters, germ clocks and accounts, held precision inventory
// and the physical image; not the next frame's queued inputs) must equal the
// uninterrupted image byte for byte. The restored queue must be the prefix
// of the first resumed frame's inputs, callback-owned requests included.
#include "../src/oni_colony.hpp"
#include "../src/oni_sha256.hpp"
#include <chrono>
#include <array>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <span>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool ok, const std::string& why) { if (!ok) throw std::runtime_error(why); }
std::vector<std::uint8_t> read_file(const std::string& path) {
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
// Split an owner wrapper (8..14) into committed state and queued inputs.
OwnerParts parse(const std::vector<std::uint8_t>& image) {
    OwnerParts out; Cursor c{image};
    require(image.size() > 72 && std::memcmp(image.data(), "MACOLN01", 8) == 0, "Not an owner image");
    c.skip(8); auto version = c.get<std::uint32_t>();
    if (version == 13) { version = c.get<std::uint32_t>(); (void)c.get<std::uint32_t>(); }
    require((version >= 8 && version <= 12) || version==14, "Unsupported owner wrapper");
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
    bool activity=false;std::uint64_t activity_sequence=0;int mode=-1;std::vector<std::uint32_t> actors,touched; };
double hex(const std::string& text) { return std::strtod(text.c_str(), nullptr); }
std::vector<std::pair<std::uint64_t, Frame>> journal(const std::string& path,const std::string& checkpoint_digest={}) {
    std::ifstream in(path); require(in.good(), "Cannot read journal " + path);
    std::vector<std::pair<std::uint64_t, Frame>> frames; std::string line;bool after_checkpoint=false;
    while (std::getline(in, line)) {
        std::istringstream words(line); std::vector<std::string> f; for (std::string w; words >> w;) f.push_back(w);
        if (f.empty() || f[0] == "session") continue;
        if(f[0]=="checkpoint") {
            require(f.size()==3,"Invalid checkpoint journal marker");
            if(!after_checkpoint && f[2]==checkpoint_digest){frames.clear();after_checkpoint=true;}
            continue;
        }
        if (f[0] == "frame") {
            Frame frame; frame.flush = f[3] == "1"; frame.seconds = hex(f[5]);frame.after_checkpoint=after_checkpoint;
            for (std::size_t k = 8; k + 1 < f.size(); k += 2) if (f[k] == "frames") frame.frames = std::stoull(f[k + 1]);
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
MAOniColonyTransfer transfer_of(const Input& in) {
    const auto& f = in.fields; require(in.kind == 'T' && f.size() == 12, "Invalid journal transfer");
    MAOniColonyTransfer t{};
    t.public_cell = static_cast<std::uint32_t>(std::stoul(f[0])); t.native_index = static_cast<std::uint32_t>(std::stoul(f[1]));
    t.kind = static_cast<std::uint32_t>(std::stoul(f[2])); t.radius = static_cast<std::uint32_t>(std::stoul(f[3]));
    t.disease = static_cast<std::uint32_t>(std::stoul(f[4])); t.mass_kg = hex(f[5]); t.temperature_k = hex(f[6]); t.disease_count = hex(f[7]);
    t.callback = std::stoi(f[11]); return t;
}
std::uint64_t job_of(const Input& in) { return std::stoull(in.fields[9]); }
bool same_request(const MAOniColonyTransfer& a, const MAOniColonyTransfer& b) {
    return a.public_cell == b.public_cell && a.native_index == b.native_index && a.kind == b.kind && a.radius == b.radius && a.disease == b.disease &&
        std::memcmp(&a.mass_kg, &b.mass_kg, 8) == 0 && std::memcmp(&a.temperature_k, &b.temperature_k, 8) == 0 &&
        std::memcmp(&a.disease_count, &b.disease_count, 8) == 0 && (a.callback == -1) == (b.callback == -1);
}

bool release_restored = false; // negative control: the previous save policy
std::size_t replay(const std::vector<std::uint8_t>& saved, const std::vector<std::pair<std::uint64_t, Frame>>& frames,
    const std::string& prefix, std::uint64_t count, bool menu, std::uint64_t& ticket) {
    using namespace ma::oni::colony;
    const auto parts = parse(saved);
    restore(parts.config, parts.materials, saved, 1); enable_precise_receipts(true);
    if (const auto awaiting = callback_rebind_state().first) {
        // Session-local callbacks never enter owner state; any bound value replays.
        std::vector<MAOniColonyCallbackRebind> bind(awaiting);
        for (std::uint32_t k = 0; k < awaiting; ++k) bind[k] = {static_cast<std::int32_t>(1000000 + k), release_restored ? 0U : 1U};
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
        const auto reference = parse(read_file(prefix + "-" + std::to_string(number) + ".owner"));
        const auto actual = parse(checkpoint());
        require(actual.completed == number && reference.completed == number, "Replayed frame number differs");
        if (actual.state != reference.state) {
            std::size_t at = 0; while (at < actual.state.size() && at < reference.state.size() && actual.state[at] == reference.state[at]) ++at;
            throw std::runtime_error((menu ? "menu" : "cold") + std::string(" replay differs from the uninterrupted owner at frame ") + std::to_string(number) +
                " (state byte " + std::to_string(at) + " of " + std::to_string(actual.state.size()) + "/" + std::to_string(reference.state.size()) + ")");
        }
        ++compared;
    }
    require(compared == count, "The journal or references do not cover every requested frame");
    return compared;
}

// --self-test DIR: a synthetic owner journals breaths, a consumer and
// unanswered injections, is saved with callback-owned requests in flight, and
// continues; the saved image must then replay byte for byte, cold and menu.
std::vector<std::string> self_test(const std::string& parent,bool activity=false) {
    using namespace ma::oni::colony;
    // A fresh private directory: journals and dumps never overwrite.
    const auto directory = (std::filesystem::path(parent) / ("replay-self-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))).string();
    require(std::filesystem::create_directory(directory), "Cannot create a fresh self-test directory");
#if defined(_WIN32)
    require(_putenv_s("MA_COLONY_INPUT_JOURNAL_DIR", directory.c_str()) == 0, "Cannot select the journal");
#else
    require(setenv("MA_COLONY_INPUT_JOURNAL_DIR", directory.c_str(), 1) == 0, "Cannot select the journal");
#endif
    const auto material = [](const char* id, std::int32_t hash, std::uint32_t index, double molar, double c) {
        MAOniColonyMaterial m{}; m.struct_size = sizeof(m); m.abi_version = MA_ONI_COLONY_ABI_VERSION; m.element_hash = hash; m.native_index = index;
        m.phase = 1; m.molar_mass_kg_per_mol = molar; m.specific_heat_j_per_kg_k = c; m.conductivity_w_per_m_k = .02;
        m.low_temperature_k = 50; m.high_temperature_k = 1e4; std::snprintf(m.id, sizeof(m.id), "%s", id); return m;
    };
    constexpr std::uint32_t oxygen = 3, carbon = 4, vacuum = 9, width = 6, height = 4;
    const std::vector<MAOniColonyMaterial> mats{material("Oxygen", 11, oxygen, .016, 1000), material("CarbonDioxide", 12, carbon, .044, 850)};
    MAOniColonyConfig cfg{}; cfg.struct_size = sizeof(cfg); cfg.abi_version = MA_ONI_COLONY_ABI_VERSION; cfg.vacuum_index = vacuum;
    cfg.grid_width = cfg.width = width; cfg.grid_height = cfg.height = height; cfg.maximum_timestep_s = .1;
    if(activity)cfg.flags=MA_ONI_COLONY_PHYSICS_CONDUCTION|MA_ONI_COLONY_GAME_KERNEL|MA_ONI_COLONY_GAME_SURFACE|MA_ONI_COLONY_GAME_ACTIVITY;
    std::vector<MAOniColonyCell> cells(width * height, MAOniColonyCell{0, MA_ONI_COLONY_SOLID, 0, 0, 0, 0});
    for (std::uint32_t y = 1; y + 1 < height; ++y) for (std::uint32_t x = 1; x + 1 < width; ++x)
        cells[x + y * width] = {x % 2 ? oxygen : carbon, MA_ONI_COLONY_FLUID, 1.1f + .1f * static_cast<float>(x), 290.f + 3.f * static_cast<float>(y), 3, 103};
    set_diseases({}, {}, 0); start(cfg, mats, cells, 1); enable_precise_receipts(true);
    const auto source_session=status().session;
    std::string disease_path;
    for(const auto& entry:std::filesystem::directory_iterator(directory))
        if(entry.path().filename().string().starts_with("diseases-") && entry.path().extension()==".bin")disease_path=entry.path().string();
    require(!disease_path.empty(),"Self-test disease journal was not written");
    const auto write = [&](const std::string& name, const std::vector<std::uint8_t>& bytes) {
        std::ofstream out(directory + "/" + name, std::ios::binary); out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        require(out.good(), "Cannot write " + name);
    };
    const auto enqueue=[&](std::uint64_t frame) {
        std::vector<MAOniColonyTransfer> inputs(4);
        inputs[0] = {width + 1, oxygen, MA_ONI_COLONY_EXTRACT, 1, static_cast<std::int32_t>(100 + frame), 0, .004 + 1e-4 * static_cast<double>(frame), 0, 0}; // a breath
        inputs[1] = {width + 4, 0, MA_ONI_COLONY_EXTRACT_GAS | MA_ONI_COLONY_CONSUMER, 1, 0x7e000001, 0, .003, 0, 0};                       // a consumer
        inputs[2] = {2 * width + 2, carbon, MA_ONI_COLONY_INJECT, 0, -1, 0, .002, 310, 0};                                                 // a native change
        inputs[3] = {2 * width + 3, oxygen, MA_ONI_COLONY_INJECT, 0, static_cast<std::int32_t>(200 + frame), 0, .001, 305, 0};            // an answered output
        queue_transfers(inputs);
        const MAOniColonyHeat heat{width + 2, 0, 7.5 * static_cast<double>(frame), 2000}; queue_heat(std::span(&heat, 1));
        if(activity) {
            if(frame==6)set_activity_tiers(status().session,false);
            if(frame==7)set_activity_tiers(status().session,true);
            const std::vector<std::uint32_t> actors=frame==6?std::vector<std::uint32_t>{}:
                std::vector<std::uint32_t>{frame>=7?width+4:width+1};
            (void)queue_activity(status().session,static_cast<double>(.2f),actors,{});
        }
    };
    for (std::uint64_t frame = 1; frame <= 8; ++frame) {
        if(!activity || frame!=5)enqueue(frame);
        if (frame == 4) {
            if(activity)enqueue(5); // two immutable submissions survive cold/menu restore
            write("saved.owner", checkpoint());
        }
        advance(frame, activity?static_cast<double>(.2f):.2, 2); wait(); require(status().state == 1, "Self-test owner faulted");
        std::vector<MAOniColonyReceipt> receipts(64); const auto n = copy_receipts(receipts.data(), 64); if (n) ack_receipts(receipts[n - 1].sequence);
        write("reference-" + std::to_string(frame) + ".owner", checkpoint());
    }
    stop();
    return {directory + "/saved.owner", directory + "/inputs-"+std::to_string(source_session)+".txt",
        disease_path, directory + "/reference", "5", "--menu"};
}
int run(std::vector<std::string> argv) {
    try {
        const auto argc = argv.size() + 1;
        require(argc == 6 || (argc == 7 && argv[5] == "--menu"),
            "Usage: ma_oni_colony_replay SAVED.owner JOURNAL.txt DISEASES.bin REFERENCE_PREFIX FRAMES [--menu]");
        const auto saved = read_file(argv[0]);require(saved.size()>=64,"Truncated saved owner");
        const auto frames = journal(argv[1],std::string(saved.end()-64,saved.end()));const auto diseases = read_file(argv[2]);
        const auto count = std::stoull(argv[4]);
        require(diseases.size() >= 12, "Invalid disease table");
        std::uint32_t counts[3]; std::memcpy(counts, diseases.data(), 12);
        require(diseases.size() == 12 + counts[0] * sizeof(MAOniColonyDisease) + counts[1] * sizeof(MAOniColonyDiseaseGrowth), "Disease table size differs");
        std::vector<MAOniColonyDisease> table(counts[0]); std::vector<MAOniColonyDiseaseGrowth> growth(counts[1]);
        if (counts[0]) std::memcpy(table.data(), diseases.data() + 12, counts[0] * sizeof(MAOniColonyDisease));
        if (counts[1]) std::memcpy(growth.data(), diseases.data() + 12 + counts[0] * sizeof(MAOniColonyDisease), counts[1] * sizeof(MAOniColonyDiseaseGrowth));
        ma::oni::colony::set_diseases(table, growth, counts[2]);
        std::uint64_t ticket = 0;
        const auto cold = replay(saved, frames, argv[3], count, false, ticket);
        std::cout << "PASS cold replay: " << cold << " frames equal the uninterrupted owner (state SHA256 " << ma::oni::hash_memory(saved.data(), saved.size()).substr(0, 16) << " source)\n";
        if (argc == 7) {
            ma::oni::colony::stop();
            const auto menu = replay(saved, frames, argv[3], count, true, ticket);
            std::cout << "PASS menu replay: " << menu << " frames equal the uninterrupted owner after an in-process restore\n";
        }
        ma::oni::colony::stop();
        return 0;
    } catch (const std::exception& error) {
        ma::oni::colony::stop();
        std::cerr << "REPLAY FAIL: " << error.what() << '\n'; return 1;
    }
}
}

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.size() == 2 && args[0] == "--self-test") {
        const auto parent=args[1];
        for(const bool activity:{false,true}) {
            release_restored=false;
            try { args = self_test(parent,activity); } catch (const std::exception& error) { std::cerr << "REPLAY FAIL: " << error.what() << '\n'; return 1; }
            if (run(args) != 0) return 1;
            // Dropping saved callback-owned inputs must diverge immediately.
            release_restored = true; args.pop_back();
            std::cerr << "expected failure follows (released callback-owned requests):\n";
            if (run(args) == 0) { std::cerr << "REPLAY FAIL: releasing in-flight requests was not detected\n"; return 1; }
            std::cout << "PASS negative control: releasing the saved in-flight requests diverges at the first resumed frame\n";
            std::filesystem::remove_all(std::filesystem::path(args[0]).parent_path());
        }
        return 0;
    }
    return run(args);
}
