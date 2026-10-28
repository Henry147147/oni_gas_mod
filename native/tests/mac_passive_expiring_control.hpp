#pragma once
#include "mac_fixture.hpp"
#include <bit>
#include <cstdlib>
#include <new>
#include <optional>
#include <typeinfo>

// Fault injection is local to this test executable and the calling thread.
// Passive staging and the derived owner allocate before committing any field.
namespace ma::testing::passive_faults {
inline thread_local std::int64_t remaining = -1;
}
void* operator new(std::size_t bytes) {
    auto& left=ma::testing::passive_faults::remaining;
    if(left>=0 && left--==0) { left=-1; throw std::bad_alloc(); }
    if(void* value=std::malloc(bytes?bytes:1))return value;
    throw std::bad_alloc();
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value,std::size_t) noexcept { std::free(value); }
void operator delete[](void* value,std::size_t) noexcept { std::free(value); }

namespace ma::testing::passive_expiring {
using namespace ma::experimental;
inline void require(bool ok,const char* why) { if(!ok)throw std::runtime_error(why); }
inline MacClosedWorld source(bool game,bool partial,bool cached) {
    const auto& f=setup();auto state=f.state(3,2,false);
    state.grid.walls[5]=1;state.cells[5]=MacMaterialCell{MixtureInventory(f.registry.size()),{}};
    state.cells[4]=MacMaterialCell{MixtureInventory(f.registry.size()),{}};
    if(partial)state.cells[1].mixture=f.mixture(.25,100000,300,1);
    MacClosedSettings settings;settings.vacuum=MacVacuumRepresentation::void_phase;
    settings.numerical_kernel=game?MacNumericalKernel::game:MacNumericalKernel::reference;
    settings.game_profile=game?MacGameProfile::game_kernel_carried_momentum:MacGameProfile::legacy;
    settings.game.liquid_law=MacGameLiquidLaw::positive_segments;
    auto world=game?MacClosedWorld::game_colony(f.registry,f.eos,std::move(state),std::vector<double>(6,300),settings):
        MacClosedWorld(f.registry,f.eos,std::move(state),settings);
    const MacFluidTransfer give{0,f.registry.index("oxygen"),true,.001,300};
    world=std::move(world).with_fluid_transfers(std::span(&give,1)).world;
    if(partial)require(!world.state().occupied_volumes_m3.empty() && world.state().occupied_volumes_m3[1]<world.state().grid.volume_m3(),
        "Passive partial-volume fixture was canonicalized to a full cell");
    // Transfers populate the game totals cache; a cold load leaves it absent.
    if(!cached)world=MacClosedWorld::from_checkpoint(f.registry,f.eos,world.checkpoint());
    return world;
}
struct Observation {
    std::vector<std::uint8_t> bytes;
    std::string error;
    std::uint64_t revision=0;
    bool operator==(const Observation&) const=default;
};
inline bool same_totals(const std::optional<MacGameAuditTotals>& a,const std::optional<MacGameAuditTotals>& b) {
    return a.has_value()==b.has_value() && (!a || (a->species_kg==b->species_kg && a->energy_j==b->energy_j));
}
inline Observation apply(std::span<const MacPassiveEdit> edits,bool expiring,bool shared,bool game,bool partial,bool cached) {
    auto world=source(game,partial,cached);const auto before=world.checkpoint();const auto revision=world.revision();const auto time=world.time_s();const auto* address=&world.state();
    const auto before_cache=world.game_audit_totals();require(before_cache.has_value()==(game && cached),"Passive totals fixture missed the requested cache branch");
    std::optional<MacClosedWorld> alias;if(shared)alias.emplace(world);
    std::optional<MacClosedWorld> result;std::string error;
    try { result.emplace(expiring?std::move(world).with_passive_edits(edits):std::as_const(world).with_passive_edits(edits)); }
    catch(const std::exception& e) { error=std::string(typeid(e).name())+":"+e.what(); }
    if(alias)require(alias->checkpoint()==before && alias->revision()==revision && same_totals(alias->game_audit_totals(),before_cache),"Passive edit changed a shared alias");
    if(!result) {
        require(world.checkpoint()==before && world.revision()==revision && &world.state()==address && same_totals(world.game_audit_totals(),before_cache),"Refused passive edit changed its source");
        return {world.checkpoint(),error,world.revision()};
    }
    require(result->revision()==revision+1 && result->time_s()==time,"Passive edit changed its clock/revision contract");
    require(result->game_audit_totals().has_value()==(edits.empty() && before_cache.has_value()),"Passive empty/nonempty totals policy differs");
    if(edits.empty())require(same_totals(result->game_audit_totals(),before_cache),"Empty passive edits changed cached totals");
    if(edits.empty())require(&result->state()==address,"Empty passive edits copied the physical state");
    else if(expiring && !shared && game)require(&result->state()==address,"Unique passive edits copied the physical state");
    else require(&result->state()!=address,"Copied passive edits mutated shared state");
    const auto bytes=result->checkpoint();
    // Exercise subsequent transfer accounting with both retained and absent
    // totals, against a fresh cold owner with identical authoritative bytes.
    const auto& f=setup();const MacFluidTransfer take{0,f.registry.index("oxygen"),false,.0001,0};
    const auto cold=MacClosedWorld::from_checkpoint(f.registry,f.eos,bytes);
    require(result->with_fluid_transfers(std::span(&take,1)).world.checkpoint()==cold.with_fluid_transfers(std::span(&take,1)).world.checkpoint(),
        "Passive edits changed subsequent cached/cold transfer accounting");
    return {bytes,{},result->revision()};
}
inline void controls() {
    const auto nan=std::numeric_limits<double>::quiet_NaN();
    const std::vector<std::vector<MacPassiveEdit>> cases{
        {},{{0,17,1}},{{0,17,-1e6}},{{0,99,-1}},{{0,99,1},{0,99,-1},{0,99,2}},
        {{0,17,0}},{{0,17,-0.0}},{{4,17,1}},{{0,99,2},{0,17,-100},{1,UINT64_C(0x8000000000000001),.125}},
        {{0,99,2},{6,17,1}},{{0,99,2},{5,17,1}},{{0,99,2},{0,0,1}},{{0,99,2},{0,17,nan}},
        {{0,99,2},{0,17,std::numeric_limits<double>::infinity()}},{{0,99,-1},{4,17,-1},{0,17,0}}};
    for(bool game:{false,true})for(bool partial:{false,true})for(bool cached:{false,true}) {
        if(partial && !game)continue;
        for(const auto& edits:cases) {
            const auto reference=apply(edits,false,false,game,partial,cached);
            require(apply(edits,true,false,game,partial,cached)==reference,"Expiring passive bytes/error differ from copying form");
            require(apply(edits,true,true,game,partial,cached)==reference,"Shared expiring passive bytes/error differ from copying form");
        }
    }
    // Existing += behavior accepts an overflowed existing quantity. Preserve
    // it here without asking a checkpoint reader to admit a nonfinite value.
    for(bool expiring:{false,true}) {
        auto world=source(true,false,true);const MacPassiveEdit huge{0,99,std::numeric_limits<double>::max()};
        world=world.with_passive_edits(std::span(&huge,1));
        auto next=expiring?std::move(world).with_passive_edits(std::span(&huge,1)):world.with_passive_edits(std::span(&huge,1));
        const auto& q=next.state().cells[0].passive_quantities;
        const auto it=std::find_if(q.begin(),q.end(),[](const auto& value){return value.identity==99;});
        require(it!=q.end() && std::bit_cast<std::uint64_t>(it->quantity)==UINT64_C(0x7ff0000000000000),"Passive overflow acceptance changed");
    }
    // The game checkpoint header saves model identity, then time/revision.
    // A valid-checksum maximum revision must refuse before empty handling too.
    auto maximum=source(true,false,true).checkpoint();std::uint32_t model_bytes=0;
    for(unsigned i=0;i<4;++i)model_bytes|=std::uint32_t(maximum[12+i])<<(8*i);
    const auto revision_offset=16+static_cast<std::size_t>(model_bytes)+8;
    require(revision_offset+8<maximum.size()-8,"Passive revision fixture has an invalid header");
    std::fill_n(maximum.begin()+static_cast<std::ptrdiff_t>(revision_offset),8,UINT8_MAX);
    std::uint64_t checksum=UINT64_C(14695981039346656037);
    for(std::size_t i=0;i+8<maximum.size();++i)checksum=(checksum^maximum[i])*UINT64_C(1099511628211);
    for(unsigned i=0;i<8;++i)maximum[maximum.size()-8+i]=static_cast<std::uint8_t>(checksum>>(8*i));
    const auto& f=setup();const MacPassiveEdit edit{0,17,1};
    for(bool empty:{false,true}) {
        auto world=MacClosedWorld::from_checkpoint(f.registry,f.eos,maximum);bool refused=false;
        try { (void)std::move(world).with_passive_edits(empty?std::span<const MacPassiveEdit>{}:std::span(&edit,1)); }
        catch(const std::overflow_error& e) { refused=std::string(e.what())=="MAC closed revision overflow"; }
        require(refused && world.checkpoint()==maximum,"Passive revision refusal changed source/error");
    }
    const std::vector<MacPassiveEdit> edits{{0,99,1},{1,98,2},{0,99,-.25},{2,97,3},{1,96,4}};
    const auto expected=apply(edits,false,false,true,true,true);std::size_t failures=0;bool completed=false;
    for(std::int64_t allocation=0;allocation<256;++allocation) {
        auto world=source(true,true,true);const auto before=world.checkpoint();const auto revision=world.revision();const auto* address=&world.state();const auto before_cache=world.game_audit_totals();
        std::optional<MacClosedWorld> result;bool refused=false;passive_faults::remaining=allocation;
        try { result.emplace(std::move(world).with_passive_edits(edits)); }
        catch(const std::bad_alloc&) { refused=true; }
        catch(...) { passive_faults::remaining=-1;throw; }
        passive_faults::remaining=-1;
        if(refused) { ++failures;require(world.checkpoint()==before && world.revision()==revision && &world.state()==address && same_totals(world.game_audit_totals(),before_cache),"Passive allocation refusal changed source"); }
        else { require(result && result->checkpoint()==expected.bytes,"Passive allocation sweep result differs");completed=true;break; }
    }
    require(completed && failures>0,"Passive allocation sweep did not exercise refusals and success");
    std::cout<<"PASS expiring passive edits: ordered/cold bytes, aliases, partial volumes, revisions, validation and allocation rollback\n";
}
} // namespace ma::testing::passive_expiring
