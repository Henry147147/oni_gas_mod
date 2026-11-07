// Terrain edit order and open-cell custody (docs/colony-owner.md#terrain).
// Synthetic owner only. ONI applies a frame's opens and closes in order: the
// game's edits as it sent them, then what the native frame observed. The owner
// must end where ONI does, or an open cell becomes a wall the owner never
// fills and native physics, suppressed beside owned cells, never fills either
// (a vacuum hole in a breathable room).

void terrain_order_control() {
    using namespace ma::oni::colony;
    constexpr double dt=static_cast<double>(.2f);
    constexpr std::uint32_t solid=1,width=7,height=3,pitch=width+2,rows=height+2;
    const auto nat=[](std::uint32_t cell){return static_cast<int>((cell%width+1)+(cell/width+1)*pitch);};
    for(const bool activity:{true,false}) {
        MAOniColonyConfig cfg{};cfg.struct_size=sizeof(cfg);cfg.abi_version=1;
        cfg.flags=MA_ONI_COLONY_GAME_KERNEL|MA_ONI_COLONY_GAME_SURFACE|(activity?MA_ONI_COLONY_GAME_ACTIVITY:0u);
        cfg.vacuum_index=vacuum;cfg.grid_width=cfg.width=width;cfg.grid_height=cfg.height=height;cfg.maximum_timestep_s=static_cast<double>(.1f);
        auto options=default_options();options.maximum_timestep_s=static_cast<double>(.1f);set_options(&options);set_activity_tiers(0,true);
        const std::vector<MAOniColonyMaterial> mats{material("Oxygen",11,oxygen,true,.032,1005),material("CarbonDioxide",12,carbon,true,.044,850)};
        // A room of five cells (8..12) in solid; cell 13 is a wall ONI holds as
        // vacuum from the start (a hole outside every world): it stays closed.
        std::vector<MAOniColonyCell> cells(width*height,MAOniColonyCell{0,MA_ONI_COLONY_SOLID,0,0,0,0});
        for(const std::uint32_t cell:{8u,9u,10u,11u,12u})cells[cell]={oxygen,MA_ONI_COLONY_FLUID,1,300,0,0};
        std::array<std::vector<std::uint16_t>,2> element;std::array<std::vector<float>,2> mass,temperature;
        std::array<std::vector<std::uint8_t>,2> disease;std::array<std::vector<std::int32_t>,2> germs;
        NativeCells native{};native.pitch=pitch;native.rows=rows;
        for(unsigned b=0;b<2;++b) {
            element[b].assign(pitch*rows,solid);mass[b].assign(pitch*rows,1000);temperature[b].assign(pitch*rows,300);
            disease[b].assign(pitch*rows,255);germs[b].assign(pitch*rows,0);
            for(const std::uint32_t cell:{8u,9u,10u,11u,12u,13u}){element[b][nat(cell)]=vacuum;mass[b][nat(cell)]=0;}
            native.element[b]=element[b].data();native.mass[b]=mass[b].data();native.temperature[b]=temperature[b].data();
            native.disease[b]=disease[b].data();native.disease_count[b]=germs[b].data();
        }
        // What ONI holds in a cell: `buffers` 3 both (its messages, before the
        // shadow), 2 only the frame's result (native physics after the shadow).
        const auto set=[&](std::uint32_t cell,std::uint16_t e,float m,unsigned buffers) {
            for(unsigned b=0;b<2;++b)if(buffers&(1u<<b)){element[b][nat(cell)]=e;mass[b][nat(cell)]=m;temperature[b][nat(cell)]=e==vacuum?0.f:300.f;}
        };
        std::uint64_t ticket=0;
        // One native frame: messages, shadow, native physics, capture, handoff.
        // ONI's next frame starts from the previous frame's result.
        const auto frame=[&](const std::function<void()>& messages,const std::function<void()>& physics) {
            std::copy(element[1].begin(),element[1].end(),element[0].begin());std::copy(mass[1].begin(),mass[1].end(),mass[0].begin());
            std::copy(temperature[1].begin(),temperature[1].end(),temperature[0].begin());
            ++ticket;if(activity)(void)queue_activity(status().session,dt,{},{});
            messages();write_shadow(native);physics();capture_shadow(native,ticket);advance(ticket,dt,2);wait();
            require(status().state==1,status().error);
        };
        const auto none=[]{};
        const auto edit=[&](std::initializer_list<MAOniColonyTerrain> edits){queue_terrain(std::span(edits.begin(),edits.size()));};
        const auto oxygen_held=[&] {
            double total=0;
            for(std::uint32_t cell=0;cell<width*height;++cell) {
                std::uint32_t indices[4]{};double masses[4]{};const auto n=composition(cell,indices,masses,4);
                for(std::uint32_t k=0;k<n;++k)if(indices[k]==oxygen)total+=masses[k];
            }
            return total;
        };
        const auto exact=[&](const char* what) {
            const auto s=status();
            require(std::abs(oxygen_held()-5)<1e-12 && s.maximum_relative_mass_error<1e-12 && s.maximum_relative_energy_error<1e-9,what);
        };
        start(cfg,mats,cells,1);
        frame(none,none);frame(none,none);
        require(owns(nat(10)) && !owns(nat(13)),"Terrain order fixture ownership differs");
        // Whatever order ONI applied a frame's messages in (it applies a dig
        // before a replacement sent ahead of it), the owner ends where ONI does:
        // an activity owner at once, serial pacing by the next frame (the shadow
        // closes a solid it finds, custody opens a wall ONI holds as vacuum).
        const auto settled=[&](bool owned_now,std::uint32_t cell,const char* what) {
            if(activity)require(owns(nat(cell))==owned_now,what);
            frame(none,none);require(owns(nat(cell))==owned_now,what);
        };
        // Closed and opened within one frame, ONI ending open (a tile built and
        // deconstructed in one tick).
        edit({{10,MA_ONI_COLONY_CLOSE},{10,MA_ONI_COLONY_OPEN}});frame([&]{set(10,vacuum,0,3);},none);
        settled(true,10,"A cell closed and opened within one frame became a wall ONI holds open");
        frame(none,none);require(owns(nat(10)) && !owns(nat(13)),"A closed-and-opened cell did not stay owned");
        exact("A closed-and-opened cell changed the owner's inventory");
        // The same edits, ONI ending solid (its dig before the replacement).
        edit({{10,MA_ONI_COLONY_CLOSE},{10,MA_ONI_COLONY_OPEN}});frame([&]{set(10,solid,1000,3);},none);
        settled(false,10,"A cell ONI holds solid after a close and an open was left open");
        exact("Following ONI's solid changed the owner's inventory");
        edit({{10,MA_ONI_COLONY_OPEN}});frame([&]{set(10,vacuum,0,3);},none);
        require(owns(nat(10)),"A dig did not reopen the cell");
        // A solid ONI placed in an owned cell (sand landing natively) and gone
        // again in the same native update: the shadow's close and its undoing
        // both reach the owner in one frame; the cell stays owned.
        frame([&]{set(10,solid,100,3);},[&]{set(10,vacuum,0,2);});
        require(owns(nat(10)),"A native close undone in the same frame left a wall");
        frame(none,none);frame(none,none);
        require(owns(nat(10)),"An undone native close did not stay owned");
        exact("An undone native close changed the owner's inventory");
        // A wall opened and closed within one frame, ONI ending solid, then ONI
        // ending open.
        edit({{2,MA_ONI_COLONY_OPEN},{2,MA_ONI_COLONY_CLOSE}});frame([&]{set(2,solid,1000,3);},none);
        settled(false,2,"A wall opened and closed within one frame was left open");
        edit({{2,MA_ONI_COLONY_OPEN},{2,MA_ONI_COLONY_CLOSE}});frame([&]{set(2,vacuum,0,3);},none);
        settled(true,2,"A wall ONI holds open after an open and a close stayed a wall");
        exact("Opening and closing a wall changed the owner's inventory");
        // Custody: a cell the owner closed while ONI holds it open, with no
        // edit left to reopen it (ONI removed the solid without a message the
        // owner sees). The next capture reopens it as vacuum, adopting nothing;
        // the displaced oxygen stays with its neighbours.
        edit({{10,MA_ONI_COLONY_CLOSE}});frame([&]{set(10,solid,100,3);},none);
        require(!owns(nat(10)),"The custody fixture's close did not apply");
        exact("The custody fixture's close changed the owner's inventory");
        const auto opened=status().opened_cells;
        frame([&]{set(10,vacuum,0,3);},none);
        require(owns(nat(10)) && status().opened_cells==opened+1,"An open cell beside owned cells was left a wall");
        frame(none,none);frame(none,none);
        require(owns(nat(10)) && !owns(nat(13)) && status().opened_cells==opened+1,"Custody reopened a cell twice or opened a hole outside the room");
        exact("Custody changed the owner's inventory");
        // A freeze is what the native frame ended with: an open of the same
        // cell earlier in the frame (a dig of fluid changes nothing) leaves it closed.
        edit({{11,MA_ONI_COLONY_OPEN}});frame(none,[&]{set(11,solid,1,2);});
        require(!owns(nat(11)),"An open earlier in the frame undid a native freeze");
        frame(none,none);require(!owns(nat(11)) && status().maximum_relative_mass_error<1e-12 && status().maximum_relative_energy_error<1e-9,
            "A frozen cell reopened or its ledgers drifted");
        stop();
    }
    set_options(nullptr);set_activity_tiers(0,true);
    std::cout<<"PASS terrain order: the owner ends where ONI does (close/open and open/close either way, native close undone), a freeze stays closed; custody reopens an open wall beside owned cells and never a hole\n";
}
