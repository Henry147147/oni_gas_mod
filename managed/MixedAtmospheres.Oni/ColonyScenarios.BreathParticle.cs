using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using HarmonyLib;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    internal static partial class ColonyScenarios
    {
        // Current whole-colony route, independent of the retired nitrogen fixture.
        // This diagnoses one real breath parcel; it does not replace the fixed
        // whole-world breathing assertion or certify a regional energy balance.
        private const float BreathParcelMass = .03125f, BreathParcelTemperature = 310;
        private static CO2? breathParcel;
        private static bool breathParcelStarted, breathParcelRetired;
        private static int breathParcelRetiredFrame;
        private static double breathParcelBefore;
        private static ulong breathParcelSourceFrame, breathParcelSession;
        [StructLayout(LayoutKind.Sequential)]
        private struct BreathSampleCell
        { internal uint PublicCell, First, Count, Flags; internal double Temperature; }
        [StructLayout(LayoutKind.Sequential)]
        private struct BreathSamplePart
        { internal uint NativeIndex, Reserved; internal double Mass; }
        [DllImport("mixed_atmospheres_oni", EntryPoint="ma_oni_colony_sample", CallingConvention=CallingConvention.Cdecl)]
        private static extern int SampleBreathArena([In] uint[] cells, uint count, out OniNativeColony.Status source,
            [Out] BreathSampleCell[] rows, [Out] BreathSamplePart[] parts, uint capacity, out uint partCount, out double bookedMass);
        private static List<CO2> LiveBreathParcels() =>
            AccessTools.Field(typeof(CO2Manager),"co2Items").GetValue(CO2Manager.instance) as List<CO2>
                ?? throw new InvalidOperationException("The native breath particle pool is unavailable.");
        private static double BreathArenaCarbon(out OniNativeColony.Status source)
        {
            if(Marshal.SizeOf<BreathSampleCell>()!=24 || Marshal.SizeOf<BreathSamplePart>()!=16)
                throw new InvalidOperationException("Whole-colony sample ABI differs.");
            var cells=Enumerable.Range(0,Width).SelectMany(x=>Enumerable.Range(0,Height).Select(y=>(uint)Cell(x,y))).OrderBy(c=>c).ToArray();
            var rows=new BreathSampleCell[cells.Length];var parts=new BreathSamplePart[cells.Length*128];
            OniNativeColony.Check(SampleBreathArena(cells,(uint)cells.Length,out source,rows,parts,(uint)parts.Length,out var count,out _));
            var carbon=(uint)ElementLoader.FindElementByHash(SimHashes.CarbonDioxide).idx;
            if(count>parts.Length)throw new InvalidOperationException("Whole-colony breath sample exceeds its capacity.");
            double mass=0;uint next=0;
            for(var i=0;i<rows.Length;++i)
            {
                var row=rows[i];
                if(row.PublicCell!=cells[i] || row.Flags!=1 || row.First!=next || next>count || row.Count>count-next)
                    throw new InvalidOperationException("Whole-colony breath sample is incomplete or refused.");
                for(uint k=row.First;k<row.First+row.Count;++k)
                {
                    var part=parts[k];
                    if(part.Reserved!=0 || !(part.Mass>0) || double.IsInfinity(part.Mass))
                        throw new InvalidOperationException("Whole-colony breath sample has an invalid constituent.");
                    if(part.NativeIndex==carbon)mass+=part.Mass;
                }
                next+=row.Count;
            }
            if(next!=count)throw new InvalidOperationException("Whole-colony breath sample has unused constituents.");
            return mass;
        }
        private static void BreathParticle(int frame)
        {
            if(Once(0,"sealed oxygen arena"))
            {
                breathParcel=null;breathParcelStarted=breathParcelRetired=false;breathParcelRetiredFrame=-1;
                Arena((x,y)=>(SimHashes.Oxygen,1f,300f));
            }
            if(Once(6,"native breath parcel"))
            {
                breathParcelBefore=BreathArenaCarbon(out var source);
                if(breathParcelBefore!=0)throw new InvalidOperationException("The isolated breath arena already contains carbon dioxide.");
                breathParcelSourceFrame=source.CompletedFrames;breathParcelSession=source.Session;
                var manager=CO2Manager.instance ?? throw new InvalidOperationException("The native breath manager is unavailable.");
                var particles=LiveBreathParcels();var before=particles.Count;
                manager.SpawnBreath(Grid.CellToPosCCC(Cell(3,2),Grid.SceneLayer.Front),BreathParcelMass,BreathParcelTemperature,false);
                if(particles.Count!=before+1)throw new InvalidOperationException("One native breath did not create exactly one particle.");
                breathParcel=particles[particles.Count-1];
                Record("native breath retains its exact declared parcel",BitConverter.SingleToInt32Bits(breathParcel.mass)==BitConverter.SingleToInt32Bits(BreathParcelMass) && breathParcel.temperature==BreathParcelTemperature,
                    $"{breathParcel.mass:R} kg, {breathParcel.temperature:R} K");
                breathParcelStarted=true;
            }
            if(Once(40,"whole-colony deposition and retirement"))
            {
                var after=BreathArenaCarbon(out var source);var delta=after-breathParcelBefore;
                Record("one native breath deposits its carbon dioxide in the owner",source.Session==breathParcelSession && source.CompletedFrames>breathParcelSourceFrame && Math.Abs(delta-BreathParcelMass)<=1e-10,
                    $"{breathParcelBefore:R} -> {after:R} kg; expected {(double)BreathParcelMass:R}; session {source.Session}, frames {breathParcelSourceFrame}/{source.CompletedFrames}");
                Record("the native breath particle retires from its pool",breathParcelRetired && breathParcelRetiredFrame>0,
                    $"retired {breathParcelRetired}, observed after CO2Manager.Sim33ms at Unity frame {breathParcelRetiredFrame}");
                breathParcelStarted=false;Finish();
            }
        }
        internal static void ObserveWholeColonyBreathRetirement()
        {
            if(!breathParcelStarted || breathParcelRetired || Current.Name!="breath-particle")return;
            if(breathParcel==null || !LiveBreathParcels().Contains(breathParcel))
            {breathParcelRetired=true;breathParcelRetiredFrame=Time.frameCount;}
        }
    }
    [HarmonyPatch(typeof(CO2Manager),nameof(CO2Manager.Sim33ms))]
    internal static class WholeColonyBreathRetirementPatch
    {
        private static bool Prepare()=>ColonyRuntimeValidation.Settings?.Scenario?.Split(',').Contains("breath-particle")==true;
        private static void Postfix()=>ColonyScenarios.ObserveWholeColonyBreathRetirement();
    }
}
