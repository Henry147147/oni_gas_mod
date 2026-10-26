#include "mixed_atmospheres/oni_conduit_contact.h"
#include "mixed_atmospheres/oni_backend.h"
#include "oni_sha256.hpp"
#include "oni_domain.hpp"
#include "oni_colony.hpp"
#include "oni_floor_capture.hpp"
#include "oni_body_heat_trace.hpp"
#include "oni_chunk_heat_trace.hpp"
#include "oni_storage_heat_native.hpp"
#include "oni_conduit_contact.hpp"
#include "oni_windows_lifecycle_trace.hpp"
#include "oni_funchook_storage.hpp"
#include "mixed_atmospheres/fluid_thermodynamics.hpp"
#include <funchook.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstdio>
#include <cfenv>
#include <cerrno>
#include <cmath>
#include <chrono>
#include <cstdlib>
#include <xmmintrin.h>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#include <link.h>
#include <unistd.h>
#endif

namespace {
static_assert(sizeof(void*) == 8, "ONI native observer requires x64");
static_assert(sizeof(MAOniObserverConfig) == 24);
static_assert(sizeof(MAOniObserverStatus) == 528);
static_assert(sizeof(MAOniDomainChunkHeatStatus) == 112);
static_assert(sizeof(MAOniDomainSolidHeatCell) == 48);
static_assert(sizeof(MAOniDomainSolidHeatEndpoint) == 64);
static_assert(sizeof(MAOniDomainSolidCellState) == 64);
static_assert(sizeof(MAOniDomainPublicationInfo) == 64);
static_assert(sizeof(MAOniDomainPhysics) == 40);
static_assert(sizeof(MAOniTraceEvent) == 32);
static_assert(sizeof(MAOniStorageHeatReceipt) == 48);
static_assert(MA_ONI_CHUNK_HEAT + 1 == MA_ONI_HOOK_COUNT);
struct HookProfile { std::uint32_t index; std::uintptr_t rva; const char* hash; };
#include "oni_profiles.hpp"

struct Failure : std::runtime_error {
    MAOniResult result;
    Failure(MAOniResult code, const char* message) : std::runtime_error(message), result(code) {}
};
thread_local std::array<char,2048> last_error{};
std::mutex lifecycle;
std::array<void*,MA_ONI_HOOK_COUNT> originals{};
std::array<void*,2> publication_originals{};
struct Counter { std::atomic<std::uint64_t> entries{}, exits{}; };
std::array<Counter,MA_ONI_HOOK_COUNT> counters;
std::atomic<std::uint64_t> active{}, positive_steps{}, trace_reserved{}, thread_sequence{};
std::atomic<std::uint64_t> native_frame_epochs{};
std::recursive_mutex native_save_mutex;
bool native_save_open{}; // protected by native_save_mutex
thread_local bool paired_save_permit{}; // consumed by exactly one BeginSave
struct DomainPair {
    MAOniDomainPairStatus status{};
    std::vector<std::uint8_t> native,physical;
    std::int32_t x{},y{};
    bool include_actors{};
    MAOniResult error_code{MA_ONI_INTERNAL_ERROR};
    std::array<char,256> error{};
};
std::mutex pair_mutex;
std::unique_ptr<DomainPair> captured_pair;
std::uint64_t pair_sequence{},last_released_pair{}; // protected by pair_mutex
std::atomic<bool> pair_pending{};
void fulfill_paired_capture(); // Executed only at the native worker barrier.
std::array<MAOniTraceEvent,MA_ONI_TRACE_CAPACITY> traces{};
std::array<std::atomic<bool>,MA_ONI_TRACE_CAPACITY> trace_ready{};
funchook_t* detours{};
std::uintptr_t image_base{};
std::uint64_t installed_mask{};
bool installed{}, recovery_required{};
ma::oni::storage_heat::Monitor storage_heat;
// Both modules remain resident. Never free cached P/Invoke targets or native
// trampolines while any simulator could retain their addresses.
void* game_module{};

std::uint64_t native_thread() noexcept {
    thread_local const auto thread=thread_sequence.fetch_add(1,std::memory_order_relaxed)+1;
    return thread;
}
struct PendingTimestep { std::uint64_t ticket{}; float seconds{}; };
thread_local PendingTimestep pending_timestep;
// Pacing (docs/colony-owner.md#pacing): positive native frames completed.
// Cell temperature pairs can execute on native job threads. The outer frame
// fence owns one interval for all of them; thread-local duration misses those
// contacts even though body components run on the frame's main worker.
std::atomic<std::uint32_t> native_heat_contact_bits{};
std::atomic<std::uint64_t> native_floor_contact_ticket{};
struct FloorDiagnostics {
    std::mutex mutex;
    ma::oni::colony::detail::FloorCapture admission,vacuum,prepared,solids;
    bool enabled=false;
    void configure() {
        if(enabled)return;
        const auto* path=std::getenv("MA_COLONY_FLOOR_ADMISSION_DUMP");
        if(!path || !*path)return;
        enabled=true;admission.configure(path);
        vacuum.configure((std::string(path)+".vacuum").c_str());
        prepared.configure((std::string(path)+".prepared").c_str());
        solids.configure((std::string(path)+".solids").c_str());
    }
};
FloorDiagnostics& floor_diagnostics() {static FloorDiagnostics value;return value;}
// A domain save must exclude native cell processing as well as publication.
// BeginFrame may wait for the main thread, so acquire only after it returns,
// at ProcessNextFrame, and release after EndFrame finishes.
thread_local std::unique_lock<std::mutex> worker_frame_fence;

void trace(std::uint32_t hook, bool entering, std::uint32_t bits=0) noexcept {
    if (hook > MA_ONI_UPDATE_DATA && (hook < MA_ONI_STEP || hook > MA_ONI_END_FRAME)) return;
    const auto thread = native_thread();
    const auto index = trace_reserved.fetch_add(1, std::memory_order_relaxed);
    if (index >= traces.size()) return;
    const auto i = static_cast<std::size_t>(index);
    traces[i] = {index+1, thread, hook, entering ? 1U : 0U, bits, 0U};
    trace_ready[i].store(true, std::memory_order_release);
}
struct Call {
    std::uint32_t hook, bits{};
    explicit Call(std::uint32_t id) noexcept : hook(id) {
        active.fetch_add(1, std::memory_order_relaxed);
        counters[hook].entries.fetch_add(1, std::memory_order_relaxed);
        trace(hook,true);
    }
    ~Call() {
        trace(hook,false,bits);
        counters[hook].exits.fetch_add(1, std::memory_order_relaxed);
        active.fetch_sub(1, std::memory_order_release);
    }
};
// Separate additive reader bindings do not change the observer ABI's hook
// indices or statistics layout. They still participate in safe detach.
struct PublicationCall {
    PublicationCall() { active.fetch_add(1, std::memory_order_relaxed); }
    ~PublicationCall() { active.fetch_sub(1, std::memory_order_release); }
};
template<class Function> Function original(std::uint32_t index) noexcept {
    return reinterpret_cast<Function>(originals[index]);
}
#define FORWARD(name, index, result, args, invocation) \
    result name args { Call call(index); using Function = result (*) args; return original<Function>(index) invocation; }
using GameCallback = int (*)(int,void*);
FORWARD(on_initialize, MA_ONI_INITIALIZE, void, (GameCallback callback), (callback))
void on_shutdown() {
    Call call(MA_ONI_SHUTDOWN);
    storage_heat.reset();
    ma::oni::windows_lifecycle_trace("native shutdown entered");
#if defined(_WIN32)
    std::fprintf(stderr,"[Mixed Atmospheres] Windows native shutdown entered.\n");
#endif
    original<void (*)()>(MA_ONI_SHUTDOWN)();
    ma::oni::windows_lifecycle_trace("original native shutdown completed");
#if defined(_WIN32)
    std::fprintf(stderr,"[Mixed Atmospheres] Windows original native shutdown completed.\n");
#endif
    ma::oni::shutdown_domain();
    ma::oni::windows_lifecycle_trace("sealed domain shutdown completed");
#if defined(_WIN32)
    std::fprintf(stderr,"[Mixed Atmospheres] Windows sealed-domain workers retired.\n");
#endif
    ma::oni::colony::stop();
#if defined(_WIN32)
    std::fprintf(stderr,"[Mixed Atmospheres] Windows native owners retired.\n");
#endif
    if (pair_pending.load(std::memory_order_acquire)) {
        std::lock_guard lock(pair_mutex);
        if (captured_pair && captured_pair->status.state==1) {
            captured_pair->status.state=3; captured_pair->error_code=MA_ONI_BUSY;
            std::snprintf(captured_pair->error.data(),captured_pair->error.size(),"Native simulator closed before paired capture");
        }
        pair_pending.store(false,std::memory_order_release);
    }
}
FORWARD(on_message, MA_ONI_MESSAGE, void*, (int id,int size,std::uint8_t* data), (id,size,data))
FORWARD(on_messages, MA_ONI_MESSAGES, void*, (int id,int size,int count,std::uint8_t* data), (id,size,count,data))
std::uint8_t* on_begin_save(int* size,int x,int y) {
    Call call(MA_ONI_BEGIN_SAVE);
    std::lock_guard save_lock(native_save_mutex);
    const bool permit=std::exchange(paired_save_permit,false);
    if (ma::oni::domain_active() && !permit) {
        if (size) *size=0;
        std::snprintf(last_error.data(),last_error.size(),"The custom native domain requires an integrated checkpoint; native scalar saving is disabled");
        return nullptr;
    }
    auto* result=original<std::uint8_t* (*)(int*,int,int)>(MA_ONI_BEGIN_SAVE)(size,x,y);
    native_save_open=result!=nullptr; return result;
}
void on_end_save() {
    Call call(MA_ONI_END_SAVE); std::lock_guard save_lock(native_save_mutex);
    original<void (*)()>(MA_ONI_END_SAVE)(); native_save_open=false;
}
FORWARD(on_main, MA_ONI_MAIN, void, (void* self), (self))
void on_process_frame(void* frame,void* data) {
    Call call(MA_ONI_PROCESS_FRAME);
    // Cell-pair physics runs inside ProcessNextFrame, before that function
    // returns its duration. SimFrameInfo carries elapsedSeconds at offset 0
    // in both inspected native builds.
    float seconds{};if(frame) std::memcpy(&seconds,frame,sizeof(seconds));
    native_heat_contact_bits.store(std::bit_cast<std::uint32_t>(seconds),std::memory_order_release);
    std::optional<ma::oni::body_heat_trace::Frame> body_observation;
    std::optional<ma::oni::chunk_heat_trace::Frame> chunk_observation;
    const auto ordinal=counters[MA_ONI_PROCESS_FRAME].entries.load(std::memory_order_relaxed);
    if(ma::oni::body_heat_trace::Frame::enabled())body_observation.emplace(ordinal,frame,data);
    if(ma::oni::chunk_heat_trace::Frame::enabled())chunk_observation.emplace(ordinal,frame,data);
    storage_heat.process(frame,data,ordinal,[&]{original<void (*)(void*,void*)>(MA_ONI_PROCESS_FRAME)(frame,data);});
}
float on_next_frame(void* self,void* data) {
    Call call(MA_ONI_NEXT_FRAME);
    // Fence inactive frames too: activation must not become visible halfway
    // through native physics that began before the owner existed.
    if (!worker_frame_fence.owns_lock())
        worker_frame_fence=std::unique_lock<std::mutex>(ma::oni::domain_frame_mutex());
    const float dt = original<float (*)(void*,void*)>(MA_ONI_NEXT_FRAME)(self,data);
    native_heat_contact_bits.store(std::bit_cast<std::uint32_t>(dt),std::memory_order_release);
    call.bits = std::bit_cast<std::uint32_t>(dt);
    if (dt > 0) pending_timestep={positive_steps.fetch_add(1,std::memory_order_relaxed)+1,dt};
    return dt;
}
void observe_completed_solid_cell(void* data) {
    MAOniDomainSolidHeatEndpoint endpoint{};ma::oni::copy_domain_solid_heat_endpoint(endpoint);
    if(endpoint.state==1 && endpoint.contact_frame) {
        try {
            if(!data) throw std::logic_error("Completed solid cell has no native owner");
            auto* sim=static_cast<std::byte*>(data);int pitch{},rows{};void* cells{};void* updated{};
            std::memcpy(&pitch,sim,sizeof(pitch));std::memcpy(&rows,sim+4,sizeof(rows));
            std::memcpy(&cells,sim+32,sizeof(cells));std::memcpy(&updated,sim+40,sizeof(updated));
            if(pitch<=2 || rows<=2 || !cells || !updated) throw std::logic_error("Completed solid cell grid is incomplete");
            const auto width=static_cast<std::uint64_t>(pitch-2),public_cell=static_cast<std::uint64_t>(endpoint.solid_public_cell);
            if(public_cell>=width*static_cast<std::uint64_t>(rows-2)) throw std::logic_error("Completed solid cell is outside its native grid");
            const auto index=public_cell%width+1+(public_cell/width+1)*static_cast<std::uint64_t>(pitch);
#if defined(_WIN32)
            constexpr std::size_t element_offset=8,temperature_offset=40,mass_offset=72,insulation_offset=136;
#else
            constexpr std::size_t element_offset=0,temperature_offset=24,mass_offset=48,insulation_offset=96;
#endif
            float* old_temperatures{};float* next_temperatures{};float* masses{};std::uint16_t* elements{};std::uint8_t* insulation{};
            std::memcpy(&old_temperatures,static_cast<std::byte*>(cells)+temperature_offset,sizeof(old_temperatures));
            std::memcpy(&next_temperatures,static_cast<std::byte*>(updated)+temperature_offset,sizeof(next_temperatures));
            std::memcpy(&masses,static_cast<std::byte*>(updated)+mass_offset,sizeof(masses));
            std::memcpy(&elements,static_cast<std::byte*>(updated)+element_offset,sizeof(elements));
            std::memcpy(&insulation,static_cast<std::byte*>(updated)+insulation_offset,sizeof(insulation));
            if(!old_temperatures || !next_temperatures || !masses || !elements || !insulation)
                throw std::logic_error("Completed solid cell arrays are incomplete");
            const auto completed=ma::oni::domain_status_under_fence();
            const MAOniDomainSolidCellState observation{64,1,1,endpoint.solid_public_cell,endpoint.session,endpoint.completed_frame,
                old_temperatures[index],next_temperatures[index],elements[index],masses[index],insulation[index],0,completed.native_ticket};
            ma::oni::record_domain_solid_cell_state(observation);
        } catch(const std::exception& error) {ma::oni::fault_domain_solid_heat(error.what());}
          catch(...) {ma::oni::fault_domain_solid_heat("Unknown completed native solid observation failure");}
    }
}
// The native cell arrays of a SimData (layouts in on_temperature below).
bool native_cells(void* data,ma::oni::colony::NativeCells& out) noexcept {
    if (!data) return false;
    auto* sim=static_cast<std::byte*>(data);int pitch{},rows{};void* arrays[2]{};
    std::memcpy(&pitch,sim,sizeof(pitch));std::memcpy(&rows,sim+4,sizeof(rows));
    std::memcpy(&arrays[0],sim+32,sizeof(void*));std::memcpy(&arrays[1],sim+40,sizeof(void*));
    if (pitch<=2 || rows<=2 || !arrays[0] || !arrays[1]) return false;
#if defined(_WIN32)
    // Pinned PDB CellSOA fields at192/224; MSVC vector data starts at+8.
    constexpr std::size_t element_offset=8,temperature_offset=40,mass_offset=72,disease_offset=200,disease_count_offset=232;
#else
    // Each field is a vector (three pointers); germs: disease index bytes
    // (255 none) and int32 counts, found by matching the two arrays at runtime.
    constexpr std::size_t element_offset=0,temperature_offset=24,mass_offset=48,disease_offset=144,disease_count_offset=168;
#endif
    for (int b=0;b<2;++b) {
        std::memcpy(&out.element[b],static_cast<std::byte*>(arrays[b])+element_offset,sizeof(void*));
        std::memcpy(&out.temperature[b],static_cast<std::byte*>(arrays[b])+temperature_offset,sizeof(void*));
        std::memcpy(&out.mass[b],static_cast<std::byte*>(arrays[b])+mass_offset,sizeof(void*));
        if (!out.element[b] || !out.temperature[b] || !out.mass[b]) return false;
        std::memcpy(&out.disease[b],static_cast<std::byte*>(arrays[b])+disease_offset,sizeof(void*));
        std::memcpy(&out.disease_count[b],static_cast<std::byte*>(arrays[b])+disease_count_offset,sizeof(void*));
        if (!out.disease[b] || !out.disease_count[b]) return false;
    }
    out.pitch=static_cast<std::uint32_t>(pitch);out.rows=static_cast<std::uint32_t>(rows);
    return true;
}
// A cell owned by the development domain or the colony owner: its native
// fluid physics is suppressed.
bool owned_cell(std::uint32_t hook,int cell) noexcept {
    return ma::oni::suppress_domain_cell(hook,cell) || ma::oni::colony::suppress(hook,cell);
}
void on_update_data(void* self,void* data) {
    Call call(MA_ONI_UPDATE_DATA);
    // UpdateData performs native physics, including ElementChunk contacts.
    // Commit their ordered boundary inputs before Step publishes GameData.
    // Keep both owners behind the same fence for the entire native update.
    std::unique_lock<std::mutex> frame_lock;
    if (!worker_frame_fence.owns_lock()) frame_lock=std::unique_lock<std::mutex>(ma::oni::domain_frame_mutex());
    const auto pending=pending_timestep; pending_timestep={};
    auto& floors=floor_diagnostics();floors.configure();
    // Observe the fields native physics will actually consume, after the
    // owner has installed its shadow. The earlier native fields may be
    // overwritten here and cannot establish an admission/time origin.
    ma::oni::colony::NativeCells shadow{};
    const bool shadowed=pending.ticket && ma::oni::colony::active() && native_cells(data,shadow);
    if (shadowed) ma::oni::colony::write_shadow(shadow);
    if(pending.ticket && floors.enabled) {
        std::lock_guard diagnostic(floors.mutex);
        try {
            ma::oni::colony::NativeCells cells{};
            if(native_cells(data,cells)) {
                (void)floors.vacuum.observe(cells,pending.ticket,pending.seconds);
                (void)floors.prepared.observe(cells,pending.ticket,pending.seconds);
                if(floors.admission.observe(cells,pending.ticket,pending.seconds))
                std::fprintf(stderr,"[Mixed Atmospheres] native floor admission captured before UpdateData ticket %llu\n",
                    static_cast<unsigned long long>(pending.ticket));
            }
        } catch(const std::exception& error) {
            floors.admission.stop();floors.vacuum.stop();floors.prepared.stop();floors.solids.stop();std::fprintf(stderr,"[Mixed Atmospheres] native floor admission capture failed: %s\n",error.what());
        }
    }
    // Colony shadow: native bodies and actors meet the owner's cells during
    // UpdateData, and what they change there is booked to the owner.
    native_heat_contact_bits.store(std::bit_cast<std::uint32_t>(pending.seconds),std::memory_order_release);
    native_floor_contact_ticket.store(floors.enabled?pending.ticket:0,std::memory_order_release);
    original<void (*)(void*,void*)>(MA_ONI_UPDATE_DATA)(self,data);
    storage_heat.sample(data,counters[MA_ONI_PROCESS_FRAME].entries.load(std::memory_order_relaxed));
    native_floor_contact_ticket.store(0,std::memory_order_release);
    native_heat_contact_bits.store(0,std::memory_order_release);
    if (shadowed && native_cells(data,shadow)) ma::oni::colony::capture_shadow(shadow,pending.ticket);
    if (pending.ticket) {
        ma::oni::advance_domain(pending.ticket,pending.seconds,native_thread());
        ma::oni::colony::advance(pending.ticket,pending.seconds,native_thread());
    }
    observe_completed_solid_cell(data);
}
float on_pressure(void* data,void* events,const void* source,void* target,int cell,std::uint16_t element,std::int8_t direction,float scale,int index) {
    Call call(MA_ONI_PRESSURE);
    // The final argument is the second cell, including the diagonal selected
    // by UpdateData. A cardinal-neighbour test cannot cover it. Retain the
    // existing conservative colony boundary policy as well.
    if (owned_cell(MA_ONI_PRESSURE,cell) || owned_cell(MA_ONI_PRESSURE,index) || ma::oni::colony::beside_owned(cell)) return 0;
    using Function=float (*)(void*,void*,const void*,void*,int,std::uint16_t,std::int8_t,float,int);
    return original<Function>(MA_ONI_PRESSURE)(data,events,source,target,cell,element,direction,scale,index);
}
void on_liquid(void* data,void* events,int cell) {
    Call call(MA_ONI_LIQUID);
    if (!owned_cell(MA_ONI_LIQUID,cell)) original<void (*)(void*,void*,int)>(MA_ONI_LIQUID)(data,events,cell);
}
void on_temperature(void* data,void* events,int a,int b) {
    Call call(MA_ONI_TEMPERATURE);
    if(const auto ticket=native_floor_contact_ticket.load(std::memory_order_acquire)) {
        auto& floors=floor_diagnostics();
        // The prepared marker identifies this tiny private arena. Other native
        // contacts never inspect files or scan requested cells. Within it the
        // current request is observed before the existing temperature hook.
        if(floors.prepared.contains_native(a) || floors.prepared.contains_native(b) || floors.vacuum.contains_native(a) || floors.vacuum.contains_native(b)) {
            std::lock_guard diagnostic(floors.mutex);
            try {ma::oni::colony::NativeCells cells{};
                const auto seconds=std::bit_cast<float>(native_heat_contact_bits.load(std::memory_order_acquire));
                if(native_cells(data,cells)) {
                    (void)floors.vacuum.observe(cells,ticket,seconds,1,a,b);
                    (void)floors.prepared.observe(cells,ticket,seconds,1,a,b);
                    (void)floors.admission.observe(cells,ticket,seconds,1,a,b);
                    (void)floors.solids.observe(cells,ticket,seconds,1,a,b);
                }
            } catch(const std::exception& error) {
                floors.admission.stop();floors.vacuum.stop();floors.prepared.stop();floors.solids.stop();
                std::fprintf(stderr,"[Mixed Atmospheres] floor pre-contact capture failed: %s\n",error.what());
            }
        }
    }
    if (ma::oni::colony::active()) {
        // Conduction between owned cells is the owner's; a pair with a native
        // cell exchanges natively with the shadow and is booked at capture.
        if (ma::oni::colony::owns(a) && ma::oni::colony::owns(b)) { (void)ma::oni::colony::suppress(MA_ONI_TEMPERATURE,a); return; }
        original<void (*)(void*,void*,int,int)>(MA_ONI_TEMPERATURE)(data,events,a,b);
        return;
    }
    const auto owned_a=owned_cell(MA_ONI_TEMPERATURE,a);
    const auto owned_b=owned_cell(MA_ONI_TEMPERATURE,b);
    if (!owned_a && !owned_b)
        original<void (*)(void*,void*,int,int)>(MA_ONI_TEMPERATURE)(data,events,a,b);
    else if(owned_a!=owned_b) {
        const auto seconds=std::bit_cast<float>(native_heat_contact_bits.load(std::memory_order_acquire));
        if(!(seconds>0)) return;
        const auto fluid=owned_a ? a : b,solid=owned_a ? b : a;
        if(!ma::oni::domain_solid_heat_registered(solid)) return;
        // These exact-build CellSOA offsets are independently inspected on
        // both native libraries. Pair indices come from the original worker.
        if(!data) {ma::oni::fault_domain_solid_heat("Native solid heat has no SimData accessor");return;}
        auto* sim=static_cast<std::byte*>(data);void* cells{};void* updated{};
        std::memcpy(&cells,sim+32,sizeof(cells));std::memcpy(&updated,sim+40,sizeof(updated));
#if defined(_WIN32)
        constexpr std::size_t element_offset=8,temperature_offset=40,mass_offset=72,insulation_offset=136;
#else
        constexpr std::size_t element_offset=0,temperature_offset=24,mass_offset=48,insulation_offset=96;
#endif
        std::uint16_t* elements{};float* masses{};float* temperatures{};float* next_temperatures{};std::uint8_t* insulation{};
        if(!cells || !updated) {ma::oni::fault_domain_solid_heat("Native solid heat has incomplete cell owners");return;}
        std::memcpy(&elements,static_cast<std::byte*>(cells)+element_offset,sizeof(elements));
        std::memcpy(&masses,static_cast<std::byte*>(cells)+mass_offset,sizeof(masses));
        std::memcpy(&temperatures,static_cast<std::byte*>(cells)+temperature_offset,sizeof(temperatures));
        std::memcpy(&insulation,static_cast<std::byte*>(cells)+insulation_offset,sizeof(insulation));
        std::memcpy(&next_temperatures,static_cast<std::byte*>(updated)+temperature_offset,sizeof(next_temperatures));
        if(!elements || !masses || !temperatures || !next_temperatures || !insulation)
        {ma::oni::fault_domain_solid_heat("Native solid heat has an incomplete CellSOA layout");return;}
        // Original UpdateTemperature accumulates into updatedCells while
        // reading the old cells. Couple to that current accumulated endpoint;
        // retain every earlier native contribution and the original old grid.
        const auto before=next_temperatures[solid];
        const auto result=ma::oni::exchange_domain_solid_heat(fluid,solid,elements[solid],masses[solid],before,seconds,
            insulation[solid],insulation[fluid],temperatures[solid]);
        if(result.handled && std::bit_cast<std::uint32_t>(result.temperature)!=std::bit_cast<std::uint32_t>(before))
            next_temperatures[solid]=result.temperature;
    }
}
bool on_transition(void* data,void* events,int cell,const void* definition) {
    Call call(MA_ONI_STATE_TRANSITION);
    if (ma::oni::colony::active()) {
        // Freezing an owned cell runs natively and is captured by the shadow.
        ma::oni::colony::NativeCells cells{};
        if (ma::oni::colony::owns(cell) && (!native_cells(data,cells) ||
                !ma::oni::colony::allow_native_transition(cell,cells.element[1][cell],cells.temperature[1][cell]))) {
            (void)ma::oni::colony::suppress(MA_ONI_STATE_TRANSITION,cell); return false;
        }
        if (ma::oni::colony::beside_owned(cell,true) && native_cells(data,cells))
            ma::oni::colony::observe_native_solid(cells,cell);
        return original<bool (*)(void*,void*,int,const void*)>(MA_ONI_STATE_TRANSITION)(data,events,cell,definition);
    }
    if (owned_cell(MA_ONI_STATE_TRANSITION,cell)) return false;
    return original<bool (*)(void*,void*,int,const void*)>(MA_ONI_STATE_TRANSITION)(data,events,cell,definition);
}
void on_load_transition(void* data,int cell) {
    Call call(MA_ONI_LOAD_TRANSITION);
    if (!owned_cell(MA_ONI_LOAD_TRANSITION,cell)) original<void (*)(void*,int)>(MA_ONI_LOAD_TRANSITION)(data,cell);
}
bool on_sublimation(void* data,void* events,int cell,const void* definition) {
    Call call(MA_ONI_SUBLIMATION);
    if (owned_cell(MA_ONI_SUBLIMATION,cell)) return false;
    return original<bool (*)(void*,void*,int,const void*)>(MA_ONI_SUBLIMATION)(data,events,cell,definition);
}
void on_post_process(void* data,void* events,int cell) {
    Call call(MA_ONI_POST_PROCESS);
    if (owned_cell(MA_ONI_POST_PROCESS,cell)) return;
    // A newly melted/opening cell can still be unowned this frame. Native
    // density and lateral swaps would move a scalar gas projection across
    // this boundary, losing its real mixture. Keep native solid work, but
    // fence transient fluid postprocessing beside the owner on both builds.
    if (ma::oni::colony::beside_owned(cell,true)) {
        ma::oni::colony::NativeCells cells{};
        if (native_cells(data,cells) && cell >= 0 &&
            static_cast<std::uint64_t>(cell) < static_cast<std::uint64_t>(cells.pitch)*cells.rows) {
            if (ma::oni::colony::is_fluid_element(cells.element[1][cell])) {
                if (ma::oni::colony::beside_owned(cell)) return;
            } else ma::oni::colony::observe_native_solid(cells,cell);
        }
    }
    original<void (*)(void*,void*,int)>(MA_ONI_POST_PROCESS)(data,events,cell);
}
bool on_gas(void* data,void* events,int cell) {
    Call call(MA_ONI_GAS);
    if (owned_cell(MA_ONI_GAS,cell)) return false;
    return original<bool (*)(void*,void*,int)>(MA_ONI_GAS)(data,events,cell);
}
bool on_gas_pair(void* data,void* events,int a,int b) {
    Call call(MA_ONI_GAS_PAIR);
    if (owned_cell(MA_ONI_GAS_PAIR,a) || owned_cell(MA_ONI_GAS_PAIR,b)) return false;
    return original<bool (*)(void*,void*,int,int)>(MA_ONI_GAS_PAIR)(data,events,a,b);
}
template<std::uint32_t Hook>
float on_displacement(std::uint16_t element,int a,int b,int c,void* data) {
    Call call(Hook);
    if (owned_cell(Hook,a) || owned_cell(Hook,b) ||
        owned_cell(Hook,c)) return 0;
    return original<float (*)(std::uint16_t,int,int,int,void*)>(Hook)(element,a,b,c,data);
}
FORWARD(on_step, MA_ONI_STEP, void, (void* self,void* data,void* game,const void* previous), (self,data,game,previous))
FORWARD(on_begin_frame, MA_ONI_BEGIN_FRAME, int, (void* self), (self))
void on_end_frame(void* self) {
    Call call(MA_ONI_END_FRAME);
    original<void (*)(void*)>(MA_ONI_END_FRAME)(self);
    native_frame_epochs.fetch_add(1,std::memory_order_release);
    if (pair_pending.load(std::memory_order_acquire)) {
        std::unique_lock<std::mutex> frame_lock;
        if (!worker_frame_fence.owns_lock()) frame_lock=std::unique_lock<std::mutex>(ma::oni::domain_frame_mutex());
        fulfill_paired_capture();
    }
    if (worker_frame_fence.owns_lock()) worker_frame_fence.unlock();
}
FORWARD(on_consumer, MA_ONI_CONSUMER, void, (void* self,float dt,void* data,const void* region), (self,dt,data,region))
FORWARD(on_emitter, MA_ONI_EMITTER, void, (void* self,float dt,void* data,const void* region), (self,dt,data,region))
FORWARD(on_emitter_dispatch, MA_ONI_EMITTER_DISPATCH, void, (void* self,float dt,void* data,const void* region), (self,dt,data,region))
FORWARD(on_building_heat, MA_ONI_BUILDING_HEAT, void, (void* self,float dt,void* data,const void* region), (self,dt,data,region))
#undef FORWARD

ma::oni::DomainChunkHeat prepare_chunk_contact(void* accessor,void* chunk,float dt,float scale) {
    ma::oni::DomainChunkHeat result{};
    std::int32_t cell{};float temperature{},capacity{},conductivity{},factor{};
    // Colony cells: items exchange natively with the shadow (booked at capture).
    if (!ma::oni::domain_active()) {
        static const bool heat_log=std::getenv("MA_COLONY_HEAT_LOG")!=nullptr;
        if (heat_log && accessor && chunk) {
            std::memcpy(&cell,static_cast<const std::uint8_t*>(accessor)+8,4);
            if (ma::oni::colony::owns(cell)) {
                const auto* c=static_cast<const std::uint8_t*>(chunk);
                std::memcpy(&temperature,c+4,4);std::memcpy(&capacity,c+8,4);std::memcpy(&conductivity,c+12,4);std::memcpy(&factor,c+16,4);
                std::fprintf(stderr,"[Mixed Atmospheres] native chunk at native cell %d: %.4f K, capacity %.6g, conductivity %.6g, factor %.6g\n",
                    cell,static_cast<double>(temperature),static_cast<double>(capacity),static_cast<double>(conductivity),static_cast<double>(factor));
            }
        }
        return result;
    }
    // The pinned Windows PDB and Linux specialized call independently verify
    // this 16-byte accessor and 44-byte record. Copy fields without aliasing
    // the native compiler's C++ object type or retaining its addresses.
    const auto* a=static_cast<const std::uint8_t*>(accessor);const auto* c=static_cast<const std::uint8_t*>(chunk);
    std::memcpy(&cell,a+8,4);std::memcpy(&temperature,c+4,4);std::memcpy(&capacity,c+8,4);
    std::memcpy(&conductivity,c+12,4);std::memcpy(&factor,c+16,4);
    result=ma::oni::exchange_domain_chunk_heat(cell,temperature,capacity,conductivity,factor,scale,dt);
    if (result.handled) std::memcpy(static_cast<std::uint8_t*>(chunk)+4,&result.temperature,4);
    return result;
}
#if defined(_WIN32)
float on_chunk_heat(float dt,const void* data,const void* handle,void* accessor,float scale,void* chunk) {
    Call call(MA_ONI_CHUNK_HEAT);const auto result=prepare_chunk_contact(accessor,chunk,dt,scale);
    if (result.handled) return result.delta_kj;
    using Function=float (*)(float,const void*,const void*,void*,float,void*);
    return original<Function>(MA_ONI_CHUNK_HEAT)(dt,data,handle,accessor,scale,chunk);
}
#else
float on_chunk_heat(void* accessor,void* chunk,float dt,float scale) {
    Call call(MA_ONI_CHUNK_HEAT);const auto result=prepare_chunk_contact(accessor,chunk,dt,scale);
    if (result.handled) return result.delta_kj;
    return original<float (*)(void*,void*,float,float)>(MA_ONI_CHUNK_HEAT)(accessor,chunk,dt,scale);
}
#endif

void on_copy_to_game(void* self, void* data, void* game, const void* previous, int frames) {
    PublicationCall call;
    std::unique_lock<std::mutex> frame_lock;
    if (!worker_frame_fence.owns_lock()) frame_lock = std::unique_lock<std::mutex>(ma::oni::domain_frame_mutex());
    using Function = void (*)(void*, void*, void*, const void*, int);
    reinterpret_cast<Function>(publication_originals[0])(self, data, game, previous, frames);
    // The native copy has already replaced this GameData. A failed capture
    // must not leave the previous frame cached under the same identity.
    const auto forget = [game]() noexcept {
        try { ma::oni::published_frames::record(game, std::nullopt); } catch (...) {}
    };
    try {
        ma::oni::published_frames::record(game,
            ma::oni::capture_domain_publication(native_frame_epochs.load(std::memory_order_acquire)));
    } catch (const std::exception& error) { forget(); ma::oni::fault_domain_solid_heat(error.what()); }
      catch (...) { forget(); ma::oni::fault_domain_solid_heat("Cannot capture completed native publication"); }
}
void* on_prepare_game_update(const void* game) {
    PublicationCall call; ma::oni::published_frames::unbind();
    const auto result = reinterpret_cast<void* (*)(const void*)>(publication_originals[1])(game);
    try { ma::oni::published_frames::bind(game, result); }
    catch (const std::exception& error) { ma::oni::fault_domain_solid_heat(error.what()); }
    catch (...) { ma::oni::fault_domain_solid_heat("Cannot bind completed native publication"); }
    return result;
}
const std::array<void*,2> publication_replacements{
    reinterpret_cast<void*>(&on_copy_to_game), reinterpret_cast<void*>(&on_prepare_game_update)};

const std::array<void*,MA_ONI_HOOK_COUNT> replacements{
    reinterpret_cast<void*>(&on_initialize), reinterpret_cast<void*>(&on_shutdown),
    reinterpret_cast<void*>(&on_message), reinterpret_cast<void*>(&on_messages),
    reinterpret_cast<void*>(&on_begin_save), reinterpret_cast<void*>(&on_end_save),
    reinterpret_cast<void*>(&on_main), reinterpret_cast<void*>(&on_process_frame),
    reinterpret_cast<void*>(&on_next_frame), reinterpret_cast<void*>(&on_update_data),
    reinterpret_cast<void*>(&on_pressure), reinterpret_cast<void*>(&on_liquid),
    reinterpret_cast<void*>(&on_temperature), reinterpret_cast<void*>(&on_transition),
    reinterpret_cast<void*>(&on_load_transition), reinterpret_cast<void*>(&on_sublimation),
    reinterpret_cast<void*>(&on_post_process), reinterpret_cast<void*>(&on_step),
    reinterpret_cast<void*>(&on_begin_frame), reinterpret_cast<void*>(&on_end_frame),
    reinterpret_cast<void*>(&on_consumer), reinterpret_cast<void*>(&on_emitter),
    reinterpret_cast<void*>(&on_building_heat), reinterpret_cast<void*>(&on_gas), reinterpret_cast<void*>(&on_gas_pair),
    reinterpret_cast<void*>(&on_displacement<MA_ONI_GAS_DISPLACEMENT>),
    reinterpret_cast<void*>(&on_displacement<MA_ONI_LIQUID_DISPLACEMENT>), reinterpret_cast<void*>(&on_emitter_dispatch),
    reinterpret_cast<void*>(&on_chunk_heat)};

void output_valid(const MAOniObserverStatus* output) {
    if (!output || output->struct_size != sizeof(MAOniObserverStatus) || output->abi_version != MA_ONI_ABI_VERSION)
        throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid observer status size/version");
}
std::filesystem::path verify_files(const MAOniObserverConfig* config) {
    if (!config || config->struct_size != sizeof(MAOniObserverConfig) || config->abi_version != MA_ONI_ABI_VERSION
        || config->flags != MA_ONI_OBSERVER_ONLY || config->reserved || !config->game_directory_utf8
        || !*config->game_directory_utf8)
        throw Failure(MA_ONI_INVALID_ARGUMENT,"Explicit observer configuration with ABI version 1 is required");
    const auto root = std::filesystem::canonical(std::filesystem::path(
        reinterpret_cast<const char8_t*>(config->game_directory_utf8)));
    if (ma::oni::hash_file(root / native_relative_path) != expected_native_hash)
        throw Failure(MA_ONI_INCOMPATIBLE,"Native image differs from the pinned platform build");
    if (ma::oni::hash_file(root / "OxygenNotIncluded_Data/Managed/Assembly-CSharp.dll") != expected_assembly_hash)
        throw Failure(MA_ONI_INCOMPATIBLE,"Managed image differs from the pinned platform build");
    return root;
}

std::uintptr_t load_original(const std::filesystem::path& path) {
#if defined(_WIN32)
    auto module = GetModuleHandleW(L"SimDLL.dll");
    if (module) {
        std::array<wchar_t,32768> name{};
        const auto count = GetModuleFileNameW(module,name.data(),static_cast<DWORD>(name.size()));
        if (!count || count >= name.size() || !std::filesystem::equivalent(path,std::filesystem::path(name.data())))
            throw Failure(MA_ONI_INCOMPATIBLE,"A different SimDLL is already loaded");
    }
    if (!game_module) game_module = LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!game_module) throw Failure(MA_ONI_INCOMPATIBLE,"Cannot load inspected SimDLL");
    const auto base = reinterpret_cast<std::uintptr_t>(game_module);
    const auto init = reinterpret_cast<std::uintptr_t>(GetProcAddress(static_cast<HMODULE>(game_module),"SIM_Initialize"));
#else
    struct Search { const char* name{}; std::uint32_t count{}; } search;
    dl_iterate_phdr([](dl_phdr_info* info,std::size_t,void* state) {
        auto& result = *static_cast<Search*>(state);
        const char* name = std::strrchr(info->dlpi_name,'/');
        name = name ? name+1 : info->dlpi_name;
        if (std::strcmp(name,"libSimDLL.so") == 0) { result.name=info->dlpi_name; ++result.count; }
        return 0;
    },&search);
    if (search.count > 1 || (search.count == 1 && !std::filesystem::equivalent(path,search.name)))
        throw Failure(MA_ONI_INCOMPATIBLE,"A different or duplicate SimDLL is already loaded");
    if (!game_module) game_module = dlopen(path.c_str(),RTLD_NOW | RTLD_LOCAL);
    if (!game_module) throw Failure(MA_ONI_INCOMPATIBLE,"Cannot load inspected libSimDLL");
    void* initialize = dlsym(game_module,"SIM_Initialize");
    Dl_info info{};
    if (!initialize || !dladdr(initialize,&info) || !std::filesystem::equivalent(path,info.dli_fname))
        throw Failure(MA_ONI_INCOMPATIBLE,"Loaded SIM_Initialize belongs to a different module");
    const auto base = reinterpret_cast<std::uintptr_t>(info.dli_fbase);
    const auto init = reinterpret_cast<std::uintptr_t>(initialize);
#endif
    if (init != base + profile_hooks[0].rva)
        throw Failure(MA_ONI_INCOMPATIBLE,"Loaded native export differs from its profile location");
    return base;
}
void require_quiescent() {
    if (active.load(std::memory_order_acquire)) throw Failure(MA_ONI_BUSY,"Native hooks are executing");
    if (!image_base) return;
    std::uintptr_t sim{}, data{};
    std::memcpy(&sim,reinterpret_cast<const void*>(image_base+sim_rva),sizeof(sim));
    std::memcpy(&data,reinterpret_cast<const void*>(image_base+data_rva),sizeof(data));
    if (sim || data) throw Failure(MA_ONI_BUSY,"Native simulator is initialized; stop it before changing hooks");
}
bool fingerprints_match() {
    for (const auto& hook : profile_hooks)
        if (ma::oni::hash_memory(reinterpret_cast<const void*>(image_base+hook.rva),32) != hook.hash) return false;
    for (const auto& hook : profile_publication_bindings)
        if (ma::oni::hash_memory(reinterpret_cast<const void*>(image_base+hook.rva),32) != hook.hash) return false;
    return true;
}

#if defined(__linux__)
bool trampoline_page_is_rx() {
    auto* maps=std::fopen("/proc/self/maps","r");
    if (!maps) return false;
    const auto first=(image_base+trampoline_storage.rva)&~(trampoline_storage.page_bytes-1);
    std::array<char,2048> line{};
    bool verified{};
    while (std::fgets(line.data(),static_cast<int>(line.size()),maps)) {
        std::size_t start{},end{}; std::array<char,5> permissions{};
        if (std::sscanf(line.data(),"%zx-%zx %4s",&start,&end,permissions.data())==3 &&
            start<=first && end>=first+trampoline_storage.page_bytes) {
            verified=std::strcmp(permissions.data(),"r-xp")==0;
            break;
        }
    }
    std::fclose(maps);
    return verified;
}
bool trampoline_padding_matches() {
    return ma::oni::hash_memory(reinterpret_cast<const void*>(image_base+trampoline_storage.rva),
                                trampoline_storage.bytes)==expected_trampoline_hash;
}
void register_trampoline_storage() {
    struct Search { std::uintptr_t base; bool verified{}; } search{image_base};
    dl_iterate_phdr([](dl_phdr_info* info,std::size_t,void* state) {
        auto& result=*static_cast<Search*>(state);
        if (info->dlpi_addr!=result.base) return 0;
        result.verified=ma::oni::verified_trampoline_segment(
            {info->dlpi_phdr,info->dlpi_phnum},trampoline_storage);
        return 1;
    },&search);
    if (sysconf(_SC_PAGESIZE)!=static_cast<long>(trampoline_storage.page_bytes) || !search.verified ||
        !trampoline_page_is_rx() || !trampoline_padding_matches())
        throw Failure(MA_ONI_INCOMPATIBLE,"Loaded native RX tail padding differs from its verified trampoline profile");
    if (ma_funchook_register_trampoline_buffer(reinterpret_cast<void*>(image_base+trampoline_storage.rva),
                                             trampoline_storage.bytes)!=0)
        throw Failure(MA_ONI_HOOK_ERROR,"Cannot reserve verified native trampoline storage");
    if (ma_funchook_trampoline_capacity()<std::size(profile_hooks)+profile_publication_bindings.size()) {
        (void)ma_funchook_register_trampoline_buffer(nullptr,0);
        throw Failure(MA_ONI_HOOK_ERROR,"Verified native trampoline storage cannot hold every required hook");
    }
}
bool release_trampoline_storage() {
    return trampoline_padding_matches() && trampoline_page_is_rx() &&
           ma_funchook_register_trampoline_buffer(nullptr,0)==0;
}
#else
void register_trampoline_storage() {}
bool release_trampoline_storage() { return true; }
#endif

bool destroy_detours() {
    if (detours) {
        if (funchook_destroy(detours)!=0) return false;
        detours=nullptr;
    }
    return release_trampoline_storage();
}
MAOniObserverStatus capture() noexcept {
    MAOniObserverStatus result{};
    result.struct_size=sizeof(result); result.abi_version=MA_ONI_ABI_VERSION; result.oni_build=744825;
    result.attached=installed ? 1U : 0U;
    result.recovery_required=recovery_required ? 1U : 0U;
    result.installed_hook_mask=installed_mask;
    result.active_calls=active.load(std::memory_order_acquire);
    result.positive_timestep_returns=positive_steps.load(std::memory_order_relaxed);
    result.trace_events_reserved=trace_reserved.load(std::memory_order_relaxed);
    if (result.trace_events_reserved > traces.size()) result.trace_events_dropped=result.trace_events_reserved-traces.size();
    for (std::size_t i=0; i<counters.size(); ++i)
        result.hooks[i]={counters[i].entries.load(std::memory_order_relaxed),counters[i].exits.load(std::memory_order_relaxed)};
    return result;
}
void reset_observations() noexcept {
    for (auto& counter : counters) { counter.entries=0; counter.exits=0; }
    positive_steps=0; trace_reserved=0; native_frame_epochs=0;
    for (auto& ready : trace_ready) ready=false;
}
void fulfill_paired_capture() {
    std::lock_guard pair_lock(pair_mutex);
    if (!captured_pair || captured_pair->status.state!=1) return;
    auto& request=*captured_pair;
    try {
        const auto current=ma::oni::domain_status_under_fence();
        if (current.state!=1 || current.session!=request.status.session || current.completed_frames<request.status.completed_frame)
            throw Failure(MA_ONI_BUSY,"Paired source session ended before its worker barrier");
        MAOniDomainConfig configuration{}; MAOniDomainStatus source{};
        auto physical=request.include_actors ? ma::oni::snapshot_domain_under_fence(current.session,current.completed_frames,configuration,source) :
            ma::oni::checkpoint_domain_under_fence(current.session,current.completed_frames,configuration,source);
        std::lock_guard save_lock(native_save_mutex);
        if (native_save_open) throw Failure(MA_ONI_BUSY,"A native save buffer is still borrowed at the worker barrier");
        std::uintptr_t data{}; std::memcpy(&data,reinterpret_cast<const void*>(image_base+data_rva),sizeof(data));
        if (!data) throw Failure(MA_ONI_BUSY,"Paired capture has no live native world");
        std::array<std::int32_t,2> dimensions{}; std::memcpy(dimensions.data(),reinterpret_cast<const void*>(data),sizeof(dimensions));
        constexpr std::uint64_t maximum_native_bytes=639ULL*1024*1024;
        if (dimensions[0]<=2 || dimensions[1]<=2 || static_cast<std::uint32_t>(dimensions[0])!=configuration.native_grid_width+2 ||
            static_cast<std::uint64_t>(dimensions[0])*static_cast<std::uint32_t>(dimensions[1])>(maximum_native_bytes-29)/36 ||
            request.x>INT32_MAX-dimensions[0] || request.y>INT32_MAX-dimensions[1])
            throw Failure(MA_ONI_INCOMPATIBLE,"Paired native dimensions differ or exceed the envelope format");
        const auto expected_bytes=29+36*static_cast<std::uint64_t>(dimensions[0])*static_cast<std::uint32_t>(dimensions[1]);
        std::vector<std::uint8_t> saved(static_cast<std::size_t>(expected_bytes));
        struct NativeSaveCleanup {
            bool started{};
            ~NativeSaveCleanup() { if (started) on_end_save(); paired_save_permit=false; }
        } cleanup;
        paired_save_permit=true; cleanup.started=true;
        int bytes=0; const auto* native=on_begin_save(&bytes,request.x,request.y);
        if (!native || bytes<29 || static_cast<std::uint64_t>(bytes)!=expected_bytes)
            throw Failure(MA_ONI_INCOMPATIBLE,"Native saving returned an unexpected paired image");
        const std::array<std::uint8_t,8> magic{'S','I','M','S','A','V','E',0}; std::array<std::int32_t,5> header{};
        std::memcpy(header.data(),native+8,sizeof(header));
        if (!std::equal(magic.begin(),magic.end(),native) || header[0]!=15 || header[1]!=dimensions[0] || header[2]!=dimensions[1] || header[3]!=request.x || header[4]!=request.y)
            throw Failure(MA_ONI_INCOMPATIBLE,"Native paired image header differs from its source world");
        std::copy_n(native,bytes,saved.data());
        on_end_save(); cleanup.started=false;
        request.native=std::move(saved); request.physical=std::move(physical);
        auto& result=request.status; result.state=2; result.configuration=configuration;
        result.native_bytes=static_cast<std::uint32_t>(request.native.size()); result.physical_bytes=static_cast<std::uint32_t>(request.physical.size());
        result.completed_frame=source.completed_frames; result.native_epoch=native_frame_epochs.load(std::memory_order_acquire); result.capture_thread=native_thread();
        result.physical_time_seconds=source.time_seconds;
    } catch (const Failure& error) {
        request.status.state=3; request.error_code=error.result; std::snprintf(request.error.data(),request.error.size(),"%s",error.what());
    } catch (const std::logic_error& error) {
        request.status.state=3; request.error_code=MA_ONI_BUSY; std::snprintf(request.error.data(),request.error.size(),"%s",error.what());
    } catch (const std::exception& error) {
        request.status.state=3; request.error_code=MA_ONI_INTERNAL_ERROR; std::snprintf(request.error.data(),request.error.size(),"%s",error.what());
    } catch (...) {
        request.status.state=3; request.error_code=MA_ONI_INTERNAL_ERROR; std::snprintf(request.error.data(),request.error.size(),"Unknown paired checkpoint capture failure");
    }
    pair_pending.store(false,std::memory_order_release);
}
template<class Action> MAOniResult boundary(Action action) noexcept {
    try { std::lock_guard lock(lifecycle); action(); last_error[0]='\0'; return MA_ONI_OK; }
    catch (const Failure& error) { std::snprintf(last_error.data(),last_error.size(),"%s",error.what()); return error.result; }
    catch (const std::exception& error) { std::snprintf(last_error.data(),last_error.size(),"%s",error.what()); return MA_ONI_INTERNAL_ERROR; }
    catch (...) { std::snprintf(last_error.data(),last_error.size(),"Unknown observer error"); return MA_ONI_INTERNAL_ERROR; }
}
// Keep throwing implementation helpers under C++ linkage. MSVC /EHc treats
// C-linkage functions as nonthrowing; exported entry points use boundary().
void start_domain_checked(const MAOniDomainConfig* config,MAOniDomainStatus* output,
    std::span<const std::uint8_t> image,double saved_time,std::span<const MAOniDomainInitialCell> initial_cells={},
    const MAOniDomainPhysics* physics=nullptr,std::span<const std::uint8_t> solid_mask={},
    ma::oni::DomainSolidHeatStartup* solid_heat=nullptr) {
    std::lock_guard save_lock(native_save_mutex);
    if (native_save_open) throw Failure(MA_ONI_BUSY,"Finish the outstanding native save before activating fluid ownership");
    if (!config || !output || output->struct_size != sizeof(*output) || output->abi_version != MA_ONI_DOMAIN_ABI_VERSION)
        throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid native domain configuration or output ABI");
    if (!installed || recovery_required || !image_base)
        throw Failure(MA_ONI_BUSY,"Native domain requires inspected attached hooks");
    if (ma::oni::domain_active()) throw Failure(MA_ONI_BUSY,"Native domain is already active");
    if (ma::oni::colony::active()) throw Failure(MA_ONI_BUSY,"A colony owner is active");
    std::uintptr_t sim{}, data{};
    std::memcpy(&sim,reinterpret_cast<const void*>(image_base+sim_rva),sizeof(sim));
    std::memcpy(&data,reinterpret_cast<const void*>(image_base+data_rva),sizeof(data));
    if (!sim || !data) throw Failure(MA_ONI_BUSY,"Initialize the actual native simulator before a sealed domain");
    // Both inspected SimData layouts start with padded width and height
    // (Windows PDB field offsets 0/4; Linux disassembly/runtime controls).
    // Both the public snapshot and native phase indices are tested; never
    // infer the native pitch from a managed public-cell index.
    std::array<std::int32_t,2> dimensions{};
    std::memcpy(dimensions.data(),reinterpret_cast<const void*>(data),sizeof(dimensions));
    if (config->native_grid_width > static_cast<std::uint32_t>(INT32_MAX-2) ||
        dimensions[0] != static_cast<std::int32_t>(config->native_grid_width+2) || dimensions[1] <= 2 ||
        static_cast<std::uint64_t>(config->offset_y)+config->height+2 >= static_cast<std::uint32_t>(dimensions[1]))
        throw Failure(MA_ONI_INCOMPATIBLE,"Native padded dimensions disagree with the requested public domain grid");
    try {
        if (physics) ma::oni::start_configured_domain(*config,*physics,solid_mask,native_thread(),image,saved_time,initial_cells);
        else ma::oni::start_domain(*config,native_thread(),image,saved_time,initial_cells,solid_heat);
    }
    catch (const std::invalid_argument& error) { throw Failure(MA_ONI_INVALID_ARGUMENT,error.what()); }
    if(solid_heat) *output=solid_heat->status;
    else ma::oni::copy_domain(*output,nullptr,0);
}
// Colony owner admission: attached hooks, a live simulator whose padded grid is
// exactly the configured public grid, and no development domain.
void colony_admitted(const MAOniColonyConfig* config,MAOniColonyStatus* output) {
    if (!config || !output || output->struct_size!=sizeof(*output) || output->abi_version!=MA_ONI_COLONY_ABI_VERSION)
        throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid colony configuration or status ABI");
    std::lock_guard save_lock(native_save_mutex);
    if (native_save_open) throw Failure(MA_ONI_BUSY,"Finish the outstanding native save before activating the colony owner");
    if (!installed || recovery_required || !image_base) throw Failure(MA_ONI_BUSY,"The colony owner requires inspected attached hooks");
    if (ma::oni::domain_active() || ma::oni::colony::active()) throw Failure(MA_ONI_BUSY,"A native fluid owner is already active");
    std::uintptr_t sim{}, data{};
    std::memcpy(&sim,reinterpret_cast<const void*>(image_base+sim_rva),sizeof(sim));
    std::memcpy(&data,reinterpret_cast<const void*>(image_base+data_rva),sizeof(data));
    if (!sim || !data) throw Failure(MA_ONI_BUSY,"Initialize the native simulator before the colony owner");
    std::array<std::int32_t,2> dimensions{};
    std::memcpy(dimensions.data(),reinterpret_cast<const void*>(data),sizeof(dimensions));
    if (config->grid_width>65535 || config->grid_height>65535 || dimensions[0]!=static_cast<std::int32_t>(config->grid_width+2) ||
        dimensions[1]!=static_cast<std::int32_t>(config->grid_height+2))
        throw Failure(MA_ONI_INCOMPATIBLE,"Native padded dimensions disagree with the colony grid");
}
void configured_inputs_valid(const MAOniDomainConfig* config,const MAOniDomainPhysics* physics,const std::uint8_t* mask,std::uint32_t count) {
    if (!config || !physics || !mask || !count || count>MA_ONI_DOMAIN_CONFIGURED_MAXIMUM_CELLS ||
        static_cast<std::uint64_t>(config->width)*config->height!=count)
        throw Failure(MA_ONI_INVALID_ARGUMENT,"Configured native domain requires its geometry, physics and complete solid mask");
}
}

extern "C" {
std::uint32_t ma_oni_colony_abi_version() { return MA_ONI_COLONY_ABI_VERSION; }
std::uint32_t ma_oni_colony_game_kernel_version() { return 1; }
MAOniResult ma_oni_colony_start(const MAOniColonyConfig* config,const MAOniColonyMaterial* materials,std::uint32_t material_count,
    const MAOniColonyCell* cells,std::uint32_t cell_count,MAOniColonyStatus* output) {
    return boundary([&] {
        colony_admitted(config,output);
        if (!materials || !material_count || !cells || !cell_count) throw Failure(MA_ONI_INVALID_ARGUMENT,"The colony owner needs its catalogue and inventory");
        try { ma::oni::colony::start(*config,std::span(materials,material_count),std::span(cells,cell_count),native_thread()); }
        catch (const std::invalid_argument& error) { throw Failure(MA_ONI_INVALID_ARGUMENT,error.what()); }
        *output=ma::oni::colony::status();
    });
}
MAOniResult ma_oni_colony_restore(const MAOniColonyConfig* config,const MAOniColonyMaterial* materials,std::uint32_t material_count,
    const std::uint8_t* image,std::uint32_t image_bytes,MAOniColonyStatus* output) {
    return boundary([&] {
        colony_admitted(config,output);
        if (!materials || !material_count || !image || !image_bytes) throw Failure(MA_ONI_INVALID_ARGUMENT,"The colony restore needs its catalogue and image");
        try { ma::oni::colony::restore(*config,std::span(materials,material_count),std::span(image,image_bytes),native_thread()); }
        catch (const std::invalid_argument& error) { throw Failure(MA_ONI_INVALID_ARGUMENT,error.what()); }
        *output=ma::oni::colony::status();
    });
}
MAOniResult ma_oni_colony_status(MAOniColonyStatus* output) {
    return boundary([&] {
        if (!output || output->struct_size!=sizeof(*output) || output->abi_version!=MA_ONI_COLONY_ABI_VERSION) throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid colony status ABI");
        *output=ma::oni::colony::status();
    });
}
MAOniResult ma_oni_colony_copy(MAOniColonyStatus* output,MAOniColonyCellView* views,std::uint32_t capacity) {
    return boundary([&] {
        if (!output || output->struct_size!=sizeof(*output) || output->abi_version!=MA_ONI_COLONY_ABI_VERSION) throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid colony status ABI");
        try { ma::oni::colony::copy(*output,views,capacity); }
        catch (const std::invalid_argument& error) { throw Failure(MA_ONI_INVALID_ARGUMENT,error.what()); }
    });
}
MAOniResult ma_oni_colony_composition(std::uint32_t cell,std::uint32_t* indices,double* masses,std::uint32_t capacity,std::uint32_t* count) {
    return boundary([&] {
        if (!count || (capacity && (!indices || !masses))) throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid colony composition buffers");
        try { *count=ma::oni::colony::composition(cell,indices,masses,capacity); }
        catch (const std::invalid_argument& error) { throw Failure(MA_ONI_INVALID_ARGUMENT,error.what()); }
    });
}
MAOniResult ma_oni_colony_copy_changed(std::uint64_t session,std::uint64_t frame,MAOniColonyStatus* output,
    MAOniColonyCellView* views,std::uint32_t capacity,std::uint32_t* copied) {
    static const bool timing=[] {const auto* value=std::getenv("MA_COLONY_VIEW_TIMING_LOG");return value && std::strcmp(value,"1")==0;}();
    using Clock=std::chrono::steady_clock;
    const auto begun=timing?Clock::now():Clock::time_point{};
    auto entered=begun;
    const auto result=boundary([&] {
        if(timing)entered=Clock::now();
        if (!output || !copied || output->struct_size!=sizeof(*output) || output->abi_version!=MA_ONI_COLONY_ABI_VERSION || (!views && capacity))
            throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid cached colony view request");
        try { *copied=ma::oni::colony::copy_changed(session,frame,*output,views,capacity)?1u:0u; }
        catch (const std::invalid_argument& error) { throw Failure(MA_ONI_INVALID_ARGUMENT,error.what()); }
    });
    if(timing) {
        const auto finished=Clock::now();
        const auto saved_error=errno;const auto csr=_mm_getcsr();std::fenv_t environment{};
        if(std::fegetenv(&environment)==0) {
            static std::atomic<std::uint64_t> sequence{0};const auto call=sequence.fetch_add(1,std::memory_order_relaxed)+1;
            std::fprintf(stderr,"[Mixed Atmospheres] native view call %llu session %llu frame %llu result %u copied %u wait_ms %.6f work_ms %.6f total_ms %.6f\n",
                static_cast<unsigned long long>(call),static_cast<unsigned long long>(result==MA_ONI_OK?output->session:0),
                static_cast<unsigned long long>(result==MA_ONI_OK?output->completed_frames:0),static_cast<unsigned>(result),
                result==MA_ONI_OK?*copied:0,std::chrono::duration<double,std::milli>(entered-begun).count(),
                std::chrono::duration<double,std::milli>(finished-entered).count(),std::chrono::duration<double,std::milli>(finished-begun).count());
            std::fflush(stderr);(void)std::fesetenv(&environment);_mm_setcsr(csr);
        }
        errno=saved_error;
    }
    return result;
}
MAOniResult ma_oni_colony_queue_transfers(const MAOniColonyTransfer* edits,std::uint32_t count) {
    return boundary([&] {
        if (!edits || !count) throw Failure(MA_ONI_INVALID_ARGUMENT,"An empty colony transfer batch");
        try { ma::oni::colony::queue_transfers(std::span(edits,count)); }
        catch (const std::invalid_argument& error) { throw Failure(MA_ONI_INVALID_ARGUMENT,error.what()); }
    });
}
MAOniResult ma_oni_colony_queue_heat(const MAOniColonyHeat* edits,std::uint32_t count) {
    return boundary([&] {
        if (!edits || !count) throw Failure(MA_ONI_INVALID_ARGUMENT,"An empty colony heat batch");
        try { ma::oni::colony::queue_heat(std::span(edits,count)); }
        catch (const std::invalid_argument& error) { throw Failure(MA_ONI_INVALID_ARGUMENT,error.what()); }
    });
}
MAOniResult ma_oni_colony_queue_terrain(const MAOniColonyTerrain* edits,std::uint32_t count) {
    return boundary([&] {
        if (!edits || !count) throw Failure(MA_ONI_INVALID_ARGUMENT,"An empty colony terrain batch");
        try { ma::oni::colony::queue_terrain(std::span(edits,count)); }
        catch (const std::invalid_argument& error) { throw Failure(MA_ONI_INVALID_ARGUMENT,error.what()); }
    });
}
MAOniResult ma_oni_colony_copy_receipts(MAOniColonyReceipt* receipts,std::uint32_t capacity,std::uint32_t* count) {
    return boundary([&] {
        if (!count || (capacity && !receipts)) throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid colony receipt buffer");
        *count=ma::oni::colony::copy_receipts(receipts,capacity);
    });
}
MAOniResult ma_oni_colony_ack_receipts(std::uint64_t through) {
    return boundary([&] {
        try { ma::oni::colony::ack_receipts(through); }
        catch (const std::invalid_argument& error) { throw Failure(MA_ONI_INVALID_ARGUMENT,error.what()); }
    });
}
MAOniResult ma_oni_colony_enable_precise_receipts(std::uint32_t enabled) {
    return boundary([&] {
        if (enabled > 1) throw Failure(MA_ONI_INVALID_ARGUMENT,"Precise receipt mode must be zero or one");
        try { ma::oni::colony::enable_precise_receipts(enabled != 0); }
        catch (const std::invalid_argument& error) { throw Failure(MA_ONI_INVALID_ARGUMENT,error.what()); }
    });
}
std::uint32_t ma_oni_colony_precise_receipt_version() { return 2; }
std::uint32_t ma_oni_colony_actor_batch_version() { return 1; }
std::uint32_t ma_oni_colony_parcel_context_version() { return 1; }
MAOniResult ma_oni_colony_copy_parcel_context(MAOniColonyParcelContext* output) {
    return boundary([&] {
        if (!output || output->struct_size!=sizeof(*output) || output->abi_version!=1 || output->reserved)
            throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid parcel context header");
        try { const auto next=ma::oni::colony::parcel_context(); *output=next; }
        catch (const std::invalid_argument& error) { throw Failure(MA_ONI_INVALID_ARGUMENT,error.what()); }
    });
}
MAOniResult ma_oni_colony_queue_actor_batch(const MAOniColonyTransfer* transfers,std::uint32_t count,
    const MAOniColonyActorJob* jobs,std::uint32_t job_count,const MAOniColonyPassive* passives,std::uint32_t passive_count) {
    return boundary([&] {
        if (!transfers || !count || (job_count && !jobs) || (passive_count && !passives))
            throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid actor batch buffers");
        try { ma::oni::colony::queue_actor_batch(std::span(transfers,count),std::span(jobs,job_count),std::span(passives,passive_count)); }
        catch (const std::invalid_argument& error) { throw Failure(MA_ONI_INVALID_ARGUMENT,error.what()); }
    });
}
MAOniResult ma_oni_colony_copy_precise_receipt(std::uint64_t sequence,std::uint8_t* buffer,std::uint32_t capacity,std::uint32_t* bytes) {
    return boundary([&] {
        if (!bytes || (capacity && !buffer)) throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid precise receipt buffer");
        *bytes=ma::oni::colony::copy_precise_receipt(sequence,buffer,capacity);
        if (buffer && capacity < *bytes) throw Failure(MA_ONI_BUFFER_TOO_SMALL,"Precise receipt buffer is too small");
    });
}
MAOniResult ma_oni_colony_wait() {
    return boundary([&] { ma::oni::colony::wait(); });
}
MAOniResult ma_oni_colony_queue_pipe_job(std::uint64_t token,const MAOniColonyTransfer* transfer,
    const MAOniColonyPassive* passives,std::uint32_t count,std::uint32_t* state,std::uint64_t* last) {
    return boundary([&] {
        if (!transfer || (count && !passives) || !state || !last) throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid pipe job request/output");
        try {
            const auto result=ma::oni::colony::queue_pipe_job(token,*transfer,std::span(passives,count));
            *state=result.first; *last=result.second;
        } catch (const std::invalid_argument& error) { throw Failure(MA_ONI_INVALID_ARGUMENT,error.what()); }
    });
}
std::uint32_t ma_oni_colony_moving_parcel_version() { return 1; }
MAOniResult ma_oni_colony_queue_moving_parcel(std::uint64_t session,std::uint64_t token,const std::uint8_t* request,
    std::uint32_t bytes,std::uint32_t* state,std::uint64_t* last) {
    return boundary([&] {
        if(!request || !bytes || !state || !last)throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid moving parcel input/output");
        try {const auto result=ma::oni::colony::queue_moving_parcel(session,token,std::span(request,bytes));*state=result.first;*last=result.second;}
        catch(const std::invalid_argument& error) {throw Failure(MA_ONI_INVALID_ARGUMENT,error.what());}
    });
}
MAOniResult ma_oni_colony_copy_moving_outcome(std::uint64_t sequence,std::uint8_t* buffer,std::uint32_t capacity,std::uint32_t* bytes) {
    return boundary([&] {
        if(!bytes || (capacity && !buffer))throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid moving outcome buffer");
        *bytes=ma::oni::colony::copy_moving_outcome(sequence,buffer,capacity);
        if(buffer && capacity<*bytes)throw Failure(MA_ONI_BUFFER_TOO_SMALL,"Moving outcome buffer is too small");
    });
}
std::uint32_t ma_oni_conduit_contact_version() {
    return 1;
}
MAOniResult ma_oni_conduit_contact_replay(const MAOniConduitContactInput* contacts,std::uint32_t count,
    MAOniConduitContactResult* output,std::uint32_t capacity) {
    return boundary([&] {
        static_assert(sizeof(MAOniConduitContactInput)==44 && sizeof(MAOniConduitContactResult)==20);
        if(!contacts || !output || !count || count>4096)throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid conduit contact arrays");
        if(capacity<count)throw Failure(MA_ONI_BUFFER_TOO_SMALL,"Conduit contact output is too small");
        for(const auto& value : std::span(contacts,count)) {
            if(value.struct_size!=44 || value.abi_version!=1 || (value.flags&~1u) || value.reserved ||
                !std::isfinite(value.seconds) || value.seconds<0 || value.seconds>10 ||
                !std::isfinite(value.fluid_temperature_k) || value.fluid_temperature_k<=0 ||
                !std::isfinite(value.body_temperature_k) || value.body_temperature_k<=0 ||
                !std::isfinite(value.fluid_heat_capacity_kj_k) || value.fluid_heat_capacity_kj_k<=0 ||
                !std::isfinite(value.body_heat_capacity_kj_k) || value.body_heat_capacity_kj_k<=0 ||
                !std::isfinite(value.fluid_conductivity) || value.fluid_conductivity<0 ||
                !std::isfinite(value.conduit_conductivity) || value.conduit_conductivity<0)
                throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid conduit contact record");
        }
        if(!installed || !game_module)throw Failure(MA_ONI_BUSY,"The inspected native image is not bound");
#if defined(__linux__)
        using Flow=float(*)(void*,bool,float,float,float,float,float);
        using Clamp=float(*)(void*,float,float,float,float,float,float);
        using Temperature=float(*)(void*,float,float,float,float,float);
        const auto flow=reinterpret_cast<Flow>(dlsym(game_module,"_ZN25ConduitTemperatureManager19CalculateEnergyFlowEbfffff"));
        const auto clamp=reinterpret_cast<Clamp>(dlsym(game_module,"_ZN25ConduitTemperatureManager19ClampEnergyTransferEffffff"));
        const auto temperature=reinterpret_cast<Temperature>(dlsym(game_module,"_ZN25ConduitTemperatureManager17ModifyTemperatureEfffff"));
        if(!flow || !clamp || !temperature)throw Failure(MA_ONI_INCOMPATIBLE,"Conduit contact helpers are unavailable");
        // This parameter is fixed by the inspected native Update callsite,
        // independent of material, timestep, execution load and saved profile.
        constexpr float exchange_coefficient=50;
#endif
        struct PreserveMath {
            std::fenv_t environment{};unsigned csr=_mm_getcsr();int error=errno;
            PreserveMath() {if(std::fegetenv(&environment))throw Failure(MA_ONI_INTERNAL_ERROR,"Cannot observe contact math environment");}
            ~PreserveMath() {(void)std::fesetenv(&environment);_mm_setcsr(csr);errno=error;}
        } preserve;
        std::vector<MAOniConduitContactResult> results;results.reserve(count);
        for(const auto& value : std::span(contacts,count)) {
#if defined(__linux__)
            const auto power=flow(nullptr,(value.flags&1)!=0,value.fluid_temperature_k,value.fluid_conductivity,
                value.body_temperature_k,value.conduit_conductivity,exchange_coefficient);
            const auto heat=clamp(nullptr,value.seconds,value.fluid_temperature_k,value.fluid_heat_capacity_kj_k,
                value.body_temperature_k,value.body_heat_capacity_kj_k,power);
            const auto fluid=temperature(nullptr,value.fluid_temperature_k,value.fluid_heat_capacity_kj_k,
                value.body_temperature_k,value.body_heat_capacity_kj_k,-heat);
            const float body_delta=heat/value.body_heat_capacity_kj_k;
            const float body=value.body_temperature_k+body_delta;
#else
            const auto replayed=ma::oni::conduit_contact::replay(value);
            const auto heat=replayed.native_heat_to_body_kj,fluid=replayed.carrier_temperature_k,body=replayed.body_temperature_k;
#endif
            if(!std::isfinite(heat) || !std::isfinite(fluid) || fluid<=0 || !std::isfinite(body) || body<=0)
                throw Failure(MA_ONI_INVALID_ARGUMENT,"Conduit contact replay is outside native thermal range");
            results.push_back({20,1,heat,fluid,body});
        }
        std::copy(results.begin(),results.end(),output);
    });
}
MAOniResult ma_oni_colony_pipe_job_state(std::uint64_t token,std::uint32_t* state,std::uint64_t* last) {
    return boundary([&] {
        if (!state || !last) throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid pipe job state output");
        try {
            const auto result=ma::oni::colony::pipe_job_state(token); *state=result.first; *last=result.second;
        } catch (const std::invalid_argument& error) { throw Failure(MA_ONI_INVALID_ARGUMENT,error.what()); }
    });
}
MAOniResult ma_oni_colony_receipt_job(std::uint64_t sequence,std::uint64_t* token) {
    return boundary([&] {
        if (!token) throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid pipe receipt token output");
        *token=ma::oni::colony::receipt_job(sequence);
    });
}
std::uint32_t ma_oni_colony_passive_snapshot_version() { return 1; }
MAOniResult ma_oni_colony_read_passives(std::uint64_t session,std::uint64_t frame,const std::uint32_t* cells,std::uint32_t cell_count,
    MAOniColonyPassiveStamp* stamp,MAOniColonyPassiveRow* rows,std::uint32_t capacity,std::uint32_t* count) {
    return boundary([&] {
        static_assert(sizeof(MAOniColonyPassiveStamp)==32 && sizeof(MAOniColonyPassiveRow)==24);
        if(!cells || !cell_count || cell_count>4096 || !stamp || stamp->struct_size!=32 || stamp->abi_version!=1 || !count || (capacity && !rows))
            throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid passive observation input/output");
        MAOniColonyPassiveStamp source{};std::vector<MAOniColonyPassiveRow> values;
        try {
            if(!ma::oni::colony::read_passives(session,frame,std::span(cells,cell_count),source,values))
                throw Failure(MA_ONI_BUSY,"The requested passive publication has advanced");
        } catch(const std::invalid_argument& error) {throw Failure(MA_ONI_INVALID_ARGUMENT,error.what());}
        if(rows && capacity<values.size())throw Failure(MA_ONI_BUFFER_TOO_SMALL,"Passive observation output is too small");
        if(rows)std::copy(values.begin(),values.end(),rows);
        *stamp=source;*count=static_cast<std::uint32_t>(values.size());
    });
}
std::uint32_t ma_oni_colony_options_version() { return 1; }
MAOniResult ma_oni_colony_default_options(MAOniColonyOptions* output) {
    return boundary([&] {
        if(!output) throw Failure(MA_ONI_INVALID_ARGUMENT,"Colony options output is required");
        *output=ma::oni::colony::default_options();
    });
}
MAOniResult ma_oni_colony_set_options(const MAOniColonyOptions* options) {
    return boundary([&] {
        try { ma::oni::colony::set_options(options); }
        catch(const std::invalid_argument& error) {throw Failure(MA_ONI_INVALID_ARGUMENT,error.what());}
    });
}
MAOniResult ma_oni_colony_copy_options(MAOniColonyOptions* output) {
    return boundary([&] {
        if(!output) throw Failure(MA_ONI_INVALID_ARGUMENT,"Colony options output is required");
        MAOniColonyOptions value{};
        try { if(!ma::oni::colony::copy_options(value)) throw Failure(MA_ONI_INCOMPATIBLE,"The running colony does not use a game kernel"); }
        catch(const std::invalid_argument& error) {throw Failure(MA_ONI_INVALID_ARGUMENT,error.what());}
        *output=value;
    });
}
std::uint32_t ma_oni_colony_world_biology_version() { return 1; }
MAOniResult ma_oni_colony_set_diseases(const MAOniColonyDisease* diseases,std::uint32_t disease_count,
    const MAOniColonyDiseaseGrowth* growth,std::uint32_t element_count) {
    return boundary([&] {
        static_assert(sizeof(MAOniColonyDisease)==48 && sizeof(MAOniColonyDiseaseGrowth)==32);
        if((disease_count && !diseases) || (disease_count && element_count && !growth) || disease_count>256 || element_count>65536)
            throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid world disease table buffers");
        try {
            ma::oni::colony::set_diseases(std::span(diseases,disease_count),
                std::span(growth,static_cast<std::size_t>(disease_count)*element_count),element_count);
        } catch(const std::invalid_argument& error) {throw Failure(MA_ONI_INVALID_ARGUMENT,error.what());}
    });
}
MAOniResult ma_oni_colony_queue_germs(const MAOniColonyGermEdit* edits,std::uint32_t count) {
    return boundary([&] {
        static_assert(sizeof(MAOniColonyGermEdit)==16);
        if(!edits || !count)throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid germ edit buffer");
        try {ma::oni::colony::queue_germs(std::span(edits,count));}
        catch(const std::invalid_argument& error) {throw Failure(MA_ONI_INVALID_ARGUMENT,error.what());}
    });
}
MAOniResult ma_oni_colony_biology(MAOniColonyBiology* output,MAOniColonyBiologyAccount* accounts,std::uint32_t capacity,std::uint32_t* count) {
    return boundary([&] {
        static_assert(sizeof(MAOniColonyBiology)==312 && sizeof(MAOniColonyBiologyAccount)==64);
        if(!output || output->struct_size!=sizeof(*output) || output->abi_version!=1 || !count || (capacity && !accounts))
            throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid germ account output");
        MAOniColonyBiology summary{};std::vector<MAOniColonyBiologyAccount> values;
        try {ma::oni::colony::germ_accounts(summary,values);}
        catch(const std::invalid_argument& error) {throw Failure(MA_ONI_INVALID_ARGUMENT,error.what());}
        if(accounts && capacity<values.size())throw Failure(MA_ONI_BUFFER_TOO_SMALL,"Germ account output is too small");
        if(accounts)std::copy(values.begin(),values.end(),accounts);
        *output=summary;*count=static_cast<std::uint32_t>(values.size());
    });
}
MAOniResult ma_oni_colony_checkpoint(std::uint8_t* buffer,std::uint32_t capacity,std::uint32_t* bytes) {
    return boundary([&] {
        if (!bytes) throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid colony checkpoint size output");
        std::vector<std::uint8_t> image;
        try { image=ma::oni::colony::checkpoint(); }
        catch (const std::invalid_argument& error) { throw Failure(MA_ONI_BUSY,error.what()); }
        if (image.size()>std::numeric_limits<std::uint32_t>::max()) throw Failure(MA_ONI_INTERNAL_ERROR,"Colony checkpoint exceeds 4 GiB");
        *bytes=static_cast<std::uint32_t>(image.size());
        if (!buffer) return;
        if (capacity<image.size()) throw Failure(MA_ONI_BUFFER_TOO_SMALL,"Colony checkpoint buffer is too small");
        std::memcpy(buffer,image.data(),image.size());
    });
}
MAOniResult ma_oni_colony_stop(MAOniColonyStatus* output) {
    return boundary([&] {
        ma::oni::windows_lifecycle_trace("colony stop API entered");
        if (output && (output->struct_size!=sizeof(*output) || output->abi_version!=MA_ONI_COLONY_ABI_VERSION)) throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid colony status ABI");
        if (output) *output=ma::oni::colony::status();
        ma::oni::windows_lifecycle_trace("colony stop API status copied");
        ma::oni::colony::stop();
    });
}
MAOniResult ma_oni_colony_fault(const char* reason) {
    return boundary([&] {
        try { ma::oni::colony::fault_from_adapter(reason); }
        catch (const std::invalid_argument& error) { throw Failure(MA_ONI_INVALID_ARGUMENT,error.what()); }
    });
}
std::uint32_t ma_oni_colony_active() { return ma::oni::colony::active() ? 1U : 0U; }
MAOniResult ma_oni_colony_species(MAOniColonySpecies* out, std::uint32_t capacity, std::uint32_t* count) {
    return boundary([&] {
        if (!count) throw Failure(MA_ONI_INVALID_ARGUMENT,"Null colony species count");
        try { *count=ma::oni::colony::species(out,capacity); }
        catch (const std::exception& error) { throw Failure(MA_ONI_INVALID_ARGUMENT,error.what()); }
    });
}
MAOniResult ma_oni_colony_readings(std::uint64_t* frame, MAOniColonyReading* out, std::uint32_t capacity, std::uint32_t* count,
    MAOniColonyConstituent* parts, std::uint32_t part_capacity, std::uint32_t* part_count) {
    return boundary([&] {
        if (!frame || !count || !part_count) throw Failure(MA_ONI_INVALID_ARGUMENT,"Null colony reading output");
        std::vector<MAOniColonyReading> readings; std::vector<MAOniColonyConstituent> constituents;
        try { *frame=ma::oni::colony::readings(readings,constituents); }
        catch (const std::exception& error) { throw Failure(MA_ONI_INVALID_ARGUMENT,error.what()); }
        *count=static_cast<std::uint32_t>(readings.size()); *part_count=static_cast<std::uint32_t>(constituents.size());
        if (!out && !parts) return;
        if (!out || !parts || capacity<readings.size() || part_capacity<constituents.size()) throw Failure(MA_ONI_BUFFER_TOO_SMALL,"Colony reading buffers are too small");
        std::copy(readings.begin(),readings.end(),out); std::copy(constituents.begin(),constituents.end(),parts);
    });
}
MAOniResult ma_oni_colony_sample_history(std::uint32_t frames) {
    return boundary([&] {
        try { ma::oni::colony::sample_history(frames); }
        catch (const std::invalid_argument& error) { throw Failure(MA_ONI_INVALID_ARGUMENT,error.what()); }
    });
}
MAOniResult ma_oni_colony_sample_frame(std::uint64_t frame,const std::uint32_t* cells,std::uint32_t count,
    MAOniColonyStatus* status,MAOniColonySampleCell* rows,MAOniColonySamplePart* parts,std::uint32_t capacity,
    std::uint32_t* part_count,double* booked_mass,double* booked_heat) {
    return boundary([&] {
        if (!cells || !count || count>4096 || !status || !part_count || !booked_mass || !booked_heat || (!parts && capacity))
            throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid retained arena sample buffer");
        MAOniColonyStatus captured{}; double mass=0,heat=0;
        std::vector<MAOniColonySampleCell> sampled; std::vector<MAOniColonySamplePart> constituents;
        try { ma::oni::colony::sample_frame(frame,std::span(cells,count),captured,sampled,constituents,&mass,&heat); }
        catch (const std::invalid_argument& error) { throw Failure(MA_ONI_INVALID_ARGUMENT,error.what()); }
        *part_count=static_cast<std::uint32_t>(constituents.size());
        if (!rows && !parts && capacity==0) { *status=captured; *booked_mass=mass; *booked_heat=heat; return; }
        if (!rows || capacity<constituents.size() || (!parts && !constituents.empty())) throw Failure(MA_ONI_BUFFER_TOO_SMALL,"Retained arena sample buffer is too small");
        std::copy(sampled.begin(),sampled.end(),rows); if (parts) std::copy(constituents.begin(),constituents.end(),parts);
        *status=captured; *booked_mass=mass; *booked_heat=heat;
    });
}
MAOniResult ma_oni_colony_sample_energy(std::uint64_t session,std::uint64_t frame,MAOniColonyEnergySample* out) {
    return boundary([&] {
        if (!out || out->struct_size!=sizeof(*out) || out->abi_version!=1)
            throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid retained arena energy record");
        MAOniColonyEnergySample captured{};
        try { captured=ma::oni::colony::sample_energy(session,frame); }
        catch (const std::invalid_argument& error) { throw Failure(MA_ONI_INVALID_ARGUMENT,error.what()); }
        *out=captured;
    });
}
MAOniResult ma_oni_colony_sample_native_frame(std::uint64_t session,std::uint64_t frame,
    const std::uint32_t* cells,std::uint32_t count,MAOniColonyStatus* out,MAOniColonyNativeSampleCell* rows) {
    return boundary([&] {
        if (!cells || !count || count>4096 || !out || !rows)
            throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid native arena sample buffers");
        MAOniColonyStatus source{};std::vector<MAOniColonyNativeSampleCell> sampled;
        try {ma::oni::colony::sample_native_frame(session,frame,std::span(cells,count),source,sampled);}
        catch(const std::invalid_argument& error){throw Failure(MA_ONI_INVALID_ARGUMENT,error.what());}
        std::copy(sampled.begin(),sampled.end(),rows);*out=source;
    });
}
MAOniResult ma_oni_colony_sample_region_energy(std::uint64_t session,std::uint64_t frame,
    const std::uint32_t* cells,std::uint32_t count,MAOniColonyRegionEnergySample* out) {
    return boundary([&] {
        if (!cells || !count || count>4096 || !out || out->struct_size!=sizeof(*out) || out->abi_version!=1)
            throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid region energy record or cells");
        MAOniColonyRegionEnergySample captured{};
        try {captured=ma::oni::colony::sample_region_energy(session,frame,std::span(cells,count));}
        catch(const std::invalid_argument& error){throw Failure(MA_ONI_INVALID_ARGUMENT,error.what());}
        *out=captured;
    });
}
MAOniResult ma_oni_colony_sample(const std::uint32_t* cells,std::uint32_t count,MAOniColonyStatus* status,
    MAOniColonySampleCell* rows,MAOniColonySamplePart* parts,std::uint32_t capacity,std::uint32_t* part_count,double* booked_mass) {
    return boundary([&] {
        if (!cells || !count || count>4096 || !status || !part_count || !booked_mass || (!parts && capacity)) throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid arena sample buffers");
        MAOniColonyStatus source{}; std::vector<MAOniColonySampleCell> sampled; std::vector<MAOniColonySamplePart> compositions;
        double booked=0;
        try { ma::oni::colony::sample(std::span(cells,count),source,sampled,compositions,&booked); }
        catch (const std::invalid_argument& error) { throw Failure(MA_ONI_INVALID_ARGUMENT,error.what()); }
        *part_count=static_cast<std::uint32_t>(compositions.size());
        if (!rows && !parts && !capacity) { *status=source; *booked_mass=booked; return; }
        if (!rows || capacity<compositions.size() || (!parts && !compositions.empty())) throw Failure(MA_ONI_BUFFER_TOO_SMALL,"Arena sample buffer is too small");
        std::copy(sampled.begin(),sampled.end(),rows);
        if (parts) std::copy(compositions.begin(),compositions.end(),parts);
        *status=source; *booked_mass=booked;
    });
}
MAOniResult ma_oni_colony_liquid_density(const MAOniColonyMaterial* materials, std::uint32_t count, std::uint32_t native_index,
    double pressure_pa, double* density) {
    return boundary([&] {
        if (!materials || !density) throw Failure(MA_ONI_INVALID_ARGUMENT,"Null liquid density argument");
        try { *density=ma::oni::colony::liquid_density(std::span(materials,count),native_index,pressure_pa); }
        catch (const std::exception& error) { throw Failure(MA_ONI_INVALID_ARGUMENT,error.what()); }
    });
}
std::uint32_t ma_oni_colony_busy() { return ma::oni::colony::busy() ? 1U : 0U; }
std::uint32_t ma_oni_abi_version() { return MA_ONI_ABI_VERSION; }
const char* ma_oni_last_error() { return last_error.data(); }
MAOniResult ma_oni_probe(const MAOniObserverConfig* config,MAOniObserverStatus* output) {
    return boundary([&] {
        output_valid(output); (void)verify_files(config);
        MAOniObserverStatus result{};
        result.struct_size=sizeof(result); result.abi_version=MA_ONI_ABI_VERSION; result.oni_build=744825;
        *output=result;
    });
}
MAOniResult ma_oni_observer_status(MAOniObserverStatus* output) {
    return boundary([&] { output_valid(output); *output=capture(); });
}
MAOniResult ma_oni_attach_observer(const MAOniObserverConfig* config,MAOniObserverStatus* output) {
    return boundary([&] {
        output_valid(output);
        if (installed || recovery_required || detours) throw Failure(MA_ONI_BUSY,"Observer already attached or native recovery is required");
        const auto root=verify_files(config);
        image_base=load_original(root/native_relative_path);
        require_quiescent();
        if (!fingerprints_match()) throw Failure(MA_ONI_INCOMPATIBLE,"Loaded native entry fingerprint differs; refusing all hooks");
        reset_observations();
        register_trampoline_storage();
        try {
            detours=funchook_create();
            if (!detours) throw Failure(MA_ONI_HOOK_ERROR,"Cannot allocate native detours");
            for (const auto& hook : profile_hooks) {
                originals[hook.index]=reinterpret_cast<void*>(image_base+hook.rva);
                if (funchook_prepare(detours,&originals[hook.index],replacements[hook.index]) != 0) {
                    std::array<char,2048> detail{};
                    std::snprintf(detail.data(),detail.size(),"Hook %u: %s",hook.index,funchook_error_message(detours));
                    throw Failure(MA_ONI_HOOK_ERROR,detail.data());
                }
            }
            for (const auto& hook : profile_publication_bindings) {
                publication_originals[hook.index] = reinterpret_cast<void*>(image_base + hook.rva);
                if (funchook_prepare(detours, &publication_originals[hook.index], publication_replacements[hook.index]) != 0)
                    throw Failure(MA_ONI_HOOK_ERROR, funchook_error_message(detours));
            }
#if defined(__linux__)
            if (ma_funchook_trampoline_used()!=std::size(profile_hooks)+profile_publication_bindings.size())
                throw Failure(MA_ONI_HOOK_ERROR,"A required hook did not use the verified native trampoline buffer");
#endif
            if (funchook_install(detours,0) != 0) throw Failure(MA_ONI_HOOK_ERROR,funchook_error_message(detours));
            installed=true;
            for (const auto& hook : profile_hooks) installed_mask |= std::uint64_t{1} << hook.index;
            *output=capture();
        } catch (...) {
            // Prepare is non-mutating, but install can fail after partial work.
            // Verify restoration, retain live trampolines if recovery fails.
            if (detours) (void)funchook_uninstall(detours,0);
            if (!fingerprints_match() || !destroy_detours()) recovery_required=true;
            throw;
        }
    });
}
MAOniResult ma_oni_detach_observer() {
    return boundary([] {
        if (recovery_required) throw Failure(MA_ONI_HOOK_ERROR,"Native recovery failed; keep observer mapped and restart the process");
        if (!installed) return;
        require_quiescent();
        if (funchook_uninstall(detours,0) != 0 || !fingerprints_match()) {
            recovery_required=true;
            throw Failure(MA_ONI_HOOK_ERROR,"Native hooks could not be restored; keep observer mapped and restart the process");
        }
        installed=false; installed_mask=0;
        if (!destroy_detours()) {
            recovery_required=true;
            throw Failure(MA_ONI_HOOK_ERROR,"Native trampoline storage could not be restored; keep observer mapped and restart the process");
        }
    });
}
MAOniResult ma_oni_copy_trace(MAOniTraceEvent* output,std::uint32_t capacity,std::uint32_t* count) {
    return boundary([&] {
        if (!count) throw Failure(MA_ONI_INVALID_ARGUMENT,"Trace count output is required");
        // A quiescent capture provides a complete ordered prefix; a live capture
        // may only return the ready prefix, never an uncommitted writer slot.
        std::uint32_t ready=0;
        while (ready<MA_ONI_TRACE_CAPACITY && trace_ready[ready].load(std::memory_order_acquire)) ++ready;
        if (capacity < ready) throw Failure(MA_ONI_BUFFER_TOO_SMALL,"Trace buffer too small; outputs unchanged");
        if (ready && !output) throw Failure(MA_ONI_INVALID_ARGUMENT,"Trace buffer is required");
        if (ready) std::copy_n(traces.data(),ready,output);
        *count=ready;
    });
}
std::uint32_t ma_oni_domain_abi_version() { return MA_ONI_DOMAIN_ABI_VERSION; }
std::uint32_t ma_oni_domain_active() { return ma::oni::domain_active() ? 1U : 0U; }
MAOniResult ma_oni_domain_species(std::uint32_t index,MAOniDomainSpecies* output) {
    return boundary([&] {
        if (!output || index >= MA_ONI_DOMAIN_SPECIES) throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid domain species output or index");
        *output=ma::oni::domain_species(index);
    });
}
MAOniResult ma_oni_domain_start(const MAOniDomainConfig* config,MAOniDomainStatus* output) {
    return boundary([&] { start_domain_checked(config,output,{},0); });
}
std::uint32_t ma_oni_domain_initial_inventory_abi_version() { return 1; }
MAOniResult ma_oni_domain_start_from_cells(const MAOniDomainConfig* config,const MAOniDomainInitialCell* cells,
    std::uint32_t count,MAOniDomainStatus* output) {
    return boundary([&] {
        if (!config || !cells || !count || count>256 ||
            static_cast<std::uint64_t>(config->width)*config->height!=count)
            throw Failure(MA_ONI_INVALID_ARGUMENT,"Initial native inventory must cover every domain cell");
        start_domain_checked(config,output,{},0,std::span(cells,count));
    });
}
std::uint32_t ma_oni_domain_configured_abi_version() { return MA_ONI_DOMAIN_CONFIGURED_ABI_VERSION; }
MAOniResult ma_oni_domain_start_configured(const MAOniDomainConfig* config,const MAOniDomainPhysics* physics,
    const std::uint8_t* mask,std::uint32_t mask_count,const MAOniDomainInitialCell* cells,std::uint32_t count,MAOniDomainStatus* output) {
    return boundary([&] {
        configured_inputs_valid(config,physics,mask,mask_count);
        if (!cells || count!=mask_count) throw Failure(MA_ONI_INVALID_ARGUMENT,"Configured initial inventory must cover every domain cell");
        start_domain_checked(config,output,{},0,std::span(cells,count),physics,std::span(mask,mask_count));
    });
}
MAOniResult ma_oni_domain_configured_checkpoint_validate(const MAOniDomainConfig* config,const MAOniDomainPhysics* physics,
    const std::uint8_t* mask,std::uint32_t mask_count,const std::uint8_t* image,std::uint32_t size,double saved_time) {
    return boundary([&] {
        configured_inputs_valid(config,physics,mask,mask_count);
        if (!image || !size) throw Failure(MA_ONI_INVALID_ARGUMENT,"Configured checkpoint validation source is missing");
        try { ma::oni::validate_configured_domain_checkpoint(*config,*physics,std::span(mask,mask_count),std::span(image,size),saved_time); }
        catch (const std::invalid_argument& error) { throw Failure(MA_ONI_INVALID_ARGUMENT,error.what()); }
    });
}
MAOniResult ma_oni_domain_restore_configured(const MAOniDomainConfig* config,const MAOniDomainPhysics* physics,
    const std::uint8_t* mask,std::uint32_t mask_count,const std::uint8_t* image,std::uint32_t size,double saved_time,MAOniDomainStatus* output) {
    return boundary([&] {
        configured_inputs_valid(config,physics,mask,mask_count);
        if (!image || !size) throw Failure(MA_ONI_INVALID_ARGUMENT,"Configured checkpoint restore source is missing");
        start_domain_checked(config,output,std::span(image,size),saved_time,{},physics,std::span(mask,mask_count));
    });
}
MAOniResult ma_oni_domain_configuration_copy(std::uint64_t session,MAOniDomainConfig* config,MAOniDomainPhysics* physics,
    std::uint8_t* mask,std::uint32_t capacity,std::uint32_t* count) {
    return boundary([&] {
        if (!config || !physics || !count || (!mask && capacity)) throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid configured domain output");
        MAOniDomainConfig geometry{};MAOniDomainPhysics settings{};std::vector<std::uint8_t> solids;
        try { ma::oni::copy_domain_configuration(session,geometry,settings,solids); }
        catch (const std::invalid_argument& error) { throw Failure(MA_ONI_INVALID_ARGUMENT,error.what()); }
        catch (const std::logic_error& error) { throw Failure(MA_ONI_BUSY,error.what()); }
        if (mask && capacity<solids.size()) throw Failure(MA_ONI_BUFFER_TOO_SMALL,"Configured solid mask capacity is insufficient");
        if (mask) std::copy(solids.begin(),solids.end(),mask);
        *config=geometry;*physics=settings;*count=static_cast<std::uint32_t>(solids.size());
    });
}
MAOniResult ma_oni_domain_checkpoint_validate(const MAOniDomainConfig* config,const std::uint8_t* image,std::uint32_t size,double saved_time) {
    return boundary([&] {
        if (!config || !image || !size) throw Failure(MA_ONI_INVALID_ARGUMENT,"Native checkpoint validation source is missing");
        try { ma::oni::validate_domain_checkpoint(*config,std::span(image,size),saved_time); }
        catch (const std::invalid_argument& error) { throw Failure(MA_ONI_INVALID_ARGUMENT,error.what()); }
    });
}
MAOniResult ma_oni_domain_restore(const MAOniDomainConfig* config,const std::uint8_t* image,std::uint32_t size,double saved_time,MAOniDomainStatus* output) {
    return boundary([&] {
        if (!image || !size) throw Failure(MA_ONI_INVALID_ARGUMENT,"Native checkpoint restore source is missing");
        start_domain_checked(config,output,std::span(image,size),saved_time);
    });
}
MAOniResult ma_oni_domain_copy(MAOniDomainStatus* output,MAOniDomainCell* cells,std::uint32_t capacity) {
    return boundary([&] {
        if (!output || output->struct_size != sizeof(*output) || output->abi_version != MA_ONI_DOMAIN_ABI_VERSION || (!cells && capacity))
            throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid native domain snapshot output");
        try { ma::oni::copy_domain(*output,cells,capacity); }
        catch (const std::length_error& error) { throw Failure(MA_ONI_BUFFER_TOO_SMALL,error.what()); }
    });
}
MAOniResult ma_oni_domain_stop(MAOniDomainStatus* output) {
    return boundary([&] {
        if (!output || output->struct_size != sizeof(*output) || output->abi_version != MA_ONI_DOMAIN_ABI_VERSION)
            throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid native domain output ABI");
        ma::oni::stop_domain(); ma::oni::copy_domain(*output,nullptr,0);
    });
}
std::uint32_t ma_oni_domain_heat_abi_version() { return 1; }
std::uint32_t ma_oni_storage_heat_version() {return ma::oni::storage_heat::Monitor::supported()?1U:0U;}
MAOniResult ma_oni_storage_heat_register(std::int32_t handle,float temperature,float capacity) {
    return boundary([&] {
        if(!installed || !ma::oni::storage_heat::Monitor::supported() || !ma::oni::colony::active())
            throw Failure(MA_ONI_INCOMPATIBLE,"Exact storage native heat tracking requires a supported attached colony");
        storage_heat.add(handle,temperature,capacity);
    });
}
MAOniResult ma_oni_storage_heat_expect_data(std::int32_t handle,float temperature,float capacity) {
    return boundary([&] {storage_heat.expect(handle,temperature,capacity);});
}
MAOniResult ma_oni_storage_heat_copy(MAOniStorageHeatReceipt* output,std::uint32_t capacity,std::uint32_t* count) {
    return boundary([&] {
        if(!count || (!output && capacity))throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid exact storage heat receipt destination");
        try {*count=storage_heat.copy(output,capacity);}
        catch(const std::length_error& error){throw Failure(MA_ONI_BUFFER_TOO_SMALL,error.what());}
    });
}
MAOniResult ma_oni_storage_heat_ack(std::uint64_t through) {return boundary([&] {storage_heat.ack(through);});}
MAOniResult ma_oni_storage_heat_reset() {return boundary([&] {storage_heat.reset();});}
std::uint32_t ma_oni_domain_chunk_heat_abi_version() { return 1; }
MAOniResult ma_oni_domain_chunk_heat_status(MAOniDomainChunkHeatStatus* output) {
    return boundary([&] {
        if (!output || output->struct_size != sizeof(*output) || output->abi_version != 1)
            throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid native body heat status ABI");
        ma::oni::copy_domain_chunk_heat_status(*output);
    });
}
std::uint32_t ma_oni_domain_solid_heat_abi_version() { return 1; }
std::uint32_t ma_oni_domain_publication_abi_version() { return profile_publication_bindings.size() == 2 ? 1U : 0U; }
MAOniResult ma_oni_domain_publication_acquire(const void* native_update, MAOniDomainPublicationInfo* output) {
    return boundary([&] {
        if (!output || output->struct_size != sizeof(*output) || output->abi_version != MA_ONI_DOMAIN_PUBLICATION_ABI_VERSION)
            throw Failure(MA_ONI_INVALID_ARGUMENT, "Invalid native publication info ABI");
        try { *output = ma::oni::published_frames::acquire(native_update); }
        catch (const std::invalid_argument& error) { throw Failure(MA_ONI_INVALID_ARGUMENT, error.what()); }
        // length_error is a logic_error; lease exhaustion is not an unowned packet.
        catch (const std::length_error& error) { throw Failure(MA_ONI_INTERNAL_ERROR, error.what()); }
        catch (const std::logic_error& error) { throw Failure(MA_ONI_BUSY, error.what()); }
    });
}
MAOniResult ma_oni_domain_publication_copy(std::uint64_t token, MAOniDomainStatus* output, MAOniDomainCell* cells,
    std::uint32_t capacity, MAOniDomainSolidHeatStatus* heat, MAOniDomainSolidHeatEndpoint* endpoint, MAOniDomainSolidCellState* solid) {
    return boundary([&] {
        if (!output || output->struct_size != sizeof(*output) || output->abi_version != MA_ONI_DOMAIN_ABI_VERSION || !cells ||
            !heat || heat->struct_size != sizeof(*heat) || heat->abi_version != 1 ||
            !endpoint || endpoint->struct_size != sizeof(*endpoint) || endpoint->abi_version != 1 ||
            !solid || solid->struct_size != sizeof(*solid) || solid->abi_version != 1)
            throw Failure(MA_ONI_INVALID_ARGUMENT, "Invalid leased publication output ABI");
        std::shared_ptr<const ma::oni::published_frames::Frame> frame;
        try { frame = ma::oni::published_frames::read(token); }
        catch (const std::invalid_argument& error) { throw Failure(MA_ONI_INVALID_ARGUMENT, error.what()); }
        if (capacity < frame->cells.size()) throw Failure(MA_ONI_BUFFER_TOO_SMALL, "Leased cell output is too small");
        std::copy(frame->cells.begin(), frame->cells.end(), cells);
        *output = frame->status; *heat = frame->solid_heat; *endpoint = frame->solid_endpoint; *solid = frame->solid_cell;
    });
}
MAOniResult ma_oni_domain_publication_cells(std::uint64_t token, const MAOniDomainCell** cells, std::uint32_t* count) {
    return boundary([&] {
        if (!cells || !count) throw Failure(MA_ONI_INVALID_ARGUMENT, "Missing leased cell view output");
        std::shared_ptr<const ma::oni::published_frames::Frame> frame;
        try { frame = ma::oni::published_frames::read(token); }
        catch (const std::invalid_argument& error) { throw Failure(MA_ONI_INVALID_ARGUMENT, error.what()); }
        *cells = frame->cells.data(); *count = static_cast<std::uint32_t>(frame->cells.size());
    });
}
MAOniResult ma_oni_domain_publication_release(std::uint64_t token) {
    return boundary([&] {
        try { ma::oni::published_frames::release(token); }
        catch (const std::invalid_argument& error) { throw Failure(MA_ONI_INVALID_ARGUMENT, error.what()); }
    });
}
MAOniResult ma_oni_domain_solid_heat_status(MAOniDomainSolidHeatStatus* output) {
    return boundary([&] {
        if(!output || output->struct_size!=sizeof(*output) || output->abi_version!=1)
            throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid native solid heat status ABI");
        ma::oni::copy_domain_solid_heat_status(*output);
    });
}
MAOniResult ma_oni_domain_solid_heat_endpoint(MAOniDomainSolidHeatEndpoint* output) {
    return boundary([&] {
        if(!output || output->struct_size!=sizeof(*output) || output->abi_version!=1)
            throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid native solid endpoint observation ABI");
        ma::oni::copy_domain_solid_heat_endpoint(*output);
    });
}
MAOniResult ma_oni_domain_solid_cell_state(MAOniDomainSolidCellState* output) {
    return boundary([&] {
        if(!output || output->struct_size!=sizeof(*output) || output->abi_version!=1)
            throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid completed native solid cell observation ABI");
        ma::oni::copy_domain_solid_cell_state(*output);
    });
}
MAOniResult ma_oni_domain_solid_heat_configure(const MAOniDomainSolidHeatCell* inputs,std::uint32_t count,MAOniDomainSolidHeatStatus* output) {
    return boundary([&] {
        if(!inputs || !count || count>128 || !output || output->struct_size!=sizeof(*output) || output->abi_version!=1)
            throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid native solid heat configuration ABI");
        std::lock_guard frame_lock(ma::oni::domain_frame_mutex());
        try {ma::oni::configure_domain_solid_heat(inputs,count,*output);}
        catch(const std::invalid_argument& error) {throw Failure(MA_ONI_INVALID_ARGUMENT,error.what());}
        catch(const std::logic_error& error) {throw Failure(MA_ONI_BUSY,error.what());}
    });
}
std::uint32_t ma_oni_domain_solid_heat_startup_abi_version() { return 1; }
MAOniResult ma_oni_domain_start_with_solid_heat(const MAOniDomainConfig* config,const MAOniDomainSolidHeatCell* inputs,
    std::uint32_t count,MAOniDomainStatus* output,MAOniDomainSolidHeatStatus* heat) {
    return boundary([&] {
        if(!inputs || !count || count>128 || !heat || heat->struct_size!=sizeof(*heat) || heat->abi_version!=1)
            throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid native solid heat startup ABI");
        ma::oni::DomainSolidHeatStartup startup{std::span(inputs,count)};
        start_domain_checked(config,output,{},0,{},nullptr,{},&startup);
        *heat=startup.heat;
    });
}
MAOniResult ma_oni_domain_heat_status(MAOniDomainHeatStatus* output) {
    return boundary([&] {
        if (!output || output->struct_size != sizeof(*output) || output->abi_version != 1)
            throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid native heat status ABI");
        ma::oni::copy_domain_heat(*output);
    });
}
MAOniResult ma_oni_domain_queue_heat(const MAOniDomainHeat* inputs,std::uint32_t count,MAOniDomainHeatStatus* output) {
    return boundary([&] {
        if (!inputs || count == 0 || count > 256 || !output || output->struct_size != sizeof(*output) || output->abi_version != 1)
            throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid native heat batch/output ABI");
        try { ma::oni::queue_domain_heat(inputs,count,*output); }
        catch (const ma::FluidFatalError& error) { throw Failure(MA_ONI_INCOMPATIBLE,error.what()); }
        catch (const std::invalid_argument& error) { throw Failure(MA_ONI_INVALID_ARGUMENT,error.what()); }
        catch (const std::logic_error& error) { throw Failure(MA_ONI_BUSY,error.what()); }
    });
}
std::uint32_t ma_oni_domain_transfer_abi_version() { return MA_ONI_DOMAIN_TRANSFER_ABI_VERSION; }
MAOniResult ma_oni_domain_transfer_status(MAOniDomainTransferStatus* output) {
    return boundary([&] {
        if (!output || output->struct_size != sizeof(*output) || output->abi_version != MA_ONI_DOMAIN_TRANSFER_ABI_VERSION)
            throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid native transfer status ABI");
        ma::oni::copy_domain_transfer_status(*output);
    });
}
MAOniResult ma_oni_domain_queue_transfers(const MAOniDomainTransfer* inputs,std::uint32_t count,MAOniDomainTransferStatus* output) {
    return boundary([&] {
        if (!inputs || !count || count > 256 || !output || output->struct_size != sizeof(*output) || output->abi_version != MA_ONI_DOMAIN_TRANSFER_ABI_VERSION)
            throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid native transfer batch/output ABI");
        try { ma::oni::queue_domain_transfers(inputs,count,*output); }
        catch (const ma::FluidFatalError& error) { throw Failure(MA_ONI_INCOMPATIBLE,error.what()); }
        catch (const std::invalid_argument& error) { throw Failure(MA_ONI_INVALID_ARGUMENT,error.what()); }
        catch (const std::logic_error& error) { throw Failure(MA_ONI_BUSY,error.what()); }
    });
}
MAOniResult ma_oni_domain_copy_receipts(MAOniDomainStatus* output,MAOniDomainCell* cells,std::uint32_t capacity,
    MAOniDomainReceipt* receipts,std::uint32_t receipt_capacity,std::uint32_t* count) {
    return boundary([&] {
        if (!output || output->struct_size != sizeof(*output) || output->abi_version != MA_ONI_DOMAIN_ABI_VERSION || !cells || !count || (!receipts && receipt_capacity))
            throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid native physical frame/receipt output ABI");
        try { ma::oni::copy_domain_receipts(*output,cells,capacity,receipts,receipt_capacity,*count); }
        catch (const std::length_error& error) { throw Failure(MA_ONI_BUFFER_TOO_SMALL,error.what()); }
    });
}
MAOniResult ma_oni_domain_ack_receipts(std::uint64_t session,std::uint64_t through) {
    return boundary([&] {
        try { ma::oni::ack_domain_receipts(session,through); }
        catch (const std::invalid_argument& error) { throw Failure(MA_ONI_INVALID_ARGUMENT,error.what()); }
        catch (const std::logic_error& error) { throw Failure(MA_ONI_BUSY,error.what()); }
    });
}
std::uint32_t ma_oni_domain_actor_abi_version() { return 1; }
std::uint32_t ma_oni_domain_thermal_abi_version() { return 2; }
MAOniResult ma_oni_domain_diffusion_configure(std::uint64_t session,std::uint64_t frame,std::uint32_t mode,double multiplier) {
    return boundary([&] {
        try { ma::oni::configure_domain_diffusion(session,frame,mode,multiplier); }
        catch(const std::invalid_argument& e) { throw Failure(MA_ONI_INVALID_ARGUMENT,e.what()); }
        catch(const std::logic_error& e) { throw Failure(MA_ONI_BUSY,e.what()); }
    });
}
MAOniResult ma_oni_domain_diffusion_status(MAOniDomainDiffusionStatus* output) {
    return boundary([&] {
        if(!output || output->struct_size!=sizeof(*output) || output->abi_version!=2)
            throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid gas diffusion status ABI");
        ma::oni::copy_domain_diffusion_status(*output);
    });
}
MAOniResult ma_oni_domain_thermal_status(MAOniDomainThermalStatus* output) {
    return boundary([&] {
        if (!output || output->struct_size != sizeof(*output) || output->abi_version != 2)
            throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid native conduction status ABI");
        ma::oni::copy_domain_thermal_status(*output);
    });
}
std::uint32_t ma_oni_domain_gravity_abi_version() { return 1; }
std::uint32_t ma_oni_domain_viscosity_abi_version() { return 1; }
std::uint32_t ma_oni_domain_radial_consumption_abi_version() { return 1; }
std::uint32_t ma_oni_domain_rectangular_consumption_abi_version() { return 1; }
MAOniResult ma_oni_domain_copy_occupied_volumes(MAOniDomainStatus* output,double* volumes,std::uint32_t capacity) {
    return boundary([&] {
        if (!output || output->struct_size!=sizeof(*output) || output->abi_version!=MA_ONI_DOMAIN_ABI_VERSION || !volumes)
            throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid native material volume output ABI");
        try { ma::oni::copy_domain_occupied_volumes(*output,volumes,capacity); }
        catch (const std::length_error& error) { throw Failure(MA_ONI_BUFFER_TOO_SMALL,error.what()); }
    });
}
MAOniResult ma_oni_domain_copy_phase_frame(MAOniDomainStatus* output,MAOniDomainCell* cells,double* volumes,std::uint32_t capacity,
    MAOniDomainSubcellStatus* account,MAOniDomainReceipt* receipts,std::uint32_t receipt_capacity,std::uint32_t* count) {
    return boundary([&] {
        if (!output || output->struct_size!=sizeof(*output) || output->abi_version!=MA_ONI_DOMAIN_ABI_VERSION || !cells || !volumes ||
            !account || account->struct_size!=sizeof(*account) || account->abi_version!=1 || !count || (!receipts && receipt_capacity))
            throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid native physical phase frame ABI");
        try { ma::oni::copy_domain_phase_frame(*output,cells,volumes,capacity,*account,receipts,receipt_capacity,*count); }
        catch (const std::length_error& error) { throw Failure(MA_ONI_BUFFER_TOO_SMALL,error.what()); }
    });
}
MAOniResult ma_oni_domain_subcell_status(MAOniDomainSubcellStatus* output) {
    return boundary([&] {
        if (!output || output->struct_size!=sizeof(*output) || output->abi_version!=1)
            throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid native subcell status ABI");
        ma::oni::copy_domain_subcell_status(*output);
    });
}
MAOniResult ma_oni_domain_viscosity_status(MAOniDomainViscosityStatus* output) {
    return boundary([&] {
        if (!output || output->struct_size!=sizeof(*output) || output->abi_version!=1)
            throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid native physical viscosity status ABI");
        ma::oni::copy_domain_viscosity_status(*output);
    });
}
MAOniResult ma_oni_domain_gravity_status(MAOniDomainGravityStatus* output) {
    return boundary([&] {
        if (!output || output->struct_size != sizeof(*output) || output->abi_version != 1)
            throw Failure(MA_ONI_INVALID_ARGUMENT,"Native gravity output ABI differs");
        ma::oni::copy_domain_gravity_status(*output);
    });
}
MAOniResult ma_oni_domain_actor_status(MAOniDomainActorStatus* output) {
    return boundary([&] {
        if (!output || output->struct_size != sizeof(*output) || output->abi_version != 1)
            throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid native actor status ABI");
        ma::oni::copy_domain_actor_status(*output);
    });
}
MAOniResult ma_oni_domain_configure_actor(const MAOniDomainActor* input,MAOniDomainActorStatus* output) {
    return boundary([&] {
        if (!input || !output || output->struct_size != sizeof(*output) || output->abi_version != 1)
            throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid native actor configuration/output ABI");
        try { ma::oni::configure_domain_actor(*input,*output); }
        catch (const std::invalid_argument& error) { throw Failure(MA_ONI_INVALID_ARGUMENT,error.what()); }
        catch (const std::logic_error& error) { throw Failure(MA_ONI_BUSY,error.what()); }
    });
}
MAOniResult ma_oni_domain_configure_actors(const MAOniDomainActor* inputs,std::uint32_t count,MAOniDomainActorStatus* output) {
    return boundary([&] {
        if (!output || output->struct_size != sizeof(*output) || output->abi_version != 1)
            throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid native actor batch output ABI");
        try { ma::oni::configure_domain_actors(inputs,count,*output); }
        catch (const std::invalid_argument& error) { throw Failure(MA_ONI_INVALID_ARGUMENT,error.what()); }
        catch (const std::logic_error& error) { throw Failure(MA_ONI_BUSY,error.what()); }
    });
}
std::uint32_t ma_oni_domain_actor_checkpoint_abi_version() { return 1; }
std::uint32_t ma_oni_domain_pending_actor_restores() { return ma::oni::pending_domain_actor_restores(); }
MAOniResult ma_oni_domain_configure_persistent_actors(const MAOniDomainPersistentActor* inputs,std::uint32_t count,MAOniDomainActorStatus* output) {
    return boundary([&] {
        static_assert(sizeof(MAOniDomainPersistentActor)==96);
        if (!output || output->struct_size!=sizeof(*output) || output->abi_version!=1)
            throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid persistent actor batch output ABI");
        try { ma::oni::configure_persistent_domain_actors(inputs,count,*output); }
        catch (const std::invalid_argument& error) { throw Failure(MA_ONI_INVALID_ARGUMENT,error.what()); }
        catch (const std::logic_error& error) { throw Failure(MA_ONI_BUSY,error.what()); }
    });
}
MAOniResult ma_oni_domain_snapshot_copy(std::uint64_t session,std::uint64_t frame,std::uint8_t* output,std::uint32_t capacity,std::uint32_t* required) {
    return boundary([&] {
        if (!required || (!output && capacity)) throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid complete snapshot output");
        std::vector<std::uint8_t> image;
        try { image=ma::oni::snapshot_domain(session,frame); }
        catch (const std::invalid_argument& error) { throw Failure(MA_ONI_BUSY,error.what()); }
        catch (const std::logic_error& error) { throw Failure(MA_ONI_BUSY,error.what()); }
        if (output && capacity<image.size()) throw Failure(MA_ONI_BUFFER_TOO_SMALL,"Complete snapshot buffer is too small");
        if (output) std::copy(image.begin(),image.end(),output);
        *required=static_cast<std::uint32_t>(image.size());
    });
}
std::uint32_t ma_oni_domain_completed_snapshot_abi_version() { return 1; }
MAOniResult ma_oni_domain_completed_snapshot_copy(std::uint64_t session,std::uint64_t frame,std::uint8_t* output,
    std::uint32_t capacity,std::uint32_t* required) {
    return boundary([&] {
        if (!required || (!output && capacity)) throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid completed frame snapshot output");
        std::vector<std::uint8_t> image;
        try {image=ma::oni::copy_completed_domain_snapshot(session,frame);}
        catch (const std::logic_error& error) {throw Failure(MA_ONI_BUSY,error.what());}
        if (output && capacity<image.size()) throw Failure(MA_ONI_BUFFER_TOO_SMALL,"Completed frame snapshot capacity is insufficient");
        if (output) std::copy(image.begin(),image.end(),output);
        *required=static_cast<std::uint32_t>(image.size());
    });
}
MAOniResult ma_oni_domain_snapshot_advance(const std::uint8_t* input,std::uint32_t count,double seconds,
    std::uint8_t* output,std::uint32_t capacity,std::uint32_t* required) {
    return boundary([&] {
        if (!input || !count || !required || (!output && capacity)) throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid complete snapshot input/output");
        std::vector<std::uint8_t> image;
        try { image=ma::oni::advance_domain_snapshot(std::span(input,count),seconds); }
        catch (const std::invalid_argument& error) { throw Failure(MA_ONI_INVALID_ARGUMENT,error.what()); }
        if (output && capacity<image.size()) throw Failure(MA_ONI_BUFFER_TOO_SMALL,"Resumed complete snapshot buffer is too small");
        if (output) std::copy(image.begin(),image.end(),output);
        *required=static_cast<std::uint32_t>(image.size());
    });
}
std::uint32_t ma_oni_domain_frame_inputs_abi_version() { return 1; }
MAOniResult ma_oni_domain_frame_inputs_copy(std::uint64_t session,std::uint64_t frame,std::uint8_t* output,
    std::uint32_t capacity,std::uint32_t* required) {
    return boundary([&] {
        if (!required || (!output && capacity)) throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid frame input output");
        std::vector<std::uint8_t> image;
        try {image=ma::oni::copy_domain_frame_inputs(session,frame);}
        catch (const std::logic_error& error) {throw Failure(MA_ONI_BUSY,error.what());}
        if (output && capacity<image.size()) throw Failure(MA_ONI_BUFFER_TOO_SMALL,"Frame input output capacity is insufficient");
        if (output) std::copy(image.begin(),image.end(),output);
        *required=static_cast<std::uint32_t>(image.size());
    });
}
MAOniResult ma_oni_domain_snapshot_advance_with_inputs(const std::uint8_t* source,std::uint32_t source_bytes,
    const std::uint8_t* inputs,std::uint32_t input_bytes,std::uint8_t* output,std::uint32_t capacity,std::uint32_t* required) {
    return boundary([&] {
        if (!source || !source_bytes || !inputs || !input_bytes || !required || (!output && capacity))
            throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid frame input replay buffers");
        std::vector<std::uint8_t> image;
        try {image=ma::oni::advance_domain_snapshot_with_inputs(std::span(source,source_bytes),std::span(inputs,input_bytes));}
        catch (const std::logic_error& error) {throw Failure(MA_ONI_INVALID_ARGUMENT,error.what());}
        if (output && capacity<image.size()) throw Failure(MA_ONI_BUFFER_TOO_SMALL,"Frame input replay capacity is insufficient");
        if (output) std::copy(image.begin(),image.end(),output);
        *required=static_cast<std::uint32_t>(image.size());
    });
}
std::uint32_t ma_oni_domain_checkpoint_abi_version() { return 1; }
MAOniResult ma_oni_domain_checkpoint_copy(std::uint64_t session,std::uint64_t frame,std::uint8_t* output,std::uint32_t capacity,std::uint32_t* required) {
    return boundary([&] {
        if (!required || (!output && capacity)) throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid physical checkpoint output");
        std::vector<std::uint8_t> image;
        try { image=ma::oni::checkpoint_domain(session,frame); }
        catch (const std::logic_error& error) { throw Failure(MA_ONI_BUSY,error.what()); }
        if (output && capacity < image.size()) throw Failure(MA_ONI_BUFFER_TOO_SMALL,"Physical checkpoint buffer is too small");
        if (output) std::copy(image.begin(),image.end(),output);
        *required=static_cast<std::uint32_t>(image.size());
    });
}
MAOniResult ma_oni_domain_checkpoint_advance(const std::uint8_t* input,std::uint32_t count,double seconds,
    std::uint8_t* output,std::uint32_t capacity,std::uint32_t* required) {
    return boundary([&] {
        if (!input || !count || !required || (!output && capacity)) throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid physical checkpoint input/output");
        std::vector<std::uint8_t> image;
        try { image=ma::oni::advance_domain_checkpoint(std::span(input,count),seconds); }
        catch (const std::invalid_argument& error) { throw Failure(MA_ONI_INVALID_ARGUMENT,error.what()); }
        if (output && capacity < image.size()) throw Failure(MA_ONI_BUFFER_TOO_SMALL,"Resumed physical checkpoint buffer is too small");
        if (output) std::copy(image.begin(),image.end(),output);
        *required=static_cast<std::uint32_t>(image.size());
    });
}
std::uint32_t ma_oni_domain_pair_abi_version() { return 1; }
static MAOniResult request_domain_pair(std::uint64_t session,std::uint64_t frame,std::int32_t x,std::int32_t y,MAOniDomainPairStatus* output,bool include_actors) {
    return boundary([&] {
        static_assert(sizeof(MAOniDomainPairStatus)==112);
        if (!output || output->struct_size!=sizeof(*output) || output->abi_version!=1 || x<0 || y<0)
            throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid paired checkpoint request/output ABI");
        std::lock_guard pair_lock(pair_mutex);
        if (!installed || recovery_required || captured_pair)
            throw Failure(MA_ONI_BUSY,"Paired capture requires attached hooks and a released prior token");
        std::lock_guard save_lock(native_save_mutex);
        if (native_save_open) throw Failure(MA_ONI_BUSY,"A native save buffer is still borrowed");
        auto candidate=std::make_unique<DomainPair>(); MAOniDomainStatus source{};
        try { ma::oni::domain_checkpoint_source(session,frame,candidate->status.configuration,source,include_actors); }
        catch (const std::invalid_argument& error) { throw Failure(MA_ONI_BUSY,error.what()); }
        catch (const std::logic_error& error) { throw Failure(MA_ONI_BUSY,error.what()); }
        if ((!source.completed_frames && source.time_seconds<=0) || source.start_thread!=native_thread())
            throw Failure(MA_ONI_BUSY,"Paired request requires a completed frame on its activation thread");
        if (pair_sequence==UINT64_MAX) throw Failure(MA_ONI_BUSY,"Paired checkpoint token range exhausted");
        candidate->x=x; candidate->y=y;candidate->include_actors=include_actors;
        auto& result=candidate->status; result.struct_size=sizeof(result); result.abi_version=1; result.state=1;
        result.token=++pair_sequence; result.session=session; result.completed_frame=frame; result.physical_time_seconds=source.time_seconds;
        captured_pair=std::move(candidate); pair_pending.store(true,std::memory_order_release); *output=captured_pair->status;
    });
}
MAOniResult ma_oni_domain_pair_request(std::uint64_t session,std::uint64_t frame,std::int32_t x,std::int32_t y,MAOniDomainPairStatus* output) {
    return request_domain_pair(session,frame,x,y,output,false);
}
MAOniResult ma_oni_domain_pair_snapshot_request(std::uint64_t session,std::uint64_t frame,std::int32_t x,std::int32_t y,MAOniDomainPairStatus* output) {
    return request_domain_pair(session,frame,x,y,output,true);
}
MAOniResult ma_oni_domain_pair_poll(std::uint64_t token,MAOniDomainPairStatus* output) {
    return boundary([&] {
        if (!output || output->struct_size!=sizeof(*output) || output->abi_version!=1)
            throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid paired checkpoint poll ABI");
        std::lock_guard lock(pair_mutex);
        if (!captured_pair || !token || captured_pair->status.token!=token) throw Failure(MA_ONI_INVALID_ARGUMENT,"Unknown paired checkpoint poll token");
        if (captured_pair->status.state==3) throw Failure(captured_pair->error_code,captured_pair->error.data());
        *output=captured_pair->status;
    });
}
MAOniResult ma_oni_domain_pair_copy(std::uint64_t token,std::uint8_t* native,std::uint32_t native_capacity,std::uint8_t* physical,std::uint32_t physical_capacity) {
    return boundary([&] {
        std::lock_guard lock(pair_mutex);
        if (!captured_pair || !token || captured_pair->status.token!=token) throw Failure(MA_ONI_INVALID_ARGUMENT,"Unknown paired checkpoint token");
        if (!native || !physical) throw Failure(MA_ONI_INVALID_ARGUMENT,"Both paired checkpoint buffers are required");
        const auto& source=*captured_pair;
        if (source.status.state==3) throw Failure(source.error_code,source.error.data());
        if (source.status.state!=2) throw Failure(MA_ONI_BUSY,"Paired checkpoint capture is not complete");
        if (native_capacity<source.native.size() || physical_capacity<source.physical.size()) throw Failure(MA_ONI_BUFFER_TOO_SMALL,"Paired checkpoint buffers are too small");
        const auto a=reinterpret_cast<std::uintptr_t>(native),b=reinterpret_cast<std::uintptr_t>(physical);
        if (a>UINTPTR_MAX-source.native.size() || b>UINTPTR_MAX-source.physical.size() ||
            (a<b+source.physical.size() && b<a+source.native.size())) throw Failure(MA_ONI_INVALID_ARGUMENT,"Paired checkpoint output buffers overlap");
        std::copy(source.native.begin(),source.native.end(),native); std::copy(source.physical.begin(),source.physical.end(),physical);
    });
}
MAOniResult ma_oni_domain_pair_release(std::uint64_t token) {
    return boundary([&] {
        std::lock_guard lock(pair_mutex);
        if (token && token==last_released_pair) return;
        if (!captured_pair || !token || captured_pair->status.token!=token) throw Failure(MA_ONI_INVALID_ARGUMENT,"Unknown paired checkpoint release token");
        captured_pair.reset(); pair_pending.store(false,std::memory_order_release); last_released_pair=token;
    });
}
}
