// Overlapped pacing (docs/colony-owner.md#pacing). Synthetic owner only.
// The game posts frame T while the owner may still compute frame T-1. Readers
// pinned with show_frame see owner frame T-2 at the tick that posts T, with
// that frame's receipts and no later ones, whatever the owner's timing. A save
// barrier's frame holds the readers until later frames catch up, and a load of
// that save shows the same readers. The owner's own trajectory and checkpoint
// bytes equal serial pacing's.
#include <random>

void overlapped_pacing_control() {
    using namespace ma::oni::colony;
    constexpr double dt=static_cast<double>(.2f);
    constexpr std::uint64_t frames=24,barrier=11;
    MAOniColonyConfig cfg{};cfg.struct_size=sizeof(cfg);cfg.abi_version=1;
    cfg.flags=MA_ONI_COLONY_GAME_KERNEL|MA_ONI_COLONY_GAME_SURFACE|MA_ONI_COLONY_GAME_ACTIVITY;
    cfg.vacuum_index=vacuum;cfg.grid_width=cfg.width=7;cfg.grid_height=cfg.height=3;cfg.maximum_timestep_s=static_cast<double>(.1f);
    auto options=default_options();options.maximum_timestep_s=static_cast<double>(.1f);set_options(&options);set_activity_tiers(0,true);
    const std::vector<MAOniColonyMaterial> mats{material("Oxygen",11,oxygen,true,.032,1005),material("CarbonDioxide",12,carbon,true,.044,850)};
    std::vector<MAOniColonyCell> cells(21,MAOniColonyCell{0,MA_ONI_COLONY_SOLID,0,0,0,0});
    for(const std::uint32_t cell:{8u,9u,10u,11u,12u})cells[cell]={oxygen,MA_ONI_COLONY_FLUID,1,300,0,0};
    const std::array<std::uint32_t,1> actor{9};
    // Each tick's inputs: gas into one end, a callback-owned extraction at the other.
    const auto inputs=[&](std::uint64_t tick) {
        const std::array<MAOniColonyTransfer,2> edits{{{8,carbon,MA_ONI_COLONY_INJECT,0,-1,0,.0625,320,0},
            {12,oxygen,MA_ONI_COLONY_EXTRACT,0,static_cast<std::int32_t>(100+tick),0,.03125,0,0}}};
        queue_transfers(edits);
    };
    const auto views=[&] {
        std::vector<MAOniColonyCellView> out(21);MAOniColonyStatus s{};s.struct_size=sizeof(s);s.abi_version=MA_ONI_COLONY_ABI_VERSION;
        copy(s,out.data(),static_cast<std::uint32_t>(out.size()));return out;
    };
    // The readers' frame comes with the views; status() is the owner's latest.
    const auto shown_frame=[&] {
        std::vector<MAOniColonyCellView> out(21);MAOniColonyStatus s{};copy(s,out.data(),static_cast<std::uint32_t>(out.size()));return s;
    };
    const auto same=[](const std::vector<MAOniColonyCellView>& a,const std::vector<MAOniColonyCellView>& b) {
        return a.size()==b.size() && std::memcmp(a.data(),b.data(),a.size()*sizeof(a[0]))==0;
    };
    // Serial pacing: each frame completes before the next is posted; readers follow.
    start(cfg,mats,cells,1);
    std::vector<std::vector<MAOniColonyCellView>> serial{views()};
    for(std::uint64_t tick=1;tick<=frames;++tick) {
        inputs(tick);(void)queue_activity(status().session,dt,actor,{});advance(tick,dt,2);wait();
        require(status().state==1 && status().completed_frames==tick,status().error);serial.push_back(views());
        std::array<MAOniColonyReceipt,8> r{};const auto n=copy_receipts(r.data(),8);
        require(n==1 && r[0].callback==static_cast<std::int32_t>(100+tick) && r[0].completed_frame==tick,"Serial reference receipt differs");
        ack_receipts(r[0].sequence);
    }
    const auto serial_image=checkpoint();stop();
    struct Seen { std::uint64_t frame=0; std::vector<MAOniColonyCellView> views; std::vector<std::int32_t> callbacks; };
    // The game loop under overlapped pacing. timing 0: the owner finishes each
    // frame before the next publication; 1: it may still run; 2: random.
    const auto overlapped=[&](unsigned timing,std::uint64_t first,std::uint64_t save_at,std::vector<std::uint8_t>* saved,std::vector<Seen>& seen) {
        std::mt19937 random(timing*7919u+static_cast<unsigned>(first));
        const auto sid=status().session;std::uint64_t posted=0,shown=0;
        for(std::uint64_t tick=first;tick<=frames;++tick) {
            inputs(tick);const auto sequence=queue_activity(sid,dt,actor,{});shown=posted;posted=sequence;
            // Tick T's publication: native frame T-1 has handed its owner frame.
            const bool found=show_frame(sid,shown);require(found==(tick>first),"A recent handoff was not found");
            Seen s;s.frame=shown_frame().completed_frames;s.views=views();
            {const std::array<std::uint32_t,1> probe{10};std::vector<MAOniColonySampleCell> rows;std::vector<MAOniColonySamplePart> parts;
             MAOniColonyStatus source{};sample(probe,source,rows,parts);
             require(source.completed_frames>=s.frame && status().completed_frames>=source.completed_frames,"The owner's sample or status fell behind the readers");}
            std::array<MAOniColonyReceipt,8> r{};const auto n=copy_receipts(r.data(),8);
            require(n<=1 && copy_receipts(nullptr,0,true)>=n,"Pinned readers received another frame's receipts or the census lost one");
            for(std::uint32_t k=0;k<n;++k){require(r[k].completed_frame<=s.frame,"A receipt arrived before its frame");s.callbacks.push_back(r[k].callback);ack_receipts(r[k].sequence);}
            seen.push_back(std::move(s));
            // Native frame T waits for owner frame T-1, then hands frame T.
            require(awaiting_handoff(sequence),"The posted frame was not awaiting its handoff");
            wait();advance(tick-first+1,dt,2);
            require(!awaiting_handoff(sequence),"A handed frame still holds the game's tick");
            if(timing==0 || (timing==2 && random()%2))wait();
            if(tick==save_at) {
                // Save barrier: readers follow the owner, every receipt is
                // delivered, and the readers stay at the settled frame.
                follow_frames(sid,true);wait();require(shown_frame().completed_frames==tick,"The barrier did not settle the handed frame");
                std::array<MAOniColonyReceipt,8> all{};const auto m=copy_receipts(all.data(),8);
                for(std::uint32_t k=0;k<m;++k)ack_receipts(all[k].sequence);
                follow_frames(sid,false);if(saved)*saved=checkpoint();
            }
        }
        wait();require(status().state==1,status().error);
    };
    for(unsigned timing=0;timing<3;++timing) {
        std::vector<Seen> seen;start(cfg,mats,cells,1);overlapped(timing,1,0,nullptr,seen);
        follow_frames(status().session,true);const auto image=checkpoint();stop();
        require(image==serial_image,"Overlapped pacing changed the owner's trajectory or checkpoint bytes");
        for(std::uint64_t tick=1;tick<=frames;++tick) {
            const auto& s=seen[tick-1];const auto expected=tick>2?tick-2:0;
            require(s.frame==expected && same(s.views,serial[expected]),"Readers did not see owner frame T-2 at tick T");
            require(tick>2?s.callbacks==std::vector<std::int32_t>{static_cast<std::int32_t>(100+expected)}:s.callbacks.empty(),
                "Receipts did not arrive with the frame that made them");
        }
    }
    // A save barrier mid-run, then a load of that save continuing with the same inputs.
    std::vector<std::uint8_t> saved;std::vector<Seen> live,loaded;
    start(cfg,mats,cells,1);overlapped(1,1,barrier,&saved,live);follow_frames(status().session,true);const auto live_image=checkpoint();stop();
    require(live_image==serial_image,"A save barrier changed the owner's trajectory");
    for(std::uint64_t tick=1;tick<=frames;++tick) {
        const auto& s=live[tick-1];const auto expected=tick<=barrier?(tick>2?tick-2:0):std::max(barrier,tick-2);
        require(s.frame==expected && same(s.views,serial[expected]),"Readers moved back after a save barrier or skipped a frame");
        const bool receipt=tick>2 && (tick<=barrier || tick-2>barrier);
        require(receipt?s.callbacks==std::vector<std::int32_t>{static_cast<std::int32_t>(100+tick-2)}:s.callbacks.empty(),"A barrier's receipts arrived twice");
    }
    restore(cfg,mats,saved,1);overlapped(2,barrier+1,0,nullptr,loaded);follow_frames(status().session,true);
    require(checkpoint()==serial_image,"A load of the barrier's save did not continue byte exactly");stop();
    for(std::uint64_t tick=barrier+1;tick<=frames;++tick) {
        const auto& a=live[tick-1];const auto& b=loaded[tick-barrier-1];
        require(a.frame==b.frame && same(a.views,b.views) && a.callbacks==b.callbacks,"A loaded save's readers differ from the uninterrupted game's");
    }
    // Routing: the ownership changes a native frame captured (a solid landing
    // in an owned cell) travel with its handoff.
    start(cfg,mats,cells,1);{
        const auto sid=status().session;const auto first=queue_activity(sid,dt,actor,{});(void)show_frame(sid,0);
        std::array<std::vector<std::uint16_t>,2> element;std::array<std::vector<float>,2> mass,temperature;
        std::array<std::vector<std::uint8_t>,2> disease;std::array<std::vector<std::int32_t>,2> germs;
        NativeCells native{};native.pitch=9;native.rows=5;
        for(unsigned b=0;b<2;++b) {
            element[b].assign(45,1);mass[b].assign(45,1000);temperature[b].assign(45,300);disease[b].assign(45,255);germs[b].assign(45,0);
            for(const std::uint32_t cell:{8u,9u,10u,11u,12u}){const auto n=(cell%7+1)+(cell/7+1)*9;element[b][n]=vacuum;mass[b][n]=0;}
            native.element[b]=element[b].data();native.mass[b]=mass[b].data();native.temperature[b]=temperature[b].data();
            native.disease[b]=disease[b].data();native.disease_count[b]=germs[b].data();
        }
        const auto landed=(11%7+1)+(11/7+1)*9;
        write_shadow(native);for(unsigned b=0;b<2;++b){element[b][landed]=1;mass[b][landed]=100;}
        write_shadow(native);capture_shadow(native,1);advance(1,dt,2);wait();require(status().state==1,status().error);
        std::array<MAOniColonyTerrain,4> edits{};const auto n=handoff_terrain(sid,first,edits.data(),4);
        require(n==1 && edits[0].public_cell==11 && edits[0].kind==MA_ONI_COLONY_CLOSE && handoff_terrain(sid,first+1,nullptr,0)==0,
            "A native frame's ownership change did not travel with its handoff");
        bool stale=false;try{(void)handoff_terrain(sid+1,first,nullptr,0);}catch(const std::invalid_argument&){stale=true;}
        require(stale,"A stale session read another session's handoff");
        // Readers that follow the owner keep no handoffs.
        follow_frames(sid,true);const auto second=queue_activity(sid,dt,actor,{});advance(2,dt,2);wait();
        require(!show_frame(sid,second) && shown_frame().completed_frames==2,"A followed frame left a handoff or the pin did not start at the latest frame");
    }stop();
    // A fault shows at once and never holds the game's tick.
    start(cfg,mats,cells,1);{
        const auto sid=status().session;const auto first=queue_activity(sid,dt,actor,{});(void)show_frame(sid,0);
        advance(1,dt,2);wait();const auto second=queue_activity(sid,dt,actor,{});require(show_frame(sid,first),"First handoff lost");
        require(awaiting_handoff(second) && shown_frame().completed_frames==0 && status().completed_frames==1,"Pinned readers or the tick hold differ before a fault");
        fault_from_adapter("overlapped control fault");
        require(!awaiting_handoff(second) && shown_frame().state==2,"A fault held the game's tick or stayed hidden behind the pin");
    }stop();
    set_options(nullptr);set_activity_tiers(0,true);
    std::cout<<"PASS overlapped pacing: readers at owner frame T-2 with its receipts under every owner timing, serial trajectory bytes, save barrier hold, loaded readers, fault release\n";
}
