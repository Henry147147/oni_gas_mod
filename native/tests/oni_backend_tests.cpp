#include "mixed_atmospheres/oni_conduit_contact.h"
#include "mixed_atmospheres/oni_backend.h"
#include "mixed_atmospheres/oni_domain.h"
#include "mixed_atmospheres/oni_publication.h"
#include "mixed_atmospheres/oni_colony.h"
#include "../src/oni_sha256.hpp"
#include "../src/oni_funchook_storage.hpp"
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
        require(ma_oni_colony_precise_receipt_version()==2,"precise receipt observation capability");
        static_assert(sizeof(MAOniColonyParcelContext)==72);
        MAOniColonyParcelContext parcel_context{};parcel_context.struct_size=sizeof(parcel_context);parcel_context.abi_version=1;
        parcel_context.session=456;const auto original_context=parcel_context;
        require(ma_oni_colony_parcel_context_version()==1 &&
            ma_oni_colony_copy_parcel_context(nullptr)==MA_ONI_INVALID_ARGUMENT &&
            ma_oni_colony_copy_parcel_context(&parcel_context)==MA_ONI_INVALID_ARGUMENT &&
            std::memcmp(&parcel_context,&original_context,sizeof(parcel_context))==0,"invalid/inactive parcel context changed output");
        parcel_context.reserved=1;const auto invalid_context=parcel_context;
        require(ma_oni_colony_copy_parcel_context(&parcel_context)==MA_ONI_INVALID_ARGUMENT &&
            std::memcmp(&parcel_context,&invalid_context,sizeof(parcel_context))==0,"invalid parcel context header changed output");
        require(ma_oni_colony_actor_batch_version()==1 &&
            ma_oni_colony_queue_actor_batch(nullptr,0,nullptr,0,nullptr,0)==MA_ONI_INVALID_ARGUMENT,
            "atomic actor batch capability/empty request");
        std::uint32_t copied_views=123; MAOniColonyStatus cached_status{}; cached_status.struct_size=sizeof(cached_status); cached_status.abi_version=1;
        const auto held_cached_status=cached_status;
        require(ma_oni_colony_copy_changed(0,0,&cached_status,nullptr,0,nullptr)==MA_ONI_INVALID_ARGUMENT &&
            ma_oni_colony_copy_changed(0,0,&cached_status,nullptr,0,&copied_views)==MA_ONI_INVALID_ARGUMENT && copied_views==123 &&
            std::memcmp(&cached_status,&held_cached_status,sizeof(cached_status))==0,"invalid/inactive cached copy changed outputs");
        const std::uint32_t sample_cell=1; std::uint32_t sample_parts=123; MAOniColonyStatus sample_status{}; sample_status.state=99;
        const auto held_sample_status=sample_status; double sample_converted=456;
        require(ma_oni_colony_sample(nullptr,1,&sample_status,nullptr,nullptr,0,&sample_parts,&sample_converted)==MA_ONI_INVALID_ARGUMENT &&
            ma_oni_colony_sample(&sample_cell,0,&sample_status,nullptr,nullptr,0,&sample_parts,&sample_converted)==MA_ONI_INVALID_ARGUMENT &&
            ma_oni_colony_sample(&sample_cell,1,&sample_status,nullptr,nullptr,0,&sample_parts,&sample_converted)==MA_ONI_INVALID_ARGUMENT &&
            sample_parts==123 && sample_converted==456 && std::memcmp(&sample_status,&held_sample_status,sizeof(sample_status))==0,
            "invalid/inactive arena sample changed outputs");
        std::uint32_t receipt_bytes=99;
        require(ma_oni_colony_enable_precise_receipts(2)==MA_ONI_INVALID_ARGUMENT &&
            ma_oni_colony_enable_precise_receipts(1)==MA_ONI_INVALID_ARGUMENT,"invalid/inactive precise receipt mode admitted");
        require(ma_oni_colony_copy_precise_receipt(1,nullptr,0,nullptr)==MA_ONI_INVALID_ARGUMENT &&
            ma_oni_colony_copy_precise_receipt(1,nullptr,1,&receipt_bytes)==MA_ONI_INVALID_ARGUMENT && receipt_bytes==99,
            "invalid precise receipt buffer changed size output");
        require(ma_oni_colony_copy_precise_receipt(1,nullptr,0,&receipt_bytes)==MA_ONI_OK && receipt_bytes==0 &&
            ma_oni_colony_wait()==MA_ONI_OK,"absent precise receipt or idle wait failed");
        std::uint32_t job_state=123; std::uint64_t last_receipt=456,job_token=789;
        require(ma_oni_colony_queue_pipe_job(1,nullptr,nullptr,0,&job_state,&last_receipt)==MA_ONI_INVALID_ARGUMENT &&
            ma_oni_colony_pipe_job_state(1,nullptr,&last_receipt)==MA_ONI_INVALID_ARGUMENT &&
            ma_oni_colony_pipe_job_state(1,&job_state,&last_receipt)==MA_ONI_INVALID_ARGUMENT && job_state==123 && last_receipt==456,
            "invalid/inactive durable pipe job changed outputs");
        require(ma_oni_colony_receipt_job(1,nullptr)==MA_ONI_INVALID_ARGUMENT &&
            ma_oni_colony_receipt_job(1,&job_token)==MA_ONI_OK && job_token==0,"unknown pipe receipt token");
        const std::uint8_t invalid_parcel=0;
        require(ma_oni_colony_moving_parcel_version()==1 &&
            ma_oni_colony_queue_moving_parcel(1,1,nullptr,1,&job_state,&last_receipt)==MA_ONI_INVALID_ARGUMENT &&
            ma_oni_colony_queue_moving_parcel(1,1,&invalid_parcel,1,&job_state,&last_receipt)==MA_ONI_INVALID_ARGUMENT &&
            job_state==123 && last_receipt==456,"invalid moving request changed outputs");
        receipt_bytes=99;
        require(ma_oni_colony_copy_moving_outcome(1,nullptr,1,&receipt_bytes)==MA_ONI_INVALID_ARGUMENT && receipt_bytes==99 &&
            ma_oni_colony_copy_moving_outcome(1,nullptr,0,nullptr)==MA_ONI_INVALID_ARGUMENT,"invalid moving buffer changed output");
        require(ma_oni_colony_copy_moving_outcome(1,nullptr,0,&receipt_bytes)==MA_ONI_OK && receipt_bytes==0,"absent moving outcome copy failed");
        static_assert(sizeof(MAOniColonyPassiveStamp)==32 && sizeof(MAOniColonyPassiveRow)==24);
        MAOniColonyPassiveStamp passive_stamp{32,1,147,148,149};const auto passive_stamp_before=passive_stamp;
        MAOniColonyPassiveRow passive_row{1,0,2,3};const auto passive_row_before=passive_row;
        const std::uint32_t passive_cell=4;std::uint32_t passive_count=147;
        require(ma_oni_colony_passive_snapshot_version()==1 &&
            ma_oni_colony_read_passives(0,0,nullptr,1,&passive_stamp,&passive_row,1,&passive_count)==MA_ONI_INVALID_ARGUMENT &&
            ma_oni_colony_read_passives(0,0,&passive_cell,1,&passive_stamp,nullptr,1,&passive_count)==MA_ONI_INVALID_ARGUMENT &&
            ma_oni_colony_read_passives(0,0,&passive_cell,1,&passive_stamp,&passive_row,1,&passive_count)==MA_ONI_INVALID_ARGUMENT &&
            passive_count==147 && std::memcmp(&passive_stamp,&passive_stamp_before,sizeof(passive_stamp))==0 &&
            std::memcmp(&passive_row,&passive_row_before,sizeof(passive_row))==0,"invalid/inactive passive observation changed outputs");
        static_assert(sizeof(MAOniConduitContactInput)==44 && sizeof(MAOniConduitContactResult)==20);
        MAOniConduitContactInput contact{44,1,0,0,.2f,300,1.005f,.024f,400,1.94f,8};
        MAOniConduitContactResult contact_result{20,1,147,148,149};const auto unchanged_contact=contact_result;
        require(ma_oni_conduit_contact_replay(nullptr,1,&contact_result,1)==MA_ONI_INVALID_ARGUMENT &&
            ma_oni_conduit_contact_replay(&contact,1,nullptr,1)==MA_ONI_INVALID_ARGUMENT &&
            ma_oni_conduit_contact_replay(&contact,1,&contact_result,0)==MA_ONI_BUFFER_TOO_SMALL &&
            std::memcmp(&contact_result,&unchanged_contact,sizeof(contact_result))==0,"invalid thermal replay modified outputs");
        contact.flags=2;require(ma_oni_conduit_contact_replay(&contact,1,&contact_result,1)==MA_ONI_INVALID_ARGUMENT,"unknown thermal replay flags accepted");
        contact.flags=0;
#if defined(__linux__)
        require(ma_oni_conduit_contact_version()==1 && ma_oni_conduit_contact_replay(&contact,1,&contact_result,1)==MA_ONI_BUSY &&
            std::memcmp(&contact_result,&unchanged_contact,sizeof(contact_result))==0,"unbound contact replay changed outputs");
#else
        require(ma_oni_conduit_contact_version()==0,"unsupported conduit replay capability");
#endif
        static_assert(sizeof(MAOniDomainPublicationInfo)==64);
        require(ma_oni_domain_publication_abi_version()==1,"native publication ABI version");
        MAOniDomainPublicationInfo publication{};publication.struct_size=sizeof(publication);publication.abi_version=1;publication.token=99;
        const auto original_publication=publication;
        require(ma_oni_domain_publication_acquire(nullptr,&publication)==MA_ONI_INVALID_ARGUMENT &&
            std::memcmp(&publication,&original_publication,sizeof(publication))==0,"null publication changed output");
        require(ma_oni_domain_publication_acquire(reinterpret_cast<void*>(std::uintptr_t{1}),&publication)==MA_ONI_BUSY &&
            std::memcmp(&publication,&original_publication,sizeof(publication))==0,"unbound publication changed output");
        publication.abi_version=99;
        require(ma_oni_domain_publication_acquire(reinterpret_cast<void*>(std::uintptr_t{1}),&publication)==MA_ONI_INVALID_ARGUMENT,
            "unknown native publication ABI admitted");
        require(ma_oni_domain_publication_release(0)==MA_ONI_INVALID_ARGUMENT,"unknown publication lease released");
        const MAOniDomainCell* borrowed=nullptr;std::uint32_t borrowed_count=123;
        require(ma_oni_domain_publication_cells(0,&borrowed,&borrowed_count)==MA_ONI_INVALID_ARGUMENT && borrowed==nullptr && borrowed_count==123,
            "invalid publication view changed output");
#if defined(__linux__)
        const ma::oni::TrampolineStorage storage{0xa41c0,0xe40,4096,0x2a000,0x2a000,0x7a1b9};
        std::array<Elf64_Phdr,2> headers{};
        headers[0].p_type=PT_LOAD;headers[0].p_flags=PF_R|PF_X;
        headers[0].p_vaddr=0x2a000;headers[0].p_offset=0x2a000;
        headers[0].p_memsz=0x7a1b9;headers[0].p_filesz=0x7a1b9;headers[0].p_align=4096;
        headers[1].p_type=PT_LOAD;headers[1].p_flags=PF_R;
        headers[1].p_vaddr=0xa5000;headers[1].p_offset=0xa5000;
        headers[1].p_memsz=0x1000;headers[1].p_filesz=0x1000;headers[1].p_align=4096;
        require(ma::oni::verified_trampoline_segment(headers,storage),"verified RX tail padding rejected");
        auto modified=headers;modified[0].p_flags|=PF_W;
        require(!ma::oni::verified_trampoline_segment(modified,storage),"writable segment accepted for trampolines");
        modified=headers;modified[0].p_filesz-=1;
        require(!ma::oni::verified_trampoline_segment(modified,storage),"BSS accepted for trampolines");
        modified=headers;modified[1].p_vaddr=storage.rva;
        require(!ma::oni::verified_trampoline_segment(modified,storage),"overlapping load segment accepted for trampolines");
        modified=headers;modified[1]=headers[0];
        require(!ma::oni::verified_trampoline_segment(modified,storage),"duplicate trampoline segment accepted");
        for (unsigned field=0;field<6;++field) {
            auto invalid=storage;
            switch (field) {
                case 0: invalid.rva-=16;break;
                case 1: invalid.bytes+=16;break;
                case 2: invalid.page_bytes=8192;break;
                case 3: invalid.segment_rva=UINTPTR_MAX;break;
                case 4: invalid.segment_file_offset+=4096;break;
                default: invalid.segment_memory_bytes=UINTPTR_MAX;break;
            }
            require(!ma::oni::verified_trampoline_segment(headers,invalid),"invalid trampoline bounds accepted");
        }
#endif
        require(ma_oni_abi_version()==4,"ABI version");
        require(sizeof(MAOniObserverConfig)==24 && sizeof(MAOniObserverStatus)==528 && sizeof(MAOniTraceEvent)==32,"ABI layouts");
        require(ma::oni::hash_memory("",0)=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855","empty SHA256");
        require(ma::oni::hash_memory("abc",3)=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","SHA256 abc");
        const std::string many(1000000,'a');
        require(ma::oni::hash_memory(many.data(),many.size())=="cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0","SHA256 multi-block");
        MAOniObserverStatus status{};
        status.struct_size=sizeof(status); status.abi_version=4;
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
        const MAOniObserverConfig invalid_image{24,4,MA_ONI_OBSERVER_ONLY,0,reinterpret_cast<const char*>(path.c_str())};
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
        require(ma_oni_domain_completed_snapshot_abi_version()==1,"Completed frame snapshot capability ABI");
        std::array<std::uint8_t,32> completed{};completed.fill(0x5a);const auto held_completed=completed;
        std::uint32_t completed_bytes=123;
        require(ma_oni_domain_completed_snapshot_copy(1,0,completed.data(),32,&completed_bytes)==MA_ONI_BUSY &&
            completed_bytes==123 && completed==held_completed,"Inactive completed frame copy changed outputs");
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
        require(ma_oni_domain_start(nullptr,&domain)==MA_ONI_INVALID_ARGUMENT && std::memcmp(&domain,&unchanged,sizeof(domain))==0,
                "Invalid domain startup escaped its C boundary or changed output");
        auto invalid_domain=domain; invalid_domain.struct_size=0; const auto held_invalid_domain=invalid_domain;
        require(ma_oni_domain_start(&domain_config,&invalid_domain)==MA_ONI_INVALID_ARGUMENT &&
                std::memcmp(&invalid_domain,&held_invalid_domain,sizeof(invalid_domain))==0,
                "Invalid startup output ABI escaped its C boundary or changed output");
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
        require(ma_oni_domain_chunk_heat_abi_version()==1 && sizeof(MAOniDomainChunkHeatStatus)==112,"Native body heat ABI");
        MAOniDomainChunkHeatStatus chunk_heat{};chunk_heat.struct_size=112;chunk_heat.abi_version=1;
        require(ma_oni_domain_chunk_heat_status(&chunk_heat)==MA_ONI_OK && chunk_heat.state==0 && chunk_heat.contacts==0,"Inactive native body heat status");
        chunk_heat.abi_version=2;const auto held_chunk=chunk_heat;
        require(ma_oni_domain_chunk_heat_status(&chunk_heat)==MA_ONI_INVALID_ARGUMENT &&
            std::memcmp(&chunk_heat,&held_chunk,sizeof(chunk_heat))==0,"Invalid native body heat ABI changed output");
        require(ma_oni_domain_solid_heat_abi_version()==1 && sizeof(MAOniDomainSolidHeatCell)==48 &&
                sizeof(MAOniDomainSolidHeatStatus)==112,"Native solid heat ABI");
        MAOniDomainSolidHeatStatus solid_heat{};solid_heat.struct_size=112;solid_heat.abi_version=1;
        require(ma_oni_domain_solid_heat_status(&solid_heat)==MA_ONI_OK && solid_heat.state==0 && solid_heat.contacts==0,
                "Inactive native solid heat status");
        const auto held_solid=solid_heat;
        const MAOniDomainSolidHeatCell solid_input{48,1,649,123,1,0,10.F,.793F,2.F,255};
        require(ma_oni_domain_solid_heat_startup_abi_version()==1,"Native solid startup ABI");
        auto initial_solid=solid_input;initial_solid.session=0;
        require(ma_oni_domain_start_with_solid_heat(&domain_config,&initial_solid,1,&domain,&solid_heat)==MA_ONI_BUSY &&
            std::memcmp(&domain,&unchanged,sizeof(domain))==0 && std::memcmp(&solid_heat,&held_solid,sizeof(solid_heat))==0,
            "Unattached solid startup changed either output");
        for(const auto count:{0U,129U})
            require(ma_oni_domain_start_with_solid_heat(&domain_config,&initial_solid,count,&domain,&solid_heat)==MA_ONI_INVALID_ARGUMENT &&
                std::memcmp(&domain,&unchanged,sizeof(domain))==0 && std::memcmp(&solid_heat,&held_solid,sizeof(solid_heat))==0,
                "Invalid solid startup count changed either output");
        require(ma_oni_domain_start_with_solid_heat(&domain_config,nullptr,1,&domain,&solid_heat)==MA_ONI_INVALID_ARGUMENT &&
            ma_oni_domain_start_with_solid_heat(&domain_config,&initial_solid,1,&domain,nullptr)==MA_ONI_INVALID_ARGUMENT &&
            std::memcmp(&domain,&unchanged,sizeof(domain))==0 && std::memcmp(&solid_heat,&held_solid,sizeof(solid_heat))==0,
            "Invalid solid startup pointers changed either output");
        require(ma_oni_domain_solid_heat_configure(&solid_input,1,&solid_heat)==MA_ONI_BUSY &&
                std::memcmp(&solid_heat,&held_solid,sizeof(solid_heat))==0,"Inactive solid registration changed output");
        for(const auto count:{0U,129U}) require(ma_oni_domain_solid_heat_configure(&solid_input,count,&solid_heat)==MA_ONI_INVALID_ARGUMENT &&
                std::memcmp(&solid_heat,&held_solid,sizeof(solid_heat))==0,"Invalid solid registration count changed output");
        require(ma_oni_domain_solid_heat_configure(nullptr,1,&solid_heat)==MA_ONI_INVALID_ARGUMENT &&
                std::memcmp(&solid_heat,&held_solid,sizeof(solid_heat))==0,"Null solid registration changed output");
        solid_heat.abi_version=2;const auto invalid_solid=solid_heat;
        require(ma_oni_domain_solid_heat_status(&solid_heat)==MA_ONI_INVALID_ARGUMENT &&
                ma_oni_domain_solid_heat_configure(&solid_input,1,&solid_heat)==MA_ONI_INVALID_ARGUMENT &&
                std::memcmp(&solid_heat,&invalid_solid,sizeof(solid_heat))==0,"Invalid solid heat ABI changed output");
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
        require(ma_oni_domain_thermal_abi_version()==2 && sizeof(MAOniDomainThermalStatus)==88,"conduction extension ABI");
        MAOniDomainThermalStatus thermal{}; thermal.struct_size=88; thermal.abi_version=2;
        require(ma_oni_domain_thermal_status(&thermal)==MA_ONI_OK && thermal.state==0 && thermal.enabled==0,"inactive conduction status");
        const auto held_thermal=thermal;thermal.abi_version=1;
        require(ma_oni_domain_thermal_status(&thermal)==MA_ONI_INVALID_ARGUMENT,"Legacy conduction layout was silently admitted");
        thermal=held_thermal;thermal.struct_size=80;
        require(ma_oni_domain_thermal_status(&thermal)==MA_ONI_INVALID_ARGUMENT,"Truncated conduction layout was silently admitted");
        require(ma_oni_domain_viscosity_abi_version()==1 && sizeof(MAOniDomainViscosityStatus)==120,"physical viscosity extension ABI");
        require(ma_oni_domain_radial_consumption_abi_version()==1,"radial consumption capability ABI");
        MAOniDomainViscosityStatus viscosity{};viscosity.struct_size=120;viscosity.abi_version=1;
        require(ma_oni_domain_viscosity_status(&viscosity)==MA_ONI_OK && viscosity.state==0 && viscosity.enabled==0,"inactive viscosity status");
        viscosity.abi_version=2;const auto held_viscosity=viscosity;
        require(ma_oni_domain_viscosity_status(&viscosity)==MA_ONI_INVALID_ARGUMENT &&
            std::memcmp(&viscosity,&held_viscosity,sizeof(viscosity))==0,"Invalid viscosity ABI changed output");
        require(ma_oni_domain_gravity_abi_version()==1 && sizeof(MAOniDomainGravityStatus)==96,"gravity extension ABI");
        MAOniDomainGravityStatus gravity{}; gravity.struct_size=96; gravity.abi_version=1;
        require(ma_oni_domain_gravity_status(&gravity)==MA_ONI_OK && gravity.state==0 && gravity.enabled==0,"inactive gravity status");
        require(ma_oni_domain_checkpoint_abi_version()==1,"physical checkpoint extension ABI");
        std::array<std::uint8_t,16> checkpoint_buffer{}; checkpoint_buffer.fill(0xa5); const auto held_checkpoint=checkpoint_buffer;
        require(ma_oni_domain_restore(&domain_config,checkpoint_buffer.data(),16,.2,&domain)==MA_ONI_BUSY &&
                std::memcmp(&domain,&unchanged,sizeof(domain))==0 && checkpoint_buffer==held_checkpoint,
                "Unattached restore escaped its C boundary or changed source/output");
        std::uint32_t required_checkpoint=123;
        require(ma_oni_domain_checkpoint_copy(1,1,checkpoint_buffer.data(),16,&required_checkpoint)==MA_ONI_BUSY &&
            required_checkpoint==123 && checkpoint_buffer==held_checkpoint,"Inactive checkpoint changed output");
        require(ma_oni_domain_checkpoint_advance(checkpoint_buffer.data(),16,.2,checkpoint_buffer.data(),16,&required_checkpoint)==MA_ONI_INVALID_ARGUMENT &&
            required_checkpoint==123 && checkpoint_buffer==held_checkpoint,"Invalid checkpoint advance changed output");
        require(ma_oni_domain_actor_checkpoint_abi_version()==1 && sizeof(MAOniDomainPersistentActor)==96 &&
            ma_oni_domain_pending_actor_restores()==0,"Durable actor extension ABI or inactive restore status");
        require(ma_oni_domain_snapshot_copy(1,1,checkpoint_buffer.data(),16,&required_checkpoint)==MA_ONI_BUSY &&
            required_checkpoint==123 && checkpoint_buffer==held_checkpoint,"Inactive complete snapshot changed output");
        require(ma_oni_domain_snapshot_advance(checkpoint_buffer.data(),16,.2,checkpoint_buffer.data(),16,&required_checkpoint)==MA_ONI_INVALID_ARGUMENT &&
            required_checkpoint==123 && checkpoint_buffer==held_checkpoint,"Invalid complete snapshot advance changed output");
        require(ma_oni_domain_pair_abi_version()==1 && sizeof(MAOniDomainPairStatus)==112,"paired checkpoint extension ABI");
        MAOniDomainPairStatus pair{}; pair.struct_size=112; pair.abi_version=1; const auto empty_pair=pair;
        require(ma_oni_domain_pair_request(1,1,0,0,&pair)==MA_ONI_BUSY && std::memcmp(&pair,&empty_pair,sizeof(pair))==0,
            "Inactive pair request changed its output");
        require(ma_oni_domain_pair_snapshot_request(1,1,0,0,&pair)==MA_ONI_BUSY && std::memcmp(&pair,&empty_pair,sizeof(pair))==0,
            "Inactive actor-aware pair request changed its output");
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
        const MAOniDomainPersistentActor persistent_actor{actor,1,12345,0};
        require(ma_oni_domain_configure_persistent_actors(&persistent_actor,1,&actors)==MA_ONI_BUSY &&
            std::memcmp(&actors,&held_actors,sizeof(actors))==0,"Inactive persistent actor changed its output");
        std::cout << "PASS: native observer ABI, SHA256 controls, unknown-image/immutable rejection and inactive lifecycle\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
