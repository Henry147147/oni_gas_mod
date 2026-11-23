// The phase ONI's grid shows for a cell (docs/colony-owner.md#the-shown-phase).
// Since 1.13.0 a cell that holds gas and liquid shows its liquid from 0.001 %
// of its volume, the share from which the readings report liquid; ONI's own
// freezing still takes only a liquid that fills half its cell or has no gas
// beside it. Synthetic owner: four sealed one-cell rooms.

void shown_phase_control() {
    using namespace ma::oni::colony;
    constexpr double dt=static_cast<double>(.2f);
    constexpr std::uint32_t width=9,height=3,solid_ice=99;
    const auto at=[](std::uint32_t x){return x+width;};                       // row 1 of the owner rectangle (the game grid)
    const auto native_at=[](std::uint32_t x){return static_cast<int>((x+1)+2*(width+2));};
    MAOniColonyConfig cfg{};cfg.struct_size=sizeof(cfg);cfg.abi_version=MA_ONI_COLONY_ABI_VERSION;cfg.vacuum_index=vacuum;
    cfg.flags=MA_ONI_COLONY_PHYSICS_CONDUCTION|MA_ONI_COLONY_GAME_KERNEL|MA_ONI_COLONY_GAME_SURFACE|MA_ONI_COLONY_GAME_ACTIVITY;
    cfg.grid_width=cfg.width=width;cfg.grid_height=cfg.height=height;cfg.maximum_timestep_s=static_cast<double>(.1f);cfg.gravity_m_per_s2=9.81;
    auto options=default_options();options.maximum_timestep_s=static_cast<double>(.1f);set_options(&options);set_activity_tiers(0,false);
    std::vector<MAOniColonyMaterial> mats{material("Oxygen",11,oxygen,true,.032,1005),material("Water",13,water,false,.018,4180,1000)};
    mats[1].low_target=solid_ice; // freezes to a solid the catalogue does not hold, below 250 K
    std::vector<MAOniColonyCell> cells(width*height,MAOniColonyCell{0,MA_ONI_COLONY_SOLID,0,0,0,0});
    cells[at(1)]={oxygen,MA_ONI_COLONY_FLUID,1.3f,300,0,0}; // a film: 50 g of water under air
    cells[at(3)]={oxygen,MA_ONI_COLONY_FLUID,1.3f,300,0,0}; // a trace: 2 g
    cells[at(5)]={water,MA_ONI_COLONY_FLUID,600.f,300,0,0}; // more than half a cell of water, then air above it
    cells[at(7)]={water,MA_ONI_COLONY_FLUID,5.f,300,0,0};   // a little water and no gas
    set_diseases({},{},0);start(cfg,mats,cells,1);
    const std::vector<MAOniColonyTransfer> added{{at(1),water,MA_ONI_COLONY_INJECT,0,-1,0,.05,300,0},{at(3),water,MA_ONI_COLONY_INJECT,0,-1,0,.002,300,0},
        {at(5),oxygen,MA_ONI_COLONY_INJECT,0,-1,0,.2,300,0}};
    queue_transfers(added);
    const std::vector<std::uint32_t> actors{at(1),at(3),at(5),at(7)};
    for(std::uint64_t frame=1;frame<=5;++frame) {
        (void)queue_activity(status().session,dt,actors,{});
        advance(frame,dt,2);wait();
        if(status().state!=1)throw std::runtime_error("shown phase control faulted: "+std::string(status().error));
    }
    std::vector<MAOniColonyCellView> views(width*height);MAOniColonyStatus st{};
    copy(st,views.data(),static_cast<std::uint32_t>(views.size()));
    const auto both=MA_ONI_COLONY_VIEW_GAS|MA_ONI_COLONY_VIEW_LIQUID;
    const auto& film=views[at(1)];const auto& trace=views[at(3)];const auto& pool=views[at(5)];const auto& drop=views[at(7)];
    require(film.element_index==water && (film.flags&both)==both && std::abs(film.mass_kg-.05f)<1e-4f &&
        film.liquid_fraction>4e-5f && film.liquid_fraction<6e-5f,"a film of water under air is not shown as water with its own mass");
    require(trace.element_index==oxygen && (trace.flags&both)==both && std::abs(trace.mass_kg-1.3f)<1e-3f && trace.liquid_fraction<1e-5f,
        "a trace of liquid below 0.001 % of the cell is not shown as its gas");
    require(pool.element_index==water && (pool.flags&both)==both && std::abs(pool.mass_kg-600.f)<.1f,"water that fills most of a cell is not shown as water");
    require(drop.element_index==water && (drop.flags&both)==MA_ONI_COLONY_VIEW_LIQUID && std::abs(drop.mass_kg-5.f)<1e-3f,"a liquid without gas is not shown as itself");
    // ONI's own freezing takes the cell: only where the liquid fills half of
    // it or no gas is beside it. A film under air is the owner's to freeze.
    require(!allow_native_transition(native_at(1),water,200) && allow_native_transition(native_at(5),water,200) &&
        allow_native_transition(native_at(7),water,200),"native freezing does not follow the half-cell rule");
    require(!allow_native_transition(native_at(1),water,300) && !allow_native_transition(native_at(5),water,300),"a liquid above its freezing point was offered to native freezing");
    const double film_fraction=film.liquid_fraction,trace_fraction=trace.liquid_fraction;
    stop();
    std::cout<<"PASS shown phase: a film of "<<100*film_fraction<<" % of its cell shows as water beside its air, a trace of "<<100*trace_fraction
        <<" % shows as the air, a pool and a gas-free drop show as water; native freezing takes only the pool and the drop\n";
}
