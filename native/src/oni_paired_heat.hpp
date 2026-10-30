#pragma once
#include "oni_chunk_heat_trace.hpp"
#include "oni_colony.hpp"
#include <array>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <algorithm>
#include <map>
#include <cmath>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>

// Opt-in observations at existing native boundaries. Native addresses identify
// only this UpdateData interval; they are never used as persistent handles.
namespace ma::oni::paired_heat {
namespace d=body_heat_trace::detail;
struct Cell {
    std::uint16_t element=0;
    std::uint32_t mass=0,temperature=0;
    bool valid=false;
};
inline Cell cell(const void* soa,std::int32_t index,std::size_t count) noexcept {
    if(!soa || index<0 || static_cast<std::size_t>(index)>=count)return {};
#if defined(_WIN32)
    constexpr std::size_t e=8,t=40,m=72;
#else
    constexpr std::size_t e=0,t=24,m=48;
#endif
    const auto* elements=d::read<const std::uint16_t*>(soa,e);
    const auto* temperatures=d::read<const float*>(soa,t);
    const auto* masses=d::read<const float*>(soa,m);
    if(!elements || !temperatures || !masses)return {};
    return {elements[index],std::bit_cast<std::uint32_t>(masses[index]),
        std::bit_cast<std::uint32_t>(temperatures[index]),true};
}
inline std::array<const void*,3> storage(const void* soa) noexcept {
    if(!soa)return {};
#if defined(_WIN32)
    constexpr std::array<std::size_t,3> offsets{8,40,72};
#else
    constexpr std::array<std::size_t,3> offsets{0,24,48};
#endif
    return {d::read<const void*>(soa,offsets[0]),d::read<const void*>(soa,offsets[1]),d::read<const void*>(soa,offsets[2])};
}
inline const void* chunk_record(const void* sim,std::int32_t handle,chunk_heat_trace::RegistryLayout layout) noexcept {
    if(!chunk_heat_trace::chunk_at(sim,handle,layout).valid)return nullptr;
    const auto* mapping=d::read<const std::int32_t*>(sim,layout.mapping);
    const auto* records=d::read<const std::byte*>(sim,layout.data);
    return records+static_cast<std::size_t>(mapping[static_cast<std::uint32_t>(handle)&0x00ffffffU])*44;
}
struct Contact {
    const void* accessor=nullptr;const void* record=nullptr;
    std::int32_t index=-1,handle=-1;
    unsigned array=0;bool owned=false,registered=false;
    double specific_heat=0;
    std::array<Cell,2> before{},after{};
    Cell origin{};
    std::uint64_t event=0,completed_event=0,thread=0;
    chunk_heat_trace::Chunk chunk_before{},chunk_after{};
    std::uint32_t returned=0;
};
// Every input is a bounded product of binary32 endpoints and catalogue Cp.
// Retain its exact binary64 additive expansion without depending on solver
// internals or exporting solver arithmetic through the backend test ABI.
class EnergySum {
    std::vector<double> terms_;
public:
    struct Term {double value;double require_representable() const noexcept {return value;}};
    void add(double value) {
        if(!std::isfinite(value))throw std::logic_error("Paired native heat has nonfinite represented work");
        std::vector<double> next;next.reserve(terms_.size()+1);
        for(double term:terms_) {
            double x=value,y=term;if(std::abs(x)<std::abs(y))std::swap(x,y);
            const double total=x+y;
            if(!std::isfinite(total))throw std::logic_error("Paired native heat represented sum overflows");
            const double tail=total-x,error=(x-(total-tail))+(y-tail);
            if(error!=0)next.push_back(error);
            value=total;
        }
        if(value!=0)next.push_back(value);
        terms_=std::move(next);
    }
    void add(Term value) {add(value.value);}
    std::size_t size() const noexcept {return terms_.size();}
    Term component(std::size_t i) const {return {terms_.at(i)};}
    int sign() const noexcept {return terms_.empty()?0:terms_.back()<0?-1:1;}
    Term value() const noexcept {double result=0;for(double term:terms_)result+=term;return {result};}
};
inline double real(std::uint32_t bits) noexcept {return static_cast<double>(std::bit_cast<float>(bits));}
inline bool positive(std::uint32_t bits) noexcept {return bits>0 && bits<0x7f800000U;}
inline double cell_work(const Cell& before,const Cell& after,double cp) {
    if(!before.valid || !after.valid || before.element!=after.element || before.mass!=after.mass ||
        !positive(before.mass) || !positive(before.temperature) || !positive(after.temperature) || !(cp>0) || !std::isfinite(cp))
        throw std::logic_error("Paired native heat changed cell material, mass or thermal validity");
    return real(before.mass)*cp*(real(after.temperature)-real(before.temperature));
}
struct Completion {
    std::vector<colony::NativeHeatAdjustment> heat;
    std::array<char,256> failure{};
};
struct BodyCell {std::int32_t index;Cell initial,before,after;bool owned;double cp;};
struct Event {std::uint64_t begin=0,end=0,thread=0;};
inline std::uint64_t thread_identity() noexcept {
    static std::atomic<std::uint64_t> next{0};
    thread_local const auto identity=next.fetch_add(1,std::memory_order_relaxed)+1;
    return identity;
}
inline bool stable(Cell initial,Cell before,Cell after,double cp) noexcept {
    return initial.valid && before.valid && after.valid && initial.element==before.element && before.element==after.element &&
        initial.mass==before.mass && before.mass==after.mass && positive(before.mass) && positive(before.temperature) && positive(after.temperature) && cp>0 && std::isfinite(cp);
}
inline std::vector<EnergySum> passive_body_corrections(double body_work,std::span<const BodyCell> cells) {
    EnergySum remainder;remainder.add(-body_work);
    double weight=0;std::vector<double> weights;weights.reserve(cells.size());
    for(const auto& cell:cells) {
        if(!cell.owned)throw std::logic_error("A mixed native body cannot assign its whole endpoint to owned cells");
        const double work=cell_work(cell.before,cell.after,cell.cp);
        remainder.add(-work);weights.push_back(std::abs(work));weight+=std::abs(work);
    }
    std::vector<EnergySum> result(cells.size());
    if(remainder.sign()==0)return result;
    if(!(weight>0) || !std::isfinite(weight))
        throw std::logic_error("A passive native body changed energy without a represented cell contact");
    std::size_t last=weights.size();while(last && weights[last-1]==0)--last;
    const double requested=remainder.value().require_representable();
    for(std::size_t i=0;i+1<last;++i)if(weights[i]>0) {
        const double share=requested*(weights[i]/weight);result[i].add(share);remainder.add(-share);
    }
    if(last)result[last-1]=std::move(remainder);
    return result;
}
// An exact additive expansion retains every represented counterwork term.
// The final capture validates a single ordered chain, never merely a sum of
// endpoint differences which might have been overwritten during the update.
class ClosureLedger {
    struct Path {Cell initial,last;double cp=0;EnergySum correction;};
    std::map<std::int32_t,Path> paths_;
    std::uint64_t ticket_=0;
public:
    void begin(std::uint64_t ticket) {paths_.clear();ticket_=ticket;}
    void record(std::int32_t index,Cell initial,Cell before,Cell after,double cp,const EnergySum& correction) {
        (void)cell_work(before,after,cp);
        if(!initial.valid || initial.element!=before.element || initial.mass!=before.mass)
            throw std::logic_error("Paired native heat has a changed initial projection");
        auto [found,inserted]=paths_.try_emplace(index,Path{initial,before,cp,{}});auto& path=found->second;
        if(!inserted && (path.initial.element!=initial.element || path.initial.mass!=initial.mass ||
            path.initial.temperature!=initial.temperature || path.cp!=cp || path.last.element!=before.element ||
            path.last.mass!=before.mass || path.last.temperature!=before.temperature))
            throw std::logic_error("Paired native heat cell contact chain was overwritten or reordered");
        path.last=after;
        for(std::size_t i=0;i<correction.size();++i)path.correction.add(correction.component(i));
    }
    void unpaired_change(std::int32_t index) {
        const auto found=paths_.find(index);if(found==paths_.end())return;
        if(found->second.correction.sign()!=0)
            throw std::logic_error("An unpaired native mutation changed a cell with pending paired heat");
        paths_.erase(found);
    }
    template<class Read> Completion finish(std::uint64_t ticket,bool same_array,Read read) {
        Completion result;
        if(ticket!=ticket_ || !same_array)throw std::logic_error("Paired native heat lost its owner ticket or native array epoch");
        for(const auto& [index,path]:paths_) {
            if(path.correction.sign()==0)continue;
            const auto final=read(index);
            if(!final.valid || final.element!=path.last.element || final.mass!=path.last.mass || final.temperature!=path.last.temperature)
                throw std::logic_error("Paired native heat endpoint did not survive to its containing capture");
            for(std::size_t i=0;i<path.correction.size();++i) {
                const double joules=path.correction.component(i).require_representable();
                if(joules!=0)result.heat.push_back({index,path.initial.element,path.initial.mass,path.initial.temperature,path.last.temperature,joules});
            }
        }
        paths_.clear();ticket_=0;return result;
    }
};
class Observer {
    std::mutex mutex_;
    std::atomic<bool> active_{false};
    const void* sim_=nullptr;std::array<const void*,2> arrays_{};
    std::array<std::array<const void*,3>,2> storage_{};
    std::size_t count_=0;
    std::uint64_t ticket_=0,ordinal_=0;
    std::uint64_t events_=0;
    std::unordered_map<const void*,std::int32_t> handles_;
    std::vector<Contact> contacts_;
    ClosureLedger closure_;
    std::array<char,256> failure_{};
    void fail(const char* reason) noexcept {if(!failure_[0])std::snprintf(failure_.data(),failure_.size(),"%s",reason);}
    static const char* path() noexcept {static const auto* value=std::getenv("MA_ONI_PAIRED_HEAT_TRACE");return value;}
    static std::FILE* file() noexcept {
        static auto* output=[] {
            auto* f=ma::oni::capture_file(path(),false);
            if(f)std::fputs("ticket\tnative_frame\tcontact\tcell\towned\tarray\thandle\taccessor\trecord\tchunk_valid\tchunk_c_before\tchunk_c_after\tchunk_t_before\tchunk_t_after\treturn\tcp_j_kg_k\told_e_before\told_m_before\told_t_before\tnew_e_before\tnew_m_before\tnew_t_before\told_e_after\told_m_after\told_t_after\tnew_e_after\tnew_m_after\tnew_t_after\tfinal_e\tfinal_m\tfinal_t\tarray_match\torigin_valid\torigin_e\torigin_m\torigin_t\tevent\tcompleted_event\tthread\n",f);
            return f;
        }();return output;
    }
public:
    static bool enabled() noexcept {return path() && *path();}
    static bool correcting() noexcept {static const bool value=[] {const auto* p=std::getenv("MA_ONI_PAIRED_HEAT_CORRECT");return p && std::strcmp(p,"1")==0;}();return value;}
    static bool observing() noexcept {return enabled() || correcting();}
    void begin(const void* sim,std::uint64_t ticket,std::uint64_t ordinal) noexcept {
        if(!observing())return;
        d::Preserve preserve;
        try {
            std::lock_guard lock(mutex_);active_.store(false,std::memory_order_release);
            contacts_.clear();handles_.clear();failure_.fill(0);closure_.begin(ticket);events_=0;sim_=sim;ticket_=ticket;ordinal_=ordinal;
            if(!sim)return;
            const auto pitch=d::read<std::int32_t>(sim,0),rows=d::read<std::int32_t>(sim,4);
            if(pitch<=2 || rows<=2)return;
            count_=static_cast<std::size_t>(pitch)*rows;
            for(unsigned i=0;i<2;++i)arrays_[i]=d::read<const void*>(sim,32+8*i);
            if(!arrays_[0] || !arrays_[1])return;
            for(unsigned i=0;i<2;++i)storage_[i]=storage(arrays_[i]);
#if defined(_WIN32)
            constexpr auto layout=chunk_heat_trace::windows_registry;
#else
            constexpr auto layout=chunk_heat_trace::linux_registry;
#endif
            const auto* mapping=d::read<const std::int32_t*>(sim,layout.mapping);
            const auto* mapping_end=d::read<const std::int32_t*>(sim,layout.mapping+8);
            const auto* generations=d::read<const std::uint8_t*>(sim,layout.generations);
            const auto* generations_end=d::read<const std::uint8_t*>(sim,layout.generations+8);
            const auto n=d::count(mapping,mapping_end,4,1U<<20),g=d::count(generations,generations_end,1,1U<<20);
            const auto* data=d::read<const std::byte*>(sim,layout.data);
            for(std::size_t i=0;i<n && i<g;++i)if(mapping[i]>=0) {
                const auto handle=static_cast<std::int32_t>((std::uint32_t(generations[i])<<24)|i);
                if(chunk_heat_trace::chunk_at(sim,handle,layout).valid)
                    handles_.emplace(data+static_cast<std::size_t>(mapping[i])*44,handle);
            }
            active_.store(true,std::memory_order_release);
        }catch(...){active_.store(false,std::memory_order_release);}
    }
    Contact before(const void* accessor,const void* record,bool owned,double specific_heat,Cell origin={}) noexcept {
        Contact result;
        if(!active_.load(std::memory_order_acquire) || !accessor || !record)return result;
        d::Preserve preserve;
        std::lock_guard lock(mutex_);
        result.accessor=d::read<const void*>(accessor,0);result.record=record;
        result.index=d::read<std::int32_t>(accessor,8);result.owned=owned;result.specific_heat=specific_heat;
        result.array=(result.accessor==arrays_[0]?1U:0U)|(result.accessor==arrays_[1]?2U:0U);
        const auto found=handles_.find(record);
        if(found!=handles_.end())result.handle=found->second;
        for(unsigned i=0;i<2;++i)result.before[i]=cell(arrays_[i],result.index,count_);
        result.origin=origin;
        result.event=++events_;result.thread=thread_identity();
        if(result.handle!=-1)result.chunk_before=chunk_heat_trace::chunk(sim_,result.handle);
        return result;
    }
    void after(Contact result,float returned) noexcept {
        if(!result.record || !active_.load(std::memory_order_acquire))return;
        d::Preserve preserve;
        try {
            std::lock_guard lock(mutex_);
            for(unsigned i=0;i<2;++i)result.after[i]=cell(arrays_[i],result.index,count_);
            if(result.handle!=-1)result.chunk_after=chunk_heat_trace::chunk(sim_,result.handle);
            result.returned=std::bit_cast<std::uint32_t>(returned);
            result.completed_event=++events_;
            // Retain even the failing leaf so a strict admission error never
            // removes the very contact needed to diagnose it.
            if(enabled() && contacts_.size()<(1U<<20))contacts_.push_back(result);
            if(correcting() && result.owned) {
                (void)std::fesetround(FE_TONEAREST);
#if defined(_WIN32)
                constexpr auto layout=chunk_heat_trace::windows_registry;
#else
                constexpr auto layout=chunk_heat_trace::linux_registry;
#endif
                const bool own_cell=result.array==2 && result.thread==thread_identity() && storage(arrays_[0])==storage_[0] && storage(arrays_[1])==storage_[1] &&
                    stable(result.origin,result.before[1],result.after[1],result.specific_heat);
                if(!own_cell) {
                    if(result.registered)throw std::logic_error("Exact storage contact lost its stable projected cell or updated array identity");
                    closure_.unpaired_change(result.index);return;
                }
                EnergySum correction;
                if(result.registered) {
                    if(!result.chunk_before.valid || !result.chunk_after.valid || chunk_record(sim_,result.handle,layout)!=result.record ||
                        result.chunk_before.capacity_bits!=result.chunk_after.capacity_bits || !positive(result.chunk_before.temperature_bits) ||
                        !positive(result.chunk_after.temperature_bits) || result.chunk_before.capacity_bits>=0x7f800000U)
                        throw std::logic_error("Exact storage contact lost its registered chunk endpoint");
                    correction.add(-1000*real(result.chunk_before.capacity_bits)*
                        (real(result.chunk_after.temperature_bits)-real(result.chunk_before.temperature_bits)));
                    correction.add(-cell_work(result.before[1],result.after[1],result.specific_heat));
                }
                closure_.record(result.index,result.origin,result.before[1],result.after[1],result.specific_heat,correction);
            }
        }catch(const std::exception& error){if(correcting()){std::lock_guard lock(mutex_);fail(error.what());}}
         catch(...){if(correcting()){std::lock_guard lock(mutex_);fail("Cannot retain paired native storage heat contact");}}
    }
    Event begin_event() noexcept {
        if(!active_.load(std::memory_order_acquire))return {};
        std::lock_guard lock(mutex_);return {++events_,0,thread_identity()};
    }
    void body(double represented_work,std::span<const BodyCell> cells,bool supported,Event& event) noexcept {
        if(!active_.load(std::memory_order_acquire))return;
        d::Preserve preserve;
        try {
            std::lock_guard lock(mutex_);event.end=++events_;
            if(!correcting())return;
            (void)std::fesetround(FE_TONEAREST);
            if(event.thread!=thread_identity())throw std::logic_error("A native body contact changed executing thread");
            std::vector<EnergySum> corrections;if(supported)corrections=passive_body_corrections(represented_work,cells);
            const EnergySum unpaired;
            for(std::size_t i=0;i<cells.size();++i) {
                const auto& c=cells[i];if(!c.owned)continue;
                if(!stable(c.initial,c.before,c.after,c.cp)){closure_.unpaired_change(c.index);continue;}
                closure_.record(c.index,c.initial,c.before,c.after,c.cp,supported?corrections[i]:unpaired);
            }
        }catch(const std::exception& error){std::lock_guard lock(mutex_);fail(error.what());}
         catch(...){std::lock_guard lock(mutex_);fail("Cannot retain paired passive building heat contacts");}
    }
    Completion finish(const void* sim,std::uint64_t ticket) noexcept {
        Completion completion;
        if(!active_.exchange(false,std::memory_order_acq_rel))return completion;
        d::Preserve preserve;
        std::lock_guard lock(mutex_);
        const auto* final_array=sim?d::read<const void*>(sim,40):nullptr;
        const bool match=sim==sim_ && final_array==arrays_[1] && d::read<const void*>(sim,32)==arrays_[0] &&
            storage(arrays_[0])==storage_[0] && storage(arrays_[1])==storage_[1];
        if(correcting() && !failure_[0])try {
            (void)std::fesetround(FE_TONEAREST);
            completion=closure_.finish(ticket,match,[&](std::int32_t index){return cell(final_array,index,count_);});
        }catch(const std::exception& error){fail(error.what());}
         catch(...){fail("Cannot complete paired native heat capture");}
        completion.failure=failure_;
        if(enabled())if(auto* f=file()) {
            for(std::size_t i=0;i<contacts_.size();++i) {
                const auto& c=contacts_[i];const auto final=cell(final_array,c.index,count_);
                std::fprintf(f,"%llu\t%llu\t%zu\t%d\t%u\t%u\t%d\t%p\t%p\t%u\t%08x\t%08x\t%08x\t%08x\t%08x\t%.17g",
                    static_cast<unsigned long long>(ticket_),static_cast<unsigned long long>(ordinal_),i,c.index,c.owned?1U:0U,c.array,c.handle,c.accessor,c.record,
                    (c.chunk_before.valid?1U:0U)|(c.chunk_after.valid?2U:0U),c.chunk_before.capacity_bits,c.chunk_after.capacity_bits,
                    c.chunk_before.temperature_bits,c.chunk_after.temperature_bits,c.returned,c.specific_heat);
                for(const auto* state:{&c.before[0],&c.before[1],&c.after[0],&c.after[1],&final})
                    std::fprintf(f,"\t%u\t%08x\t%08x",state->element,state->mass,state->temperature);
                std::fprintf(f,"\t%u\t%u\t%u\t%08x\t%08x\t%llu\t%llu\t%llu\n",match?1U:0U,c.origin.valid?1U:0U,c.origin.element,c.origin.mass,c.origin.temperature,
                    static_cast<unsigned long long>(c.event),static_cast<unsigned long long>(c.completed_event),static_cast<unsigned long long>(c.thread));
            }
            (void)std::fflush(f);
        }
        if(enabled() && correcting()) {
            static auto* output=[] {
                const auto name=std::string(path())+".corrections";auto* f=ma::oni::capture_file(name.c_str(),false);
                if(f)std::fputs("ticket\tnative_frame\tcell\telement\tmass_bits\tinitial_t_bits\tfinal_t_bits\tjoules_bits\n",f);
                return f;
            }();
            if(output) {
                for(const auto& h:completion.heat)std::fprintf(output,"%llu\t%llu\t%d\t%u\t%08x\t%08x\t%08x\t%016llx\n",
                    static_cast<unsigned long long>(ticket_),static_cast<unsigned long long>(ordinal_),h.native_cell,h.element,h.mass_bits,
                    h.initial_temperature_bits,h.final_temperature_bits,static_cast<unsigned long long>(std::bit_cast<std::uint64_t>(h.joules)));
                (void)std::fflush(output);
            }
        }
        contacts_.clear();handles_.clear();sim_=nullptr;arrays_={};storage_={};count_=0;ticket_=ordinal_=0;
        return completion;
    }
};

// The two pinned native implementations iterate compacted records in order.
// A shallow invocation view narrows only that iteration; native records,
// handle-indexed result storage and event destinations retain their identities.
// No native object is copied into persistent state or reconstructed as C++.
struct BuildingLayout {
    std::size_t prefix,size;
    std::ptrdiff_t records,handles,results;
    std::size_t result_descriptor;
};
inline constexpr BuildingLayout linux_buildings{0,160,8,32,136,24};
inline constexpr BuildingLayout windows_buildings{160,200,-152,-120,8,32};
template<class T> T relative_read(const void* object,std::ptrdiff_t offset) noexcept {
    T result{};std::memcpy(&result,static_cast<const std::byte*>(object)+offset,sizeof(result));return result;
}
template<class T> void relative_write(void* object,std::ptrdiff_t offset,const T& value) noexcept {
    std::memcpy(static_cast<std::byte*>(object)+offset,&value,sizeof(value));
}
template<class Original,class Before,class After>
bool each_building(void* self,BuildingLayout layout,Original original,Before before,After after) {
    auto* records=relative_read<std::byte*>(self,layout.records);
    auto* end=relative_read<std::byte*>(self,layout.records+8);
    auto* handles=relative_read<std::int32_t*>(self,layout.handles);
    auto* handle_end=relative_read<std::int32_t*>(self,layout.handles+8);
    const auto n=d::count(records,end,44,1U<<20),h=d::count(handles,handle_end,4,1U<<20);
    if(!n || n!=h)return false;
    for(std::size_t i=0;i<n;++i) {
        alignas(16) std::array<std::byte,200> invocation{};
        auto* view=invocation.data()+layout.prefix;
        {
            d::Preserve preserve;
            std::memcpy(invocation.data(),static_cast<std::byte*>(self)-layout.prefix,layout.size);
            relative_write(view,layout.records,records+44*i);relative_write(view,layout.records+8,records+44*(i+1));
            relative_write(view,layout.handles,handles+i);relative_write(view,layout.handles+8,handles+i+1);
            before(records+44*i,handles[i]);
        }
        original(view);
        {
            d::Preserve preserve;
            // Resize belongs to the native implementation and allocator. Move
            // its changed vector descriptor back to its actual owning object.
            std::memcpy(static_cast<std::byte*>(self)+layout.results,view+layout.results,layout.result_descriptor);
            after(records+44*i,handles[i]);
        }
    }
    return true;
}

class BuildingObserver {
    struct Snapshot {std::int32_t index;Cell before,origin;bool owned;double cp;};
    std::vector<Snapshot> cells_;
    std::array<std::uint32_t,11> before_{};
    std::uint64_t ticket_,ordinal_;
    const void* array_=nullptr;std::size_t count_=0;std::int32_t pitch_=0;
    const void* region_;
    Observer* closure_=nullptr;
    Event event_{};
    static const char* path() noexcept {static const auto* value=std::getenv("MA_ONI_PAIRED_BUILDING_HEAT_TRACE");return value;}
    static std::FILE* file() noexcept {
        static auto* output=[] {
            auto* f=ma::oni::capture_file(path(),false);
            if(f)std::fputs("ticket\tnative_frame\tbody_handle\tcell\towned\tbody_t_before\tbody_t_after\tbody_c_per_cell\tbody_kw\tbody_area\tcell_e_before\tcell_m_before\tcell_t_before\tcell_e_after\tcell_m_after\tcell_t_after\tcp_j_kg_k\torigin_valid\torigin_e\torigin_m\torigin_t\tevent\tcompleted_event\tthread\n",f);
            return f;
        }();return output;
    }
public:
    static bool enabled() noexcept {return path() && *path();}
    BuildingObserver(const void* sim,const void* region,std::uint64_t ticket,std::uint64_t ordinal,Observer* closure=nullptr):ticket_(ticket),ordinal_(ordinal),region_(region),closure_(closure) {
        pitch_=d::read<std::int32_t>(sim,0);const auto rows=d::read<std::int32_t>(sim,4);
        if(pitch_>2 && rows>2)count_=static_cast<std::size_t>(pitch_)*rows;
        array_=d::read<const void*>(sim,40);
    }
    template<class Owned,class Capacity,class Projection> void before(const std::byte* record,std::int32_t,Owned owned,Capacity capacity,Projection projection) {
        event_=closure_?closure_->begin_event():Event{};
        cells_.clear();std::memcpy(before_.data(),record,44);
        const auto left=d::read<std::int32_t>(record,28),bottom=d::read<std::int32_t>(record,32),
            right=d::read<std::int32_t>(record,36),top=d::read<std::int32_t>(record,40);
        if(!count_ || left<0 || bottom<0 || right<=left || top<=bottom || right>pitch_ ||
            std::uint64_t(top)*std::uint32_t(pitch_)>count_ || std::uint64_t(right-left)*std::uint32_t(top-bottom)>(1U<<20))return;
        if(left<d::read<std::int32_t>(region_,0) || bottom<d::read<std::int32_t>(region_,4) ||
            right>d::read<std::int32_t>(region_,8) || top>d::read<std::int32_t>(region_,12))return;
        for(auto y=bottom;y<top;++y)for(auto x=left;x<right;++x) {
            const auto index=x+y*pitch_;const auto state=cell(array_,index,count_);
            cells_.push_back({index,state,projection(index),owned(index),capacity(state.element)});
        }
    }
    void after(const std::byte* record,std::int32_t handle) {
        if(closure_) {
            std::vector<BodyCell> contacts;contacts.reserve(cells_.size());bool supported=(before_[2]&0x7fffffffU)==0 && positive(before_[0]) && positive(before_[4]) &&
                d::read<std::uint32_t>(record,16)==before_[4];
            for(const auto& entry:cells_) {
                const auto after=cell(array_,entry.index,count_);
                const auto initial=entry.origin;
                if(!positive(entry.before.mass))continue; // Native does not contact empty cells.
                if(!entry.owned || !(entry.cp>0) || !after.valid || entry.before.element!=after.element || entry.before.mass!=after.mass) supported=false;
                if(!initial.valid || initial.element!=entry.before.element || initial.mass!=entry.before.mass)supported=false;
                contacts.push_back({entry.index,initial,entry.before,after,entry.owned,entry.cp});
            }
            double work=0;
            if(supported && !contacts.empty()) {
                (void)std::fesetround(FE_TONEAREST);
                const auto area=(d::read<std::int32_t>(record,36)-d::read<std::int32_t>(record,28))*
                    (d::read<std::int32_t>(record,40)-d::read<std::int32_t>(record,32));
                // The registered body capacity is the actual binary32
                // area-times-per-cell product used at the native endpoint.
                const float total_capacity=static_cast<float>(area)*std::bit_cast<float>(before_[4]);
                work=static_cast<double>(total_capacity)*1000*
                    (real(d::read<std::uint32_t>(record,0))-real(before_[0]));
            }
            closure_->body(work,contacts,supported,event_);
        }
        if(enabled())if(auto* f=file())for(const auto& entry:cells_) {
            const auto now=cell(array_,entry.index,count_);
            const auto area=(d::read<std::int32_t>(record,36)-d::read<std::int32_t>(record,28))*
                (d::read<std::int32_t>(record,40)-d::read<std::int32_t>(record,32));
            std::fprintf(f,"%llu\t%llu\t%d\t%d\t%u\t%08x\t%08x\t%08x\t%08x\t%d\t%u\t%08x\t%08x\t%u\t%08x\t%08x\t%.17g\t%u\t%u\t%08x\t%08x\t%llu\t%llu\t%llu\n",
                static_cast<unsigned long long>(ticket_),static_cast<unsigned long long>(ordinal_),handle,entry.index,entry.owned?1U:0U,
                before_[0],d::read<std::uint32_t>(record,0),before_[4],before_[2],area,entry.before.element,entry.before.mass,
                entry.before.temperature,now.element,now.mass,now.temperature,entry.cp,entry.origin.valid?1U:0U,entry.origin.element,entry.origin.mass,entry.origin.temperature,
                static_cast<unsigned long long>(event_.begin),static_cast<unsigned long long>(event_.end),static_cast<unsigned long long>(event_.thread));
        }
        if(enabled())if(auto* f=file())(void)std::fflush(f);
    }
};
}
