#include "mixed_atmospheres/oni_backend.h"
#include "mixed_atmospheres/oni_domain.h"
#include "../src/oni_sha256.hpp"
#include <array>
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

static void require(bool condition,const char* message) {
    if (!condition) throw std::runtime_error(message);
}
int main() {
    try {
        require(ma_oni_abi_version()==3,"ABI version");
        require(sizeof(MAOniObserverConfig)==24 && sizeof(MAOniObserverStatus)==512 && sizeof(MAOniTraceEvent)==32,"ABI layouts");
        require(ma::oni::hash_memory("",0)=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855","empty SHA256");
        require(ma::oni::hash_memory("abc",3)=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","SHA256 abc");
        const std::string many(1000000,'a');
        require(ma::oni::hash_memory(many.data(),many.size())=="cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0","SHA256 multi-block");
        MAOniObserverStatus status{};
        status.struct_size=sizeof(status); status.abi_version=3;
        require(ma_oni_observer_status(&status)==MA_ONI_OK && !status.attached && !status.authoritative_fluid_owner,"inactive observer status");
        const auto before=status;
        require(ma_oni_probe(nullptr,&status)==MA_ONI_INVALID_ARGUMENT && std::memcmp(&before,&status,sizeof(status))==0,"invalid config changed output");
        require(std::strlen(ma_oni_last_error())>0,"bounded error text");
        require(ma_oni_probe(nullptr,nullptr)==MA_ONI_INVALID_ARGUMENT,"null output");
        const auto temporary=std::filesystem::temp_directory_path() /
            ("ma-oni-identity-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        require(std::filesystem::create_directory(temporary),"fresh identity fixture directory");
#if defined(_WIN32)
        const auto fake_image=temporary/"OxygenNotIncluded_Data/Plugins/x86_64/SimDLL.dll";
#else
        const auto fake_image=temporary/"OxygenNotIncluded_Data/Plugins/libSimDLL.so";
#endif
        std::filesystem::create_directories(fake_image.parent_path());
        { std::ofstream file(fake_image,std::ios::binary); file << "Not a licensed ONI native image"; }
        const auto path=temporary.u8string();
        const MAOniObserverConfig invalid_image{24,3,MA_ONI_OBSERVER_ONLY,0,reinterpret_cast<const char*>(path.c_str())};
        require(ma_oni_probe(&invalid_image,&status)==MA_ONI_INCOMPATIBLE && std::memcmp(&before,&status,sizeof(status))==0,
                "unknown image probe changed output or accepted native code");
        require(ma_oni_attach_observer(&invalid_image,&status)==MA_ONI_INCOMPATIBLE && std::memcmp(&before,&status,sizeof(status))==0,
                "unknown image attachment changed output or loaded native code");
        std::filesystem::remove_all(temporary);
        status.abi_version=99;
        require(ma_oni_observer_status(&status)==MA_ONI_INVALID_ARGUMENT,"unknown ABI accepted");
        require(ma_oni_detach_observer()==MA_ONI_OK && ma_oni_detach_observer()==MA_ONI_OK,"idempotent inactive detach");
        std::uint32_t count=99;
        require(ma_oni_copy_trace(nullptr,0,&count)==MA_ONI_OK && count==0,"empty trace");
        require(ma_oni_copy_trace(nullptr,0,nullptr)==MA_ONI_INVALID_ARGUMENT,"null trace count");
        require(ma_oni_domain_abi_version()==2 && ma_oni_domain_active()==0,"sealed-domain extension ABI");
        MAOniDomainStatus domain{}; domain.struct_size=sizeof(domain); domain.abi_version=2;
        require(ma_oni_domain_copy(&domain,nullptr,0)==MA_ONI_OK && domain.state==0,"inactive domain status");
        auto obsolete_domain=domain; obsolete_domain.abi_version=1; const auto held_obsolete=obsolete_domain;
        require(ma_oni_domain_copy(&obsolete_domain,nullptr,0)==MA_ONI_INVALID_ARGUMENT &&
            std::memcmp(&obsolete_domain,&held_obsolete,sizeof(obsolete_domain))==0,"obsolete four-species client was accepted or changed output");
        const auto unchanged=domain;
        const MAOniDomainConfig domain_config{40,2,1,64,10,10,3,2,0,0};
        constexpr auto inactive_start_result=MA_ONI_BUSY;
        require(ma_oni_domain_start(&domain_config,&domain)==inactive_start_result && std::memcmp(&domain,&unchanged,sizeof(domain))==0,
                "unattached native domain accepted or changed output");
        require(ma_oni_domain_copy(&domain,nullptr,6)==MA_ONI_INVALID_ARGUMENT && std::memcmp(&domain,&unchanged,sizeof(domain))==0,
                "invalid native domain copy changed output");
        MAOniDomainSpecies species{-1,-1,-1,-1}; const auto held_species=species;
        require(ma_oni_domain_species(99,&species)==MA_ONI_INVALID_ARGUMENT && std::memcmp(&species,&held_species,sizeof(species))==0,
                "unknown domain species changed output");
        require(ma_oni_domain_species(MA_ONI_DOMAIN_SPECIES,&species)==MA_ONI_INVALID_ARGUMENT && std::memcmp(&species,&held_species,sizeof(species))==0,
                "one-past-last domain species accepted or changed output");
        require(ma_oni_domain_species(4,&species)==MA_ONI_OK && species.molar_mass>0 && species.cv>0,
                "absent physical carbon-dioxide definition");
        require(ma_oni_domain_heat_abi_version()==1 && sizeof(MAOniDomainHeat)==48 && sizeof(MAOniDomainHeatStatus)==56,"heat extension ABI");
        MAOniDomainHeatStatus heat{56,1,0,0,0,0,0,0,0};
        require(ma_oni_domain_heat_status(&heat)==MA_ONI_OK && heat.state==0,"inactive heat status");
        const auto held_heat=heat;
        const MAOniDomainHeat request{48,1,0,0,0,0,100,1000};
        require(ma_oni_domain_queue_heat(&request,1,&heat)==MA_ONI_BUSY && std::memcmp(&heat,&held_heat,sizeof(heat))==0,
                "unowned heat accepted or changed output");
        require(ma_oni_domain_transfer_abi_version()==2 && sizeof(MAOniDomainTransfer)==64 &&
                sizeof(MAOniDomainReceipt)==88 && sizeof(MAOniDomainTransferStatus)==104,"transfer extension ABI");
        MAOniDomainTransferStatus transfers{}; transfers.struct_size=104; transfers.abi_version=2;
        require(ma_oni_domain_transfer_status(&transfers)==MA_ONI_OK && transfers.state==0,"inactive transfer status");
        const auto held_transfers=transfers;
        const MAOniDomainTransfer transfer{64,2,0,0,0,1,1,0,-1,0,.05,300};
        require(ma_oni_domain_queue_transfers(&transfer,1,&transfers)==MA_ONI_BUSY &&
                std::memcmp(&transfers,&held_transfers,sizeof(transfers))==0,"unowned gas transfer changed output");
        require(ma_oni_domain_actor_abi_version()==1 && sizeof(MAOniDomainActor)==80 && sizeof(MAOniDomainActorStatus)==64,"actor extension ABI");
        require(ma_oni_domain_thermal_abi_version()==1 && sizeof(MAOniDomainThermalStatus)==80,"conduction extension ABI");
        MAOniDomainThermalStatus thermal{}; thermal.struct_size=80; thermal.abi_version=1;
        require(ma_oni_domain_thermal_status(&thermal)==MA_ONI_OK && thermal.state==0 && thermal.enabled==0,"inactive conduction status");
        require(ma_oni_domain_gravity_abi_version()==1 && sizeof(MAOniDomainGravityStatus)==96,"gravity extension ABI");
        MAOniDomainGravityStatus gravity{}; gravity.struct_size=96; gravity.abi_version=1;
        require(ma_oni_domain_gravity_status(&gravity)==MA_ONI_OK && gravity.state==0 && gravity.enabled==0,"inactive gravity status");
        require(ma_oni_domain_checkpoint_abi_version()==1,"physical checkpoint extension ABI");
        std::array<std::uint8_t,16> checkpoint_buffer{}; checkpoint_buffer.fill(0xa5); const auto held_checkpoint=checkpoint_buffer;
        std::uint32_t required_checkpoint=123;
        require(ma_oni_domain_checkpoint_copy(1,1,checkpoint_buffer.data(),16,&required_checkpoint)==MA_ONI_BUSY &&
            required_checkpoint==123 && checkpoint_buffer==held_checkpoint,"Inactive checkpoint changed output");
        require(ma_oni_domain_checkpoint_advance(checkpoint_buffer.data(),16,.2,checkpoint_buffer.data(),16,&required_checkpoint)==MA_ONI_INVALID_ARGUMENT &&
            required_checkpoint==123 && checkpoint_buffer==held_checkpoint,"Invalid checkpoint advance changed output");
        require(ma_oni_domain_pair_abi_version()==1 && sizeof(MAOniDomainPairStatus)==112,"paired checkpoint extension ABI");
        MAOniDomainPairStatus pair{}; pair.struct_size=112; pair.abi_version=1; const auto empty_pair=pair;
        require(ma_oni_domain_pair_request(1,1,0,0,&pair)==MA_ONI_BUSY && std::memcmp(&pair,&empty_pair,sizeof(pair))==0,
            "Inactive pair request changed its output");
        require(ma_oni_domain_pair_poll(999,&pair)==MA_ONI_INVALID_ARGUMENT && std::memcmp(&pair,&empty_pair,sizeof(pair))==0,
            "Unknown paired token changed poll output");
        require(ma_oni_domain_pair_copy(999,checkpoint_buffer.data(),8,checkpoint_buffer.data()+8,8)==MA_ONI_INVALID_ARGUMENT &&
            checkpoint_buffer==held_checkpoint,"Unknown paired token changed copy outputs");
        MAOniDomainConfig checkpoint_configuration{40,2,1,64,10,10,3,2,0,0};
        require(ma_oni_domain_checkpoint_validate(&checkpoint_configuration,checkpoint_buffer.data(),16,.2)==MA_ONI_INVALID_ARGUMENT,
            "Invalid physical restore image passed independent pre-native validation");
        MAOniDomainActorStatus actors{}; actors.struct_size=64; actors.abi_version=1;
        require(ma_oni_domain_actor_status(&actors)==MA_ONI_OK && actors.state==0 && actors.live_actors==0,"inactive actor status");
        const auto held_actors=actors;
        const MAOniDomainActor actor{80,1,0,0,1,1,0,1,0,0,.05,0,0,0};
        require(ma_oni_domain_configure_actor(&actor,&actors)==MA_ONI_BUSY && std::memcmp(&actors,&held_actors,sizeof(actors))==0,
            "unowned actor configuration accepted or changed output");
        std::cout << "PASS: native observer ABI, SHA256 controls, unknown-image/immutable rejection and inactive lifecycle\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
