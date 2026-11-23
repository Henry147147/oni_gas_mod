// Face forces through the colony owner (docs/face-forces.md): the handles
// another mod's fan holds. Synthetic owner only: two sealed rooms of oxygen
// joined by a one-cell opening, inside a larger game grid so that public
// cells differ from owner cells.

void face_force_control() {
    using namespace ma::oni::colony;
    constexpr double dt=static_cast<double>(.2f);
    constexpr std::uint32_t grid_width=20,grid_height=9,offset_x=3,offset_y=2,width=15,height=6;
    const auto at=[](std::uint32_t x,std::uint32_t y){return (x+offset_x)+(y+offset_y)*grid_width;};
    const auto begin=[&](bool tiers) {
        MAOniColonyConfig cfg{};cfg.struct_size=sizeof(cfg);cfg.abi_version=MA_ONI_COLONY_ABI_VERSION;cfg.vacuum_index=vacuum;
        cfg.flags=MA_ONI_COLONY_PHYSICS_CONDUCTION|MA_ONI_COLONY_GAME_KERNEL|MA_ONI_COLONY_GAME_SURFACE|MA_ONI_COLONY_GAME_ACTIVITY;
        cfg.grid_width=grid_width;cfg.grid_height=grid_height;cfg.offset_x=offset_x;cfg.offset_y=offset_y;cfg.width=width;cfg.height=height;
        cfg.maximum_timestep_s=static_cast<double>(.1f);cfg.gravity_m_per_s2=9.81;
        auto options=default_options();options.maximum_timestep_s=static_cast<double>(.1f);set_options(&options);set_activity_tiers(0,tiers);
        const std::vector<MAOniColonyMaterial> mats{material("Oxygen",11,oxygen,true,.032,1005)};
        std::vector<MAOniColonyCell> cells(width*height,MAOniColonyCell{0,MA_ONI_COLONY_SOLID,0,0,0,0});
        for(std::uint32_t y=1;y<=4;++y)for(std::uint32_t x=1;x<=13;++x)
            if(x!=7 || y==2)cells[x+y*width]={oxygen,MA_ONI_COLONY_FLUID,1.3f,300,0,0};
        set_diseases({},{},0);start(cfg,mats,cells,1);
        return cfg;
    };
    std::uint64_t ticket=0;
    const auto frame=[&](std::span<const std::uint32_t> actors={}) {
        (void)queue_activity(status().session,dt,actors,{});
        advance(++ticket,dt,2);wait();
        if(status().state!=1)throw std::runtime_error("face force control faulted: "+std::string(status().error));
    };
    const auto room=[&](bool right) {
        double total=0;
        for(std::uint32_t y=1;y<=4;++y)for(std::uint32_t x=right?8:1;x<=(right?13u:6u);++x) {
            std::uint32_t indices[4]{};double masses[4]{};const auto n=composition(at(x,y),indices,masses,4);
            for(std::uint32_t k=0;k<n;++k)total+=masses[k];
        }
        return total;
    };
    const auto booked_heat=[&] {
        MAOniColonyStatus st{};std::vector<MAOniColonySampleCell> rows;std::vector<MAOniColonySamplePart> parts;double mass=0,heat=0;
        const std::array<std::uint32_t,1> one{at(1,1)};
        sample_frame(status().completed_frames,one,st,rows,parts,&mass,&heat);return heat;
    };
    const auto refused=[](const std::function<void()>& call) {
        try {call();} catch(const std::invalid_argument&) {return true;}
        return false;
    };

    // No owner: nothing is active and nothing can be opened.
    require(!face_forces_active() && open_face_force()==0,"Face forces were offered without a colony owner");
    require(!set_face_force(1,at(7,2),1,0,120,6) && take_face_force_work(1)==0,"An unknown face force handle was accepted");
    close_face_force(1);

    begin(false);sample_history(4);
    require(face_forces_active(),"A healthy owner does not accept face forces");
    std::vector<std::uint8_t> undriven;
    {
    const auto fan=open_face_force(),idle=open_face_force();
    require(fan!=0 && idle!=0 && fan!=idle,"Face force handles are not distinct and nonzero");
    require(!set_face_force(fan+100,at(7,2),1,0,120,6),"An unknown handle was set");
    for(const auto& bad:{std::array<double,4>{1,1,120,6},{0,0,120,6},{2,0,120,6},{1,0,-1,6},{1,0,120,-1},{1,0,10001,6},{1,0,120,101},
            {1,0,std::numeric_limits<double>::quiet_NaN(),6},{1,0,120,std::numeric_limits<double>::infinity()}})
        require(refused([&]{(void)set_face_force(fan,at(7,2),static_cast<std::int32_t>(bad[0]),static_cast<std::int32_t>(bad[1]),bad[2],bad[3]);}),
            "An invalid face force was accepted");
    require(face_force_entries().empty(),"A refused or unset handle drives a face");
    // Cells that cannot be driven are accepted and do nothing: outside the
    // colony rectangle, outside the game grid, a solid neighbour, a solid cell.
    for(const std::int64_t cell:{std::int64_t{0},std::int64_t{-5},std::int64_t{grid_width}*grid_height+7,std::int64_t{at(6,3)},std::int64_t{at(7,3)}})
        require(set_face_force(idle,cell,1,0,120,6),"A face that cannot be driven was refused");
    // The same frames without any handle end in the same state.
    for(unsigned n=0;n<5;++n)frame();
    undriven=checkpoint();
    require(take_face_force_work(idle)==0,"A face that cannot be driven booked work");
    require(booked_heat()==0,"A face that cannot be driven booked energy");
    // Zero strength: the handle stays open without a force.
    const auto listed=[&](std::uint32_t handle) {
        for(const auto& entry:face_force_entries())if(entry.handle==handle)return true;
        return false;
    };
    require(set_face_force(fan,at(7,2),1,0,0,6) && !listed(fan) && set_face_force(fan,at(7,2),1,0,120,0) && !listed(fan),"A zero strength drives a face");
    require(set_face_force(fan,at(7,2),1,0,1e-12,6) && !listed(fan),"A strength below the floor drives a face");
    }

    {
        stop();begin(false);sample_history(4);ticket=0;
        for(unsigned n=0;n<5;++n)frame();
        require(checkpoint()==undriven,"Handles on faces that cannot be driven changed the colony");
    }
    const auto fan=open_face_force(),idle=open_face_force();
    // The fan: 120 Pa, 6 m/s, blowing right through the opening.
    require(set_face_force(fan,at(7,2),1,0,120,6),"The fan's force was refused");
    require(face_force_entries().size()==1 && face_force_entries()[0].handle==fan,"The driven handle is not listed");
    long double taken=0;double largest=0;
    for(unsigned n=0;n<600;++n) {
        require(set_face_force(fan,at(7,2),1,0,120,6),"An unchanged Set was refused"); // as a fan does every tick
        frame();
        const double work=take_face_force_work(fan);taken+=work;largest=std::max(largest,work);
    }
    const double left=room(false),right=room(true);
    MAOniColonyStatus st{};std::vector<MAOniColonyReading> readings;std::vector<MAOniColonyConstituent> parts;
    (void)ma::oni::colony::readings(readings,parts);
    double p_left=0,p_right=0;
    for(const auto& r:readings) {
        if(r.public_cell==at(3,2))p_left=r.mechanical_pressure_pa;
        if(r.public_cell==at(11,2))p_right=r.mechanical_pressure_pa;
    }
    require(std::abs(p_right-p_left-120)<3,"The fan did not settle at its stall pressure");
    require(right>left,"The fan did not move gas the way it blows");
    require(taken>0 && largest<=.25*120*6*dt*(1+1e-9),"The fan's work is not positive or exceeds a quarter of stall pressure times free speed");
    // The energy ledger booked exactly the work the handle reported, and closed.
    const double booked=booked_heat();
    require(std::abs(booked-static_cast<double>(taken))<=1e-9*std::abs(booked),"The handle's work differs from the ledger's booked energy");
    require(status().maximum_relative_energy_error<1e-9 && status().maximum_relative_mass_error<1e-12,"The ledgers did not close with a fan running");
    require(take_face_force_work(fan)==0,"Work was reported twice");
    (void)st;

    // Nothing is saved: the checkpoint of this state is the same with the
    // handles open as after closing them, and a restored colony has none.
    const auto with_handles=checkpoint();
    close_face_force(fan);close_face_force(idle);close_face_force(fan);
    require(checkpoint()==with_handles,"Open face force handles changed the checkpoint");
    require(!set_face_force(fan,at(7,2),1,0,120,6) && take_face_force_work(fan)==0,"A closed handle is still known");
    const auto mats=std::vector<MAOniColonyMaterial>{material("Oxygen",11,oxygen,true,.032,1005)};
    const auto reopened=open_face_force();
    require(set_face_force(reopened,at(7,2),1,0,120,6),"A new handle was refused");
    stop();
    require(!face_forces_active() && !set_face_force(reopened,at(7,2),1,0,120,6) && open_face_force()==0,"Handles outlived the colony owner");
    {
        const auto parts_of=ma::oni::colony::journal_replay::parse(with_handles);
        restore(parts_of.config,parts_of.materials,with_handles,1);
        require(face_forces_active() && face_force_entries().empty(),"A restored colony holds face forces");
        require(checkpoint()==with_handles,"The restored owner differs from the saved image");
        // No force acts until it is set again: the rooms relax towards equal pressure.
        const double before=room(true)-room(false);
        ticket=status().native_ticket;
        for(unsigned n=0;n<50;++n)frame();
        require(room(true)-room(false)<.5*before,"A force acted on a restored colony before it was set again");
        stop();
    }

    // A fan switched on in a paused region wakes it.
    begin(true);ticket=0;
    for(unsigned n=0;n<600;++n)frame(); // no actors: past the pause deadline
    const double paused_left=room(false);
    for(unsigned n=0;n<20;++n)frame();
    require(room(false)==paused_left,"The undisturbed region did not pause");
    const auto late=open_face_force();
    require(set_face_force(late,at(7,2),1,0,120,6),"The late fan was refused");
    double woke=0;
    for(unsigned n=0;n<100;++n){frame();woke+=take_face_force_work(late);}
    require(room(true)-room(false)>.5*(right-left) && woke>0,"A fan switched on in a paused region did not move gas");
    // A fault ends every handle.
    fault_from_adapter("face force control");
    require(!face_forces_active() && !set_face_force(late,at(7,2),1,0,120,6) && open_face_force()==0,"Handles outlived a fault");
    stop();
    std::cout<<"PASS face forces through the owner: a 120 Pa handle settles at "<<p_right-p_left<<" Pa, its "<<static_cast<double>(taken)
        <<" J of work is the ledger's booked energy ("<<booked<<" J), nothing enters the checkpoint, handles end with the owner, "
        <<"a restored colony has none, and a handle wakes a paused region ("<<woke<<" J)\n";
}
