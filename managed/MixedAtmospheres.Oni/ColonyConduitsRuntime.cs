using System;
using System.Collections.Generic;
using System.Linq;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // The whole mixed-pipe runtime of a colony (docs/mixed-pipes.md#runtime):
    // owned cells, precise world ports, pumps, vents, spills, machine ports,
    // body contact heat and all-identity biology, started together.
    internal static class ColonyPipeRuntime
    {
        internal static bool Active=>ColonyConduits.Active;
        // New colonies own their pipe mixtures once every pipe system has
        // passed its in-game check; until then a run selects it explicitly.
        // A loaded colony keeps the mode its save recorded.
        internal static bool NewColoniesUseMixedPipes=>Environment.GetEnvironmentVariable("MA_COLONY_MIXED_PIPES")=="1";
        internal static string StartNotes { get; private set; }="";
        internal static void Start()
        {
            if(ColonyConduits.Active&&ColonyMachinePorts.Enabled)return;
            ColonyConduits.StartControl();ColonyPipePorts.StartControl();ColonyPipePumps.StartControl();ColonyPipeVents.StartControl();
            ColonyPipeTeardown.StartControl();ColonyMachinePorts.StartControl();
            var notes="";
            // Exact body contact and all-identity biology need pinned native
            // layouts; without them the carrier/projected paths remain.
            try{ColonyPipeContactHeat.StartControl();}
            catch(NotSupportedException error){notes+="contact heat unavailable: "+error.Message+"; ";}
            try{ColonyPipeBiology.StartControl();}
            catch(NotSupportedException error){notes+="all-identity biology unavailable: "+error.Message+"; ";}
            StartNotes=notes;
            if(notes.Length>0)Debug.LogWarning("[Mixed Atmospheres] Mixed pipes started with fallbacks: "+notes);
        }
        // Save point (docs/mixed-pipes.md#saved-runtime). The caller has
        // delivered and acknowledged every committed owner receipt.
        internal static void RequireQuiescent()
        {
            if(!Active)return;
            if(ColonyPipeContactHeat.Open)throw new InvalidOperationException("A pipe heat contact is open at the save point.");
            ColonyConduits.RequireQuiescent();ColonyPipePorts.RequireQuiescent();ColonyMachinePorts.RequireQuiescent();
        }
        // MACPIP01 schema 3: cells/accounts (schema 1), boundary jobs
        // (schema 2) and every module's runtime state.
        internal static byte[] Capture()
        {
            RequireQuiescent();
            var cells=ColonyConduits.Capture();var boundary=ColonyPipePorts.ValidateSealedBoundary();
            var runtime=new PipeRuntimeImage();
            ColonyPipePorts.Capture(runtime);ColonyPipePumps.Capture(runtime);ColonyPipeVents.Capture(runtime);ColonyPipeTeardown.Capture(runtime);
            ColonyMachinePorts.Capture(runtime);ColonyPipeContactHeat.Capture(runtime);ColonyPipeBiology.Capture(runtime);runtime.PhaseEvents=ColonyConduits.PhaseEvents;
            var image=new ColonyConduitCheckpoint(ColonyConduits.Geometry,ColonyConduits.CatalogueIdentity(),cells.Cells,cells.Initial,cells.Supplied,cells.Removed,
                cells.Heat,cells.Diseases,boundary,runtime);
            ++Captures;return image.Encode();
        }
        // Load: after the owner restore, prefabs spawned and ONI's conduit
        // contents deserialized, before any owner frame publishes a receipt.
        internal static void Restore(byte[] bytes)
        {
            if(Active)throw new InvalidOperationException("Mixed pipes are already active before their saved image was restored.");
            ColonyConduits.StartRestoring();
            try
            {
                var catalogue=ColonyConduits.CatalogueIdentity();var geometry=ColonyConduits.Geometry;
                var image=ColonyConduitCheckpoint.Decode(bytes,geometry,catalogue,ColonyConduits.MaterialPhase);
                if(image.SchemaVersion!=3||image.Boundary==null||image.Runtime==null)throw new System.IO.InvalidDataException("Only a runtime (schema 3) pipe image can be restored.");
                ColonyConduits.Restore(image);
                ColonyPipePorts.Restore(image.Boundary,image.Runtime);
                ColonyPipePumps.StartControl();ColonyPipeVents.StartControl();ColonyPipeTeardown.StartControl();ColonyMachinePorts.StartControl();
                var notes="";
                try{ColonyPipeContactHeat.StartControl();}catch(NotSupportedException error){notes+="contact heat unavailable: "+error.Message+"; ";}
                try{ColonyPipeBiology.StartControl();}catch(NotSupportedException error){notes+="all-identity biology unavailable: "+error.Message+"; ";}
                StartNotes=notes;
                ColonyPipePumps.Restore(image.Runtime);ColonyPipeVents.Restore(image.Runtime);ColonyPipeTeardown.Restore(image.Runtime);
                ColonyMachinePorts.Restore(image.Runtime);ColonyPipeContactHeat.Restore(image.Runtime);ColonyPipeBiology.Restore(image.Runtime);
                ColonyConduits.PhaseEvents=image.Runtime.PhaseEvents;
                ValidateOwnership(image.Runtime,image.Boundary);
                ++Restores;
            }
            catch{ColonyConduits.Reset();throw;}
        }
        // Every pump/vent/spill identity names a live buffer or pending job,
        // and no buffer or job is owned twice.
        private static void ValidateOwnership(PipeRuntimeImage runtime,PipeBoundaryState boundary)
        {
            var known=new HashSet<ulong>(runtime.Buffers.Select(b=>b.Id));foreach(var job in boundary.Jobs)known.Add(job.LocalId);
            var claimed=new HashSet<ulong>();
            void Claim(ulong id,string owner){if(!known.Contains(id)||!claimed.Add(id))throw new System.IO.InvalidDataException($"Saved {owner} names an unknown or shared pipe buffer {id}.");}
            foreach(var pump in runtime.Pumps){foreach(var id in pump.Buffers)Claim(id,"pump");foreach(var item in pump.Pending)if(!pump.Buffers.Contains(item.Id))throw new System.IO.InvalidDataException("Saved pump reservation has no buffer.");}
            foreach(var vent in runtime.Vents){foreach(var id in vent.Buffers)Claim(id,"vent");foreach(var id in vent.Pending)if(!vent.Buffers.Contains(id))throw new System.IO.InvalidDataException("Saved vent reservation has no buffer.");}
            foreach(var spill in runtime.Spills)foreach(var id in spill.Ids)Claim(id,"spill");
        }
        // Set by the save writer once it captures this runtime inside its
        // quiescence barrier; until then active mixed pipes refuse saving.
        internal static bool Persistable { get; set; }
        internal static long Captures { get; private set; }
        internal static long Restores { get; private set; }
        internal static string Report=>$"mixed pipes {(Active?"active":"inactive")}; {ColonyPipeContactHeat.Report}; biology {ColonyPipeBiology.Report}; {ColonyConduits.PhaseEvents} phase events"+(StartNotes.Length>0?"; "+StartNotes:"");
    }
}
