// Synthetic full-owner activity controls. No licensed data or altered gates.
#include <bit>
#include <atomic>
#include <exception>
#include "../src/oni_catalogue.hpp"

void activity_biology_frame_control() {
    using namespace ma::oni::colony;
    constexpr double dt=static_cast<double>(.2f);
    constexpr float inf=std::numeric_limits<float>::infinity();
    constexpr std::uint32_t near_cell=8,remote_cell=10,pristine_cell=12;
    const std::array<std::uint32_t,3> selected{near_cell,remote_cell,pristine_cell};
    const std::array<std::uint32_t,1> actor{near_cell},remote_touch{remote_cell};
    MAOniColonyConfig cfg{};cfg.struct_size=sizeof(cfg);cfg.abi_version=1;
    cfg.flags=MA_ONI_COLONY_GAME_KERNEL|MA_ONI_COLONY_GAME_SURFACE|MA_ONI_COLONY_GAME_ACTIVITY;
    cfg.vacuum_index=vacuum;cfg.grid_width=cfg.width=7;cfg.grid_height=cfg.height=3;cfg.maximum_timestep_s=static_cast<double>(.1f);
    auto options=default_options();options.maximum_timestep_s=static_cast<double>(.1f);
    set_options(&options);set_activity_tiers(0,true);
    const std::vector<MAOniColonyMaterial> mats{material("Oxygen",11,oxygen,true,.032,1005),material("CarbonDioxide",12,carbon,true,.044,850)};
    std::vector<MAOniColonyCell> cells(21,MAOniColonyCell{0,MA_ONI_COLONY_SOLID,0,0,0,0});
    cells[near_cell]={oxygen,MA_ONI_COLONY_FLUID,1,300,1,128};
    cells[remote_cell]={oxygen,MA_ONI_COLONY_FLUID,1,300,2,1<<30};
    cells[pristine_cell]={oxygen,MA_ONI_COLONY_FLUID,1,300,1,128};
    std::array<MAOniColonyDisease,2> diseases{};
    for(auto& disease:diseases) {
        disease.struct_size=sizeof(disease);disease.abi_version=1;disease.strength=1;
        std::fill(std::begin(disease.temperature_range),std::end(disease.temperature_range),inf);
        std::fill(std::begin(disease.temperature_half_lives),std::end(disease.temperature_half_lives),inf);
    }
    std::vector<MAOniColonyDiseaseGrowth> growth(20,MAOniColonyDiseaseGrowth{0,inf,inf,0,0,inf,0,0});
    growth[oxygen].population_half_life=growth[oxygen].over_population_half_life=-.2f;
    growth[10+oxygen].population_half_life=growth[10+oxygen].over_population_half_life=.2f;
    set_diseases(diseases,growth,10);
    const auto summary=[] {MAOniColonyBiology out{};std::vector<MAOniColonyBiologyAccount> accounts;germ_accounts(out,accounts);return out;};
    const auto amount=[&](std::uint32_t cell) {
        MAOniColonyPassiveStamp stamp{};std::vector<MAOniColonyPassiveRow> rows;
        require(read_passives(0,0,selected,stamp,rows),"Activity biology passive observation failed");
        for(const auto& row:rows)if(row.public_cell==cell)return row.quantity;
        return 0.;
    };
    const auto tick=[&](std::uint64_t ticket,std::span<const std::uint32_t> actors,std::span<const std::uint32_t> touched={}) {
        (void)queue_activity(status().session,dt,actors,touched);advance(ticket,dt,2);wait();require(status().state==1,status().error);
    };
    const auto dump=[&](const std::string& name,const auto& image) {
        if(const char* directory=std::getenv("MA_GERM_CLOCK_CONTROL_SAVE_DIR")) {
            std::filesystem::create_directories(directory);std::ofstream file(std::filesystem::path(directory)/name,std::ios::binary);
            file.write(reinterpret_cast<const char*>(image.data()),static_cast<std::streamsize>(image.size()));require(file.good(),"Germ clock evidence save failed");
        }
    };
    // Locate the existing wrapper14 field structurally in this queue-free,
    // two-species fixture. Do not invent a physical checkpoint normalization.
    const auto counter_offset=[&](const auto& image) {
        ma::oni::colony::detail::ParcelWireReader r{image};const auto magic=r.raw(8);
        require(std::string_view(reinterpret_cast<const char*>(magic.data()),magic.size())=="MACOLN01" && r.u32()==14,"Germ clock fixture wrapper changed");
        (void)r.raw(sizeof(MAOniColonyConfig));const auto species=r.u32();require(species==mats.size(),"Germ clock fixture species changed");
        (void)r.raw(species*sizeof(MAOniColonyMaterial));auto count=r.u32();(void)r.raw(count);count=r.u32();(void)r.raw(count);
        require(r.u32()==0,"Germ clock fixture unexpectedly has a reservoir");
        (void)r.raw((6*species+8)*16+16+14*8); // wide ledgers, trace totals, saved status counters
        for(unsigned k=0;k<4;++k)require(r.u32()==0,"Germ clock fixture unexpectedly has queued input");
        (void)r.u64();require(r.u32()==0 && r.u32()==1,"Germ clock fixture job/mode changed");return r.at;
    };
    const auto with_counter=[&](auto image,std::uint64_t value) {
        const auto offset=counter_offset(image);for(unsigned k=0;k<8;++k)image[offset+k]=static_cast<std::uint8_t>(value>>(8*k));
        const auto digest=ma::oni::hash_memory(image.data(),image.size()-64);std::copy(digest.begin(),digest.end(),image.end()-64);return image;
    };
    bool nominal=true,batch=false;unsigned previous_remote_ticks=0;
    std::vector<std::uint8_t> source,at21;std::uint64_t saved_frames=0,frames21=0;
    start(cfg,mats,cells,1);
    for(std::uint64_t frame=1;frame<=28;++frame) {
        tick(frame,actor,frame==1?std::span<const std::uint32_t>(remote_touch):std::span<const std::uint32_t>{});
        const auto b=summary();nominal=nominal && b.frames==frame;
        require(b.mode==1 && amount(near_cell)==std::ldexp(128.,static_cast<int>(frame)) && amount(pristine_cell)==128,
            "Nominal biology counter changed near growth or untouched culture");
        const double remote=amount(remote_cell);unsigned ticks=0;
        while(ticks<=30 && std::ldexp(1.,30-static_cast<int>(ticks))!=remote)++ticks;
        require(ticks<=30 && ticks>=previous_remote_ticks,"Remote decay changed its exact half-life");
        batch=batch || ticks>previous_remote_ticks+1;previous_remote_ticks=ticks;
        require(b.born==amount(near_cell)-128 && b.died==std::ldexp(1.,30)-remote && b.culled==0 && b.moved==0 && b.audit_error==0,
            "Activity growth/decay accounts changed");
        const auto image=checkpoint();dump("frame-"+std::to_string(frame)+".owner",image);
        if(frame==17){source=image;saved_frames=b.frames;}
        if(frame==21){at21=image;frames21=b.frames;}
    }
    require(batch,"Clock regression fixture never exercised a multi-tick remote batch");
    const auto continued=checkpoint();const auto continued_frames=summary().frames;stop();
    restore(cfg,mats,source,1);require(checkpoint()==source && summary().frames==saved_frames,"Cold load rewrote saved biology clock");
    const auto zero=checkpoint();const auto zero_time=status().time_seconds;(void)flush_inputs();
    require(checkpoint()==zero && summary().frames==saved_frames && status().time_seconds==zero_time,"Zero-time flush advanced biology clock");
    for(std::uint64_t frame=1;frame<=11;++frame)tick(frame,actor);
    nominal=nominal && summary().frames-saved_frames==11;
    require(checkpoint()==continued && summary().frames==continued_frames,"Remote biology cold continuation differs");stop();
    // Older saves may contain an inflated absolute diagnostic. Keep that exact
    // baseline; all future deltas use nominal frames, never infer a migration.
    auto legacy=with_counter(source,saved_frames+1000);restore(cfg,mats,legacy,1);
    require(checkpoint()==legacy && summary().frames==saved_frames+1000,"Restore silently migrated an old biology counter");
    for(std::uint64_t frame=1;frame<=4;++frame)tick(frame,actor);
    nominal=nominal && summary().frames-(saved_frames+1000)==4;
    require(checkpoint()==with_counter(at21,frames21+1000),"Saved diagnostic baseline changed physical/culture continuation");
    const auto before=summary().frames;const auto time=status().time_seconds;const auto near=amount(near_cell);
    const MAOniColonyGermEdit edit{pristine_cell,1,1};queue_germs(std::span(&edit,1));(void)flush_inputs();
    require(summary().frames==before && status().time_seconds==time && amount(near_cell)==near && amount(pristine_cell)==129,
        "Zero-time germ edit advanced growth or its diagnostic clock");stop();
    start(cfg,mats,cells,1);for(std::uint64_t frame=1;frame<=4;++frame)tick(frame,{});
    nominal=nominal && summary().frames==4;
    require(amount(near_cell)==128 && amount(remote_cell)==std::ldexp(1.,30) && amount(pristine_cell)==128,"Pristine biology advanced");
    dump("pristine4.owner",checkpoint());stop();
    auto legacy_cfg=cfg;legacy_cfg.flags&=~MA_ONI_COLONY_GAME_ACTIVITY;start(legacy_cfg,mats,cells,1);
    for(std::uint64_t frame=1;frame<=3;++frame){advance(frame,dt,2);wait();require(status().state==1,status().error);}
    require(summary().frames==3 && amount(near_cell)==1024 && amount(remote_cell)==std::ldexp(1.,27),"Nonadaptive biology clock/arithmetic changed");stop();
    set_diseases({}, {},0);set_options(nullptr);set_activity_tiers(0,true);
    require(nominal,"Activity biology frames count remote batch ticks instead of nominal native frames");
    std::cout<<"PASS activity biology clock: nominal frames, exact near growth/remote decay, pristine hold, zero-time edits, saved baseline and cold deltas\n";
}

void activity_owner_control() {
    using namespace ma::oni::colony;
    constexpr double dt=static_cast<double>(.2f);
    const auto export_image=[&](const char* name,const auto& image) {
        if(const char* directory=std::getenv("MA_ACTIVITY_CONTROL_SAVE_DIR")) {
            std::filesystem::create_directories(directory);std::ofstream file(std::filesystem::path(directory)/name,std::ios::binary);
            file.write(reinterpret_cast<const char*>(image.data()),static_cast<std::streamsize>(image.size()));require(file.good(),"Activity evidence save failed");
        }
    };
    const auto rejects=[&](auto&& f){bool refused=false;try{f();}catch(const std::invalid_argument&){refused=true;}require(refused,"Invalid activity operation accepted");};
    MAOniColonyConfig cfg{};cfg.struct_size=sizeof(cfg);cfg.abi_version=1;
    cfg.flags=MA_ONI_COLONY_GAME_KERNEL|MA_ONI_COLONY_GAME_SURFACE|MA_ONI_COLONY_GAME_ACTIVITY;
    cfg.vacuum_index=vacuum;cfg.grid_width=cfg.width=7;cfg.grid_height=cfg.height=3;cfg.maximum_timestep_s=.1;
    auto options=default_options();options.maximum_timestep_s=static_cast<double>(.1f);options.trace_sink_threshold_kg=1e-6;set_options(&options);set_activity_tiers(0,true);
    std::vector<MAOniColonyMaterial> mats{material("Oxygen",11,oxygen,true,.032,1005),material("CarbonDioxide",12,carbon,true,.044,850)};
    std::vector<MAOniColonyCell> cells(21,MAOniColonyCell{0,MA_ONI_COLONY_SOLID,0,0,0,0});
    const std::array<std::uint32_t,3> selection{8,10,12};
    for(const auto cell:selection)cells[cell]={oxygen,MA_ONI_COLONY_FLUID,1,300,0,0};
    const auto sample=[&] {
        std::array<MAOniColonyActivityCell,3> rows{};MAOniColonyStatus source{};
        require(sample_activity(0,0,selection,source,rows),"Activity sample failed");return rows;
    };
    const auto held=[&](std::uint32_t cell,std::uint32_t species=oxygen) {
        std::uint32_t indices[4]{};double masses[4]{};const auto n=composition(cell,indices,masses,4);
        for(std::uint32_t k=0;k<n;++k)if(indices[k]==species)return masses[k];
        return 0.;
    };
    const auto tick=[&](std::uint64_t ticket,std::span<const std::uint32_t> actors={},std::span<const std::uint32_t> touched={}) {
        (void)queue_activity(status().session,dt,actors,touched);advance(ticket,dt,2);wait();
        require(status().state==1,status().error);
    };
    start(cfg,mats,cells,1);
    require(checkpoint()[8]==14,"Integrated activity owner did not use wrapper14");export_image("native-wrapper14.owner",checkpoint());
    const auto pinned=reading_snapshot();const auto pinned_readings=std::vector(pinned.readings.begin(),pinned.readings.end());
    const auto sid=status().session;
    rejects([&]{(void)queue_activity(sid,.2,{},{});}); // .2 differs from the native float timestep.
    rejects([&]{(void)queue_activity(sid+1,dt,{},{});});
    tick(1);require(sample()[0].last_activity_s==-1 && held(8)==1,"Pristine region advanced");
    // Snapshot cuts isolate actor inputs even when two complete managed frames precede binding.
    const MAOniColonyTransfer first{8,oxygen,MA_ONI_COLONY_INJECT,0,-1,0,.125,300,0};
    const MAOniColonyTransfer second{8,oxygen,MA_ONI_COLONY_INJECT,0,-1,0,.25,300,0};
    queue_transfers(std::span(&first,1));require(queue_activity(sid,dt,{},{})==2,"First sequence lost");
    queue_transfers(std::span(&second,1));require(queue_activity(sid,dt,{},{})==3,"Second sequence lost");
    (void)queue_activity(sid,0,std::span(selection.data()+1,1),std::span(selection.data()+2,1));
    (void)queue_activity(sid,0,std::span(selection.data()+1,1),{});
    require(flush_inputs()==0 && held(8)==1 && sample()[1].actors==0,"Zero refresh consumed/reordered a positive snapshot");
    const auto queued=checkpoint();export_image("native-wrapper14-pending.owner",queued);
    advance(2,dt,2);wait();require(status().state==1 && held(8)==1.125,"Frame1 consumed frame2 inputs");
    advance(3,dt,2);wait();require(status().state==1 && held(8)==1.375 && status().completed_frames==3,"FIFO coalesced/dropped a frame");
    require(flush_inputs()==0 && sample()[1].actors==1 && sample()[2].last_activity_s>=0,"Lifecycle census lost actor state or touch union");
    const auto continued=checkpoint();stop();
    require(pinned.readings.size()==pinned_readings.size() && std::memcmp(pinned.readings.data(),pinned_readings.data(),pinned_readings.size()*sizeof(pinned_readings[0]))==0,
        "Pinned reading snapshot expired at shutdown");
    restore(cfg,mats,queued,1);require(checkpoint()==queued,"Pending activity cold encoding differs");
    MAOniColonyActivityState state{};require(copy_activity(state) && state.pending_frames==2 && state.pending_head_sequence==2,"Saved FIFO not exposed for resume");
    rejects([&]{claim_activity_frame(status().session,2,.2);});
    claim_activity_frame(status().session,2,dt);require(copy_activity(state) && state.pending_head_sequence==3,"First claim did not expose second packet");
    claim_activity_frame(status().session,3,dt);require(copy_activity(state) && state.pending_frames==0,"Second claim lost");
    advance(1,dt,2);advance(2,dt,2);wait();require(status().state==1 && held(8)==1.375 && status().completed_frames==3,"Resumed input FIFO duplicated actor input");
    require(flush_inputs()==0 && checkpoint()==continued,"Saved actor census/input cuts did not continue byte exactly");
    // Policy + changed census used to invalidate a non-owning activity span.
    set_activity_tiers(status().session,false);(void)queue_activity(status().session,0,std::span(selection.data(),1),{});
    require(flush_inputs()==0 && copy_activity(state) && state.enabled==0 && sample()[0].actors==1,"Zero-time policy/census transaction failed");
    set_activity_tiers(status().session,true);tick(3,std::span(selection.data()+2,1));
    require(copy_activity(state) && state.enabled==1 && sample()[2].actors==1,"Positive policy/census transaction failed");
    stop();
    // Callback wrapper13 can carry integrated14; releasing an absent callback remaps later activity cuts.
    start(cfg,mats,cells,1);
    const MAOniColonyTransfer callback{8,oxygen,MA_ONI_COLONY_EXTRACT,0,41,0,.25,0,0};queue_transfers(std::span(&callback,1));
    queue_transfers(std::span(&first,1));(void)queue_activity(status().session,dt,{},{});
    queue_transfers(std::span(&second,1));(void)queue_activity(status().session,dt,{},{});
    const auto wrapped=checkpoint();require(wrapped[8]==13 && wrapped[12]==14,"Callback wrapper did not carry base14");
    export_image("native-wrapper13-base14.owner",wrapped);stop();
    auto unsafe=wrapped;unsafe[12]=12;const auto digest=ma::oni::hash_memory(unsafe.data(),unsafe.size()-64);
    std::copy(digest.begin(),digest.end(),unsafe.end()-64);rejects([&]{restore(cfg,mats,unsafe,1);});
    restore(cfg,mats,wrapped,1);claim_activity_frame(status().session,1,dt);claim_activity_frame(status().session,2,dt);
    advance(1,dt,2);advance(2,dt,2);wait();
    require(status().state==1 && held(8)==1.375 && callback_rebind_state().second==1,"Released callback corrupted activity cuts");stop();
    // An unrelated zero-time lifecycle flush cannot consume a restored,
    // unclaimed positive packet or release its callback-owned extraction.
    const MAOniColonyTransfer protected_callback{8,oxygen,MA_ONI_COLONY_EXTRACT,0,77,0,.25,0,0};
    const MAOniColonyHeat unrelated_heat{10,1,10,1000};
    const auto late_lifecycle=[&] {
        queue_heat(std::span(&unrelated_heat,1));
        (void)queue_activity(status().session,0,std::span(selection.data()+1,1),std::span(selection.data()+2,1));
    };
    const auto delivery=[&](int callback_id) {
        MAOniColonyReceipt receipt{};require(copy_receipts(&receipt,1)==1 && receipt.callback==callback_id && receipt.mass_kg==.25f,
            "Restored callback was not delivered exactly once to its explicit rebound actor");
        ack_receipts(receipt.sequence);require(copy_receipts(nullptr,0)==0,"Acknowledged callback remained deliverable");
    };
    start(cfg,mats,cells,1);queue_transfers(std::span(&protected_callback,1));
    (void)queue_activity(status().session,dt,std::span(selection.data(),1),{});
    const auto saved_protected=checkpoint();require(saved_protected[8]==13 && saved_protected[12]==14,"Protected callback save lost wrapper13/base14");
    late_lifecycle();require(flush_inputs()==0 && held(8)==1 && status().completed_frames==0,"Live reference flush consumed protected callback input");
    const auto flushed_reference=checkpoint();advance(1,dt,2);wait();require(status().state==1,status().error);delivery(77);
    const auto delivered_reference=checkpoint();tick(2);require(copy_receipts(nullptr,0)==0,"Live callback reference repeated delivery");
    const auto after_delivery_reference=checkpoint();stop();
    restore(cfg,mats,saved_protected,1);require(callback_rebind_state()==std::pair<std::uint32_t,std::uint64_t>{1,0},"Restored protected callback did not await binding");
    late_lifecycle();require(flush_inputs()==0 && status().state==1 && status().completed_frames==0 && status().time_seconds==0 && held(8)==1 &&
        callback_rebind_state()==std::pair<std::uint32_t,std::uint64_t>{1,0} && copy_receipts(nullptr,0)==0,
        "Zero-time tail heat/census released or delivered a protected restored callback");
    require(copy_activity(state) && state.pending_frames==1 && state.pending_head_sequence==1 && checkpoint()==flushed_reference,
        "Zero-time restore flush changed protected packet/input/checkpoint bytes");
    require(flush_inputs()==0 && checkpoint()==flushed_reference && callback_rebind_state().first==1,
        "Repeated zero-time restore flush consumed protected callback custody");
    const MAOniColonyCallbackRebind bind_protected{77,1};rebind_callbacks(std::span(&bind_protected,1));
    claim_activity_frame(status().session,1,dt);advance(1,dt,2);wait();require(status().state==1,status().error);delivery(77);
    require(checkpoint()==delivered_reference,"Explicit rebound/claimed callback continuation differs");
    tick(2);require(copy_receipts(nullptr,0)==0 && checkpoint()==after_delivery_reference,"Rebound callback repeated or changed later continuation");stop();
    // A restored callback outside any captured packet must first be rebound;
    // never execute its saved ordinal as a live callback during a zero flush.
    const MAOniColonyTransfer tail_callback{10,oxygen,MA_ONI_COLONY_EXTRACT,0,88,0,.25,0,0};
    start(cfg,mats,cells,1);queue_transfers(std::span(&protected_callback,1));(void)queue_activity(status().session,dt,{},{});
    queue_transfers(std::span(&tail_callback,1));const auto saved_tail=checkpoint();stop();restore(cfg,mats,saved_tail,1);
    rejects([&]{(void)flush_inputs();});
    require(status().state==1 && checkpoint()==saved_tail && callback_rebind_state()==std::pair<std::uint32_t,std::uint64_t>{2,0} && copy_receipts(nullptr,0)==0,
        "Refused unresolved callback tail changed owner health, inputs or callback custody");
    const std::array<MAOniColonyCallbackRebind,2> bind_tail{{{77,1},{88,1}}};rebind_callbacks(bind_tail);
    require(flush_inputs()==0 && held(8)==1 && held(10)==.75,"Rebound callback tail flush consumed protected packet");delivery(88);
    claim_activity_frame(status().session,1,dt);advance(1,dt,2);wait();require(status().state==1,status().error);delivery(77);stop();
    // A lifecycle command already dispatched may overlap a new positive submission; neither work item is consumed by the other.
    start(cfg,mats,cells,1);(void)queue_activity(status().session,dt,{},{});
    std::vector<MAOniColonyHeat> command_heat(100000,MAOniColonyHeat{10,1,.01,1000});queue_heat(command_heat);
    std::atomic<bool> finished{false};std::exception_ptr command_error;
    std::thread command([&]{try{(void)flush_inputs();}catch(...){command_error=std::current_exception();}finished.store(true);});
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!busy() && !finished.load() && std::chrono::steady_clock::now()<deadline)std::this_thread::yield();
    const bool command_published=busy() || finished.load();
    advance(1,dt,2);command.join();wait();if(command_error)std::rethrow_exception(command_error);
    require(command_published && status().state==1 && status().completed_frames==1,"Lifecycle command dropped the overlapping activity packet");stop();
    // Real queued terrain visibility must not leak a later packet into the preceding native shadow.
    start(cfg,mats,cells,1);(void)queue_activity(status().session,dt,{},{});
    const MAOniColonyTerrain close{8,MA_ONI_COLONY_CLOSE};queue_terrain(std::span(&close,1));(void)queue_activity(status().session,dt,{},{});
    std::array<std::vector<std::uint16_t>,2> element;std::array<std::vector<float>,2> mass,temperature;
    NativeCells native{};native.pitch=9;native.rows=5;
    for(unsigned b=0;b<2;++b){element[b].assign(45,1);mass[b].assign(45,1000);temperature[b].assign(45,300);
        for(const auto cell:selection){const auto n=(cell%7+1)+(cell/7+1)*9;element[b][n]=vacuum;mass[b][n]=0;}
        native.element[b]=element[b].data();native.mass[b]=mass[b].data();native.temperature[b]=temperature[b].data();}
    write_shadow(native);require(element[0][20]==oxygen && mass[0][20]==1,"Future close changed the preceding native shadow");
    capture_shadow(native,1);advance(1,dt,2);wait();require(status().state==1 && held(8)==1,"First native frame adopted future close");
    write_shadow(native);require(!owns(20),"Bound close did not leave the native cell to ONI");
    element[0][20]=element[1][20]=1;mass[0][20]=mass[1][20]=1000;
    capture_shadow(native,2);advance(2,dt,2);wait();require(status().state==1 && held(8)==0,"Second packet terrain not applied");stop();
    // Every post-flow stage shares the same due cells; pristine conversions, traces and germs stay held.
    mats[0].high_target=12;mats[0].high_temperature_k=350;
    cells[8].temperature_k=400;cells[10].disease=1;cells[10].disease_count=1000;cells[12].mass_kg=1e-7f;
    constexpr float inf=std::numeric_limits<float>::infinity();MAOniColonyDisease disease{};
    disease.struct_size=sizeof(disease);disease.abi_version=1;disease.strength=1;
    std::fill(std::begin(disease.temperature_range),std::end(disease.temperature_range),inf);
    std::fill(std::begin(disease.temperature_half_lives),std::end(disease.temperature_half_lives),inf);
    std::vector<MAOniColonyDiseaseGrowth> growth(10,MAOniColonyDiseaseGrowth{0,inf,inf,0,0,inf,0,0});
    growth[oxygen].population_half_life=.2f;growth[oxygen].over_population_half_life=.2f;
    set_diseases(std::span(&disease,1),growth,10);
    start(cfg,mats,cells,1);const auto trace=held(12);
    const auto germs=[&]{MAOniColonyPassiveStamp stamp{};std::vector<MAOniColonyPassiveRow> rows;
        require(read_passives(0,0,std::span(selection.data()+1,1),stamp,rows),"Germ sample failed");return rows.empty()?0.:rows.front().quantity;};
    tick(1);tick(2);require(held(8,carbon)==0 && held(12)==trace && germs()==1000 && status().trace_sink_kg==0,"Paused owner post-stage evolved");
    const MAOniColonyHeat passive{10,1,10,1000};queue_heat(std::span(&passive,1));tick(3);
    require(sample()[1].last_activity_s==-1 && germs()==1000,"Passive solid heat woke biology/flow");
    wake_native_cell(22);tick(4);require(sample()[1].last_activity_s>=0 && germs()==500,"Actual body/contact heat failed to wake biology");
    tick(5,std::span(selection.data(),1));require(held(8,carbon)>0 && held(12)==trace,"Conversion selection lost activity or woke a distant trace");
    tick(6,{},std::span(selection.data()+2,1));require(held(12)==0 && status().trace_sink_kg>0,"Touched trace cell did not sink");
    // Leave the disturbed second region remote, then hold the pending fraction across a cold load.
    for(std::uint64_t frame=7;frame<=19;++frame)tick(frame);
    require(sample()[1].next_remote_s>0,"Region did not enter remote tier");
    const auto remote=checkpoint();for(std::uint64_t frame=20;frame<=27;++frame)tick(frame);
    const auto remote_continued=checkpoint();stop();restore(cfg,mats,remote,1);
    for(std::uint64_t frame=1;frame<=8;++frame)tick(frame);
    require(checkpoint()==remote_continued,"Remote phase/biology elapsed cold continuation differs");
    require(status().maximum_relative_mass_error<1e-12 && status().maximum_relative_energy_error<1e-10,"Activity owner conservation failed");stop();set_diseases({}, {},0);
    // Once remote time reaches the idle deadline, no stage catches that elapsed time up on wake.
    start(cfg,mats,cells,1);tick(1,{},std::span(selection.data()+1,1));
    for(std::uint64_t frame=2;frame<=306;++frame)tick(frame);
    const auto dormant=sample()[1];require(dormant.next_remote_s==0 && dormant.pending_s==0,"Long-idle region did not pause");
    const auto old_time=status().time_seconds;tick(307,{},std::span(selection.data()+1,1));
    require(status().time_seconds==old_time+dt && sample()[1].last_activity_s==old_time && sample()[1].pending_s==0,"Wake caught up idle time");stop();
    // A space cell receives real input immediately, sinks only on its selected stage, and books all mass.
    start(cfg,mats,cells,1);set_space_cells(std::span(selection.data()+1,1),true);
    tick(1);require(held(10)==0 && held(8)==1 && status().space_sink_kg==1,"Selective space sink touched a held region");stop();
    // Always simulate retains all authoritative profile8 physical bytes after normalization of profile metadata.
    std::vector<ma::oni::GameFluidElement> elements;
    for(const auto& m:mats) {
        ma::oni::GameFluidElement e;e.id=m.id;e.hash=m.element_hash;e.native_index=m.native_index;e.gas=m.phase==1;
        e.molar_mass_kg_per_mol=m.molar_mass_kg_per_mol;e.specific_heat_j_per_kg_k=m.specific_heat_j_per_kg_k;
        e.conductivity_w_per_m_k=m.conductivity_w_per_m_k;e.density_kg_per_m3=m.density_kg_per_m3;
        e.low_temperature_k=m.low_temperature_k;e.high_temperature_k=m.high_temperature_k;e.low_target=m.low_target;e.high_target=m.high_target;
        elements.push_back(std::move(e));
    }
    const auto cat=ma::oni::build_catalogue(std::move(elements));
    const auto normalized=[&](const auto& owner) {
        constexpr std::string_view magic="MACCLS01";const auto at=std::search(owner.begin(),owner.end(),magic.begin(),magic.end());
        require(at!=owner.end(),"Owner lacks physical checkpoint");
        const auto bytes=std::span(owner).subspan(static_cast<std::size_t>(at-owner.begin()),static_cast<std::size_t>(owner.end()-64-at));
        auto world=ma::experimental::MacClosedWorld::from_checkpoint(cat.registry,cat.eos,bytes);auto settings=world.settings();
        settings.game_profile=ma::experimental::MacGameProfile::game_kernel_carried_momentum;settings.game.activity_tiers=false;
        return std::pair{std::bit_cast<std::uint64_t>(world.time_s()),ma::experimental::MacClosedWorld(cat.registry,cat.eos,world.state(),settings).checkpoint()};
    };
    const auto trajectory=[&](bool activity,unsigned workers) {
        auto config=cfg;if(!activity)config.flags&=~MA_ONI_COLONY_GAME_ACTIVITY;
        set_worker_count(workers);set_activity_tiers(0,false);start(config,mats,cells,1);
        for(std::uint64_t frame=1;frame<=4;++frame) {
            if(frame==1){refuse_heat(10);const std::array<MAOniColonyHeat,2> deferred{{{10,activity?1u:0u,.1,1000},{10,0,.2,1000}}};queue_heat(deferred);}
            const MAOniColonyTransfer transfer{10,carbon,MA_ONI_COLONY_INJECT,0,-1,0,.03125,310,0};queue_transfers(std::span(&transfer,1));
            if(activity)(void)queue_activity(status().session,dt,{},{});
            advance(frame,dt,2);wait();require(status().state==1,status().error);if(frame==1)refuse_heat(-1);
        }
        const auto output=normalized(checkpoint());stop();return output;
    };
    const auto legacy=trajectory(false,1);require(trajectory(true,1)==legacy && trajectory(true,4)==legacy,"Always simulate changed physical profile8 bytes or worker determinism");
    set_worker_count(0);set_activity_tiers(0,true);
    // A full contact queue uses a conservative saved wake-all fallback rather than losing real contact wakes.
    start(cfg,mats,cells,1);for(std::size_t k=0;k<(1u<<20)+1;++k)wake_native_cell(22);
    const auto overflow=checkpoint();stop();restore(cfg,mats,overflow,1);tick(1);
    require(sample()[0].last_activity_s>=0 && sample()[2].last_activity_s>=0,"Contact overflow lost a wake across restore");stop();
    // Missing or differently bound input is refused, never silently coalesced.
    start(cfg,mats,cells,1);advance(1,dt,2);wait();
    require(status().state==2 && std::string(status().error).find("claimed census")!=std::string::npos,"Missing activity packet accepted or its cause hidden");stop();
    // The native protocol may deliver another frame after managed startup has
    // failed. Preserve that first fault instead of replacing it with a missing
    // census error or consuming the saved/queued frame.
    start(cfg,mats,cells,1);(void)queue_activity(status().session,dt,{},{});
    fault_from_adapter("activity census setup failed");const auto first_fault=std::string(status().error);
    advance(1,dt,2);wait();
    require(status().state==2 && status().completed_frames==0 && std::string(status().error)==first_fault,"A later native callback replaced the originating adapter fault");stop();
    set_options(nullptr);set_activity_tiers(0,true);
    activity_biology_frame_control();
    std::cout<<"PASS owner activity: pristine hold, isolated FIFO cuts, two claims/cold continuation, lifecycle touch union, policies, terrain visibility, passive/contact heat, selective biology/conversion/trace, remote clocks, pinned readings, fail closed\n";
}
