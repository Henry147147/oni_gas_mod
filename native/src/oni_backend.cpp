#include "mixed_atmospheres/oni_backend.h"
#include "oni_sha256.hpp"
#include "oni_domain.hpp"
#include "mixed_atmospheres/fluid_thermodynamics.hpp"
#include <funchook.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <stdexcept>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#include <link.h>
#endif

namespace {
static_assert(sizeof(void*) == 8, "ONI native observer requires x64");
static_assert(sizeof(MAOniObserverConfig) == 24);
static_assert(sizeof(MAOniObserverStatus) == 512);
static_assert(sizeof(MAOniTraceEvent) == 32);
static_assert(MA_ONI_EMITTER_DISPATCH + 1 == MA_ONI_HOOK_COUNT);
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
}
FORWARD(on_message, MA_ONI_MESSAGE, void*, (int id,int size,std::uint8_t* data), (id,size,data))
FORWARD(on_messages, MA_ONI_MESSAGES, void*, (int id,int size,int count,std::uint8_t* data), (id,size,count,data))
std::uint8_t* on_begin_save(int* size,int x,int y) {
    Call call(MA_ONI_BEGIN_SAVE);
    if (ma::oni::domain_active()) {
        if (size) *size=0;
        std::snprintf(last_error.data(),last_error.size(),"The custom native domain requires an integrated checkpoint; native scalar saving is disabled");
        return nullptr;
    }
    return original<std::uint8_t* (*)(int*,int,int)>(MA_ONI_BEGIN_SAVE)(size,x,y);
}
FORWARD(on_end_save, MA_ONI_END_SAVE, void, (), ())
FORWARD(on_main, MA_ONI_MAIN, void, (void* self), (self))
FORWARD(on_process_frame, MA_ONI_PROCESS_FRAME, void, (void* frame,void* data), (frame,data))
float on_next_frame(void* self,void* data) {
    Call call(MA_ONI_NEXT_FRAME);
    const float dt = original<float (*)(void*,void*)>(MA_ONI_NEXT_FRAME)(self,data);
    call.bits = std::bit_cast<std::uint32_t>(dt);
    if (dt > 0) pending_timestep={positive_steps.fetch_add(1,std::memory_order_relaxed)+1,dt};
    return dt;
}
void on_update_data(void* self,void* data) {
    Call call(MA_ONI_UPDATE_DATA);
    // Native publication can wake the managed reader inside UpdateData. Keep
    // both owners behind the same fence until the original update completes.
    std::lock_guard frame_lock(ma::oni::domain_frame_mutex());
    const auto pending=pending_timestep; pending_timestep={};
    if (pending.ticket) ma::oni::advance_domain(pending.ticket,pending.seconds,native_thread());
    original<void (*)(void*,void*)>(MA_ONI_UPDATE_DATA)(self,data);
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
    if (!ma::oni::suppress_domain_cell(MA_ONI_TEMPERATURE,a) && !ma::oni::suppress_domain_cell(MA_ONI_TEMPERATURE,b))
        original<void (*)(void*,void*,int,int)>(MA_ONI_TEMPERATURE)(data,events,a,b);
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
FORWARD(on_end_frame, MA_ONI_END_FRAME, void, (void* self), (self))
FORWARD(on_consumer, MA_ONI_CONSUMER, void, (void* self,float dt,void* data,const void* region), (self,dt,data,region))
FORWARD(on_emitter, MA_ONI_EMITTER, void, (void* self,float dt,void* data,const void* region), (self,dt,data,region))
FORWARD(on_emitter_dispatch, MA_ONI_EMITTER_DISPATCH, void, (void* self,float dt,void* data,const void* region), (self,dt,data,region))
FORWARD(on_building_heat, MA_ONI_BUILDING_HEAT, void, (void* self,float dt,void* data,const void* region), (self,dt,data,region))
#undef FORWARD

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
    reinterpret_cast<void*>(&on_displacement<MA_ONI_LIQUID_DISPLACEMENT>), reinterpret_cast<void*>(&on_emitter_dispatch)};

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
    positive_steps=0; trace_reserved=0;
    for (auto& ready : trace_ready) ready=false;
}
template<class Action> MAOniResult boundary(Action action) noexcept {
    try { std::lock_guard lock(lifecycle); action(); last_error[0]='\0'; return MA_ONI_OK; }
    catch (const Failure& error) { std::snprintf(last_error.data(),last_error.size(),"%s",error.what()); return error.result; }
    catch (const std::exception& error) { std::snprintf(last_error.data(),last_error.size(),"%s",error.what()); return MA_ONI_INTERNAL_ERROR; }
    catch (...) { std::snprintf(last_error.data(),last_error.size(),"Unknown observer error"); return MA_ONI_INTERNAL_ERROR; }
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
        detours=funchook_create();
        if (!detours) throw Failure(MA_ONI_HOOK_ERROR,"Cannot allocate native detours");
        try {
            for (const auto& hook : profile_hooks) {
                originals[hook.index]=reinterpret_cast<void*>(image_base+hook.rva);
                if (funchook_prepare(detours,&originals[hook.index],replacements[hook.index]) != 0) {
                    std::array<char,2048> detail{};
                    std::snprintf(detail.data(),detail.size(),"Hook %u: %s",hook.index,funchook_error_message(detours));
                    throw Failure(MA_ONI_HOOK_ERROR,detail.data());
                }
            }
            if (funchook_install(detours,0) != 0) throw Failure(MA_ONI_HOOK_ERROR,funchook_error_message(detours));
            installed=true;
            for (const auto& hook : profile_hooks) installed_mask |= std::uint64_t{1} << hook.index;
            *output=capture();
        } catch (...) {
            // Prepare is non-mutating, but install can fail after partial work.
            // Verify restoration, retain live trampolines if recovery fails.
            (void)funchook_uninstall(detours,0);
            if (fingerprints_match()) {
                (void)funchook_destroy(detours); detours=nullptr;
            } else recovery_required=true;
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
        (void)funchook_destroy(detours); detours=nullptr;
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
    return boundary([&] {
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
        try { ma::oni::start_domain(*config,native_thread()); }
        catch (const std::invalid_argument& error) { throw Failure(MA_ONI_INVALID_ARGUMENT,error.what()); }
        ma::oni::copy_domain(*output,nullptr,0);
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
}
