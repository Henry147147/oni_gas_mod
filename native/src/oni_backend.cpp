#include "mixed_atmospheres/oni_backend.h"
#include "oni_sha256.hpp"
#include "oni_domain.hpp"
#include "oni_funchook_storage.hpp"
#include "mixed_atmospheres/fluid_thermodynamics.hpp"
#include <funchook.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
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
static_assert(sizeof(MAOniTraceEvent) == 32);
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
// Both modules remain resident. Never free cached P/Invoke targets or native
// trampolines while any simulator could retain their addresses.
void* game_module{};

std::uint64_t native_thread() noexcept {
    thread_local const auto thread=thread_sequence.fetch_add(1,std::memory_order_relaxed)+1;
    return thread;
}
struct PendingTimestep { std::uint64_t ticket{}; float seconds{}; };
thread_local PendingTimestep pending_timestep;
// Cell temperature pairs can execute on native job threads. The outer frame
// fence owns one interval for all of them; thread-local duration misses those
// contacts even though body components run on the frame's main worker.
std::atomic<std::uint32_t> native_heat_contact_bits{};
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
template<class Function> Function original(std::uint32_t index) noexcept {
    return reinterpret_cast<Function>(originals[index]);
}
#define FORWARD(name, index, result, args, invocation) \
    result name args { Call call(index); using Function = result (*) args; return original<Function>(index) invocation; }
using GameCallback = int (*)(int,void*);
FORWARD(on_initialize, MA_ONI_INITIALIZE, void, (GameCallback callback), (callback))
void on_shutdown() {
    Call call(MA_ONI_SHUTDOWN);
    original<void (*)()>(MA_ONI_SHUTDOWN)();
    ma::oni::shutdown_domain();
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
    original<void (*)(void*,void*)>(MA_ONI_PROCESS_FRAME)(frame,data);
}
float on_next_frame(void* self,void* data) {
    Call call(MA_ONI_NEXT_FRAME);
    if (ma::oni::domain_active() && !worker_frame_fence.owns_lock())
        worker_frame_fence=std::unique_lock<std::mutex>(ma::oni::domain_frame_mutex());
    const float dt = original<float (*)(void*,void*)>(MA_ONI_NEXT_FRAME)(self,data);
    native_heat_contact_bits.store(std::bit_cast<std::uint32_t>(dt),std::memory_order_release);
    call.bits = std::bit_cast<std::uint32_t>(dt);
    if (dt > 0) pending_timestep={positive_steps.fetch_add(1,std::memory_order_relaxed)+1,dt};
    return dt;
}
void on_update_data(void* self,void* data) {
    Call call(MA_ONI_UPDATE_DATA);
    // UpdateData performs native physics, including ElementChunk contacts.
    // Commit their ordered boundary inputs before Step publishes GameData.
    // Keep both owners behind the same fence for the entire native update.
    std::unique_lock<std::mutex> frame_lock;
    if (!worker_frame_fence.owns_lock()) frame_lock=std::unique_lock<std::mutex>(ma::oni::domain_frame_mutex());
    const auto pending=pending_timestep; pending_timestep={};
    native_heat_contact_bits.store(std::bit_cast<std::uint32_t>(pending.seconds),std::memory_order_release);
    original<void (*)(void*,void*)>(MA_ONI_UPDATE_DATA)(self,data);
    native_heat_contact_bits.store(0,std::memory_order_release);
    if (pending.ticket) ma::oni::advance_domain(pending.ticket,pending.seconds,native_thread());
}
float on_pressure(void* data,void* events,const void* source,void* target,int cell,std::uint16_t element,std::int8_t direction,float scale,int index) {
    Call call(MA_ONI_PRESSURE);
    if (ma::oni::suppress_domain_cell(MA_ONI_PRESSURE,cell)) return 0;
    using Function=float (*)(void*,void*,const void*,void*,int,std::uint16_t,std::int8_t,float,int);
    return original<Function>(MA_ONI_PRESSURE)(data,events,source,target,cell,element,direction,scale,index);
}
void on_liquid(void* data,void* events,int cell) {
    Call call(MA_ONI_LIQUID);
    if (!ma::oni::suppress_domain_cell(MA_ONI_LIQUID,cell)) original<void (*)(void*,void*,int)>(MA_ONI_LIQUID)(data,events,cell);
}
void on_temperature(void* data,void* events,int a,int b) {
    Call call(MA_ONI_TEMPERATURE);
    const auto owned_a=ma::oni::suppress_domain_cell(MA_ONI_TEMPERATURE,a);
    const auto owned_b=ma::oni::suppress_domain_cell(MA_ONI_TEMPERATURE,b);
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
            insulation[solid],insulation[fluid]);
        if(result.handled && std::bit_cast<std::uint32_t>(result.temperature)!=std::bit_cast<std::uint32_t>(before))
            next_temperatures[solid]=result.temperature;
    }
}
bool on_transition(void* data,void* events,int cell,const void* definition) {
    Call call(MA_ONI_STATE_TRANSITION);
    if (ma::oni::suppress_domain_cell(MA_ONI_STATE_TRANSITION,cell)) return false;
    return original<bool (*)(void*,void*,int,const void*)>(MA_ONI_STATE_TRANSITION)(data,events,cell,definition);
}
void on_load_transition(void* data,int cell) {
    Call call(MA_ONI_LOAD_TRANSITION);
    if (!ma::oni::suppress_domain_cell(MA_ONI_LOAD_TRANSITION,cell)) original<void (*)(void*,int)>(MA_ONI_LOAD_TRANSITION)(data,cell);
}
bool on_sublimation(void* data,void* events,int cell,const void* definition) {
    Call call(MA_ONI_SUBLIMATION);
    if (ma::oni::suppress_domain_cell(MA_ONI_SUBLIMATION,cell)) return false;
    return original<bool (*)(void*,void*,int,const void*)>(MA_ONI_SUBLIMATION)(data,events,cell,definition);
}
void on_post_process(void* data,void* events,int cell) {
    Call call(MA_ONI_POST_PROCESS);
    if (!ma::oni::suppress_domain_cell(MA_ONI_POST_PROCESS,cell)) original<void (*)(void*,void*,int)>(MA_ONI_POST_PROCESS)(data,events,cell);
}
bool on_gas(void* data,void* events,int cell) {
    Call call(MA_ONI_GAS);
    if (ma::oni::suppress_domain_cell(MA_ONI_GAS,cell)) return false;
    return original<bool (*)(void*,void*,int)>(MA_ONI_GAS)(data,events,cell);
}
bool on_gas_pair(void* data,void* events,int a,int b) {
    Call call(MA_ONI_GAS_PAIR);
    if (ma::oni::suppress_domain_cell(MA_ONI_GAS_PAIR,a) || ma::oni::suppress_domain_cell(MA_ONI_GAS_PAIR,b)) return false;
    return original<bool (*)(void*,void*,int,int)>(MA_ONI_GAS_PAIR)(data,events,a,b);
}
template<std::uint32_t Hook>
float on_displacement(std::uint16_t element,int a,int b,int c,void* data) {
    Call call(Hook);
    if (ma::oni::suppress_domain_cell(Hook,a) || ma::oni::suppress_domain_cell(Hook,b) ||
        ma::oni::suppress_domain_cell(Hook,c)) return 0;
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
    if (!ma::oni::domain_active()) return result;
    std::int32_t cell{};float temperature{},capacity{},conductivity{},factor{};
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
    if (ma_funchook_trampoline_capacity()<std::size(profile_hooks)) {
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
    std::span<const std::uint8_t> image,double saved_time,std::span<const MAOniDomainInitialCell> initial_cells={}) {
    std::lock_guard save_lock(native_save_mutex);
    if (native_save_open) throw Failure(MA_ONI_BUSY,"Finish the outstanding native save before activating fluid ownership");
    if (!config || !output || output->struct_size != sizeof(*output) || output->abi_version != MA_ONI_DOMAIN_ABI_VERSION)
        throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid native domain configuration or output ABI");
    if (!installed || recovery_required || !image_base)
        throw Failure(MA_ONI_BUSY,"Native domain requires inspected attached hooks");
    if (ma::oni::domain_active()) throw Failure(MA_ONI_BUSY,"Native domain is already active");
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
    try { ma::oni::start_domain(*config,native_thread(),image,saved_time,initial_cells); }
    catch (const std::invalid_argument& error) { throw Failure(MA_ONI_INVALID_ARGUMENT,error.what()); }
    ma::oni::copy_domain(*output,nullptr,0);
}
}

extern "C" {
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
#if defined(__linux__)
            if (ma_funchook_trampoline_used()!=std::size(profile_hooks))
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
std::uint32_t ma_oni_domain_chunk_heat_abi_version() { return 1; }
MAOniResult ma_oni_domain_chunk_heat_status(MAOniDomainChunkHeatStatus* output) {
    return boundary([&] {
        if (!output || output->struct_size != sizeof(*output) || output->abi_version != 1)
            throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid native body heat status ABI");
        ma::oni::copy_domain_chunk_heat_status(*output);
    });
}
std::uint32_t ma_oni_domain_solid_heat_abi_version() { return 1; }
MAOniResult ma_oni_domain_solid_heat_status(MAOniDomainSolidHeatStatus* output) {
    return boundary([&] {
        if(!output || output->struct_size!=sizeof(*output) || output->abi_version!=1)
            throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid native solid heat status ABI");
        ma::oni::copy_domain_solid_heat_status(*output);
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
std::uint32_t ma_oni_domain_thermal_abi_version() { return 1; }
MAOniResult ma_oni_domain_thermal_status(MAOniDomainThermalStatus* output) {
    return boundary([&] {
        if (!output || output->struct_size != sizeof(*output) || output->abi_version != 1)
            throw Failure(MA_ONI_INVALID_ARGUMENT,"Invalid native conduction status ABI");
        ma::oni::copy_domain_thermal_status(*output);
    });
}
std::uint32_t ma_oni_domain_gravity_abi_version() { return 1; }
std::uint32_t ma_oni_domain_viscosity_abi_version() { return 1; }
std::uint32_t ma_oni_domain_radial_consumption_abi_version() { return 1; }
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
