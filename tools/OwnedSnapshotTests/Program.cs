using System;
using System.Collections.Generic;
using System.Buffers.Binary;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using MixedAtmospheres.Oni;

// Synthetic memory ownership tests compiled against the actual locally licensed
// declarations. No Unity singleton, native simulator, or game code is started.
internal static unsafe class Program
{
    private static int passed;
    private static int Main(string[] args)
    {
        if (args.Length is < 1 or > 2 || (args.Length == 2 && args[1] != "--frame-coordinator-benchmark"))
            throw new ArgumentException("Usage: OwnedSnapshotTests <licensed ONI root> [--frame-coordinator-benchmark]");
        var managed = Path.Combine(Path.GetFullPath(args[0]), "OxygenNotIncluded_Data", "Managed");
        AssemblyLoadContext.Default.Resolving += (_, name) =>
        {
            var candidate = Path.Combine(managed, name.Name + ".dll");
            return File.Exists(candidate) ? AssemblyLoadContext.Default.LoadFromAssemblyPath(candidate) : null;
        };
        return Run(args.Length == 2);
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static int Run(bool benchmark)
    {
        Test("world passive observations retain all IDs, tiny values and exact publication labels", () =>
        {
            Check(Marshal.SizeOf<WorldPassiveStamp>()==32 && Marshal.SizeOf<WorldPassiveRow>()==24 &&
                Marshal.OffsetOf<WorldPassiveStamp>(nameof(WorldPassiveStamp.Session)).ToInt32()==8 &&
                Marshal.OffsetOf<WorldPassiveRow>(nameof(WorldPassiveRow.Quantity)).ToInt32()==16);
            var original=new uint[]{26,35,44};var calls=0;
            var values=new[]{new WorldPassiveRow{PublicCell=35,Identity=1,Quantity=double.Epsilon},
                new WorldPassiveRow{PublicCell=35,Identity=2,Quantity=1e-240},new WorldPassiveRow{PublicCell=35,Identity=4,Quantity=1e30},
                new WorldPassiveRow{PublicCell=35,Identity=0x100000000,Quantity=.5}};
            int Read(ulong session,ulong frame,uint[] cells,uint n,ref WorldPassiveStamp stamp,WorldPassiveRow[]? rows,uint capacity,out uint count)
            {
                ++calls;Check(n==3 && cells.SequenceEqual(new uint[]{26,35,44}));
                if(rows!=null){Check(session==7 && frame==42 && capacity==4);values.CopyTo(rows,0);}
                else Check(session==0);
                stamp=new WorldPassiveStamp{Size=32,Version=1,Session=7,CompletedFrame=42,TimeSeconds=8.4};count=4;return 0;
            }
            Check(ColonyWorldPassives.Read(Read,0,0,original,out var snapshot)==0 && calls==2 && snapshot!=null);
            Check(snapshot!.Rows.Count==4 && snapshot.Source.Session==7 && snapshot.Source.CompletedFrame==42 &&
                BitConverter.DoubleToInt64Bits(snapshot.Rows[0].Quantity)==1 && snapshot.Rows[1].Quantity==1e-240 && snapshot.Rows[2].Quantity==1e30);
            original[0]=0;Check(snapshot.Cells[0]==26);
        });
        Test("advancing world passive publications never expose a partial snapshot", () =>
        {
            foreach(var error in new[]{1,2,3,5,6})
            {
                int Read(ulong session,ulong frame,uint[] cells,uint n,ref WorldPassiveStamp stamp,WorldPassiveRow[]? rows,uint capacity,out uint count)
                {count=0;if(rows==null){stamp=new WorldPassiveStamp{Size=32,Version=1,Session=7,CompletedFrame=42,TimeSeconds=8.4};count=1;return 0;}return error;}
                Check(ColonyWorldPassives.Read(Read,0,0,new uint[]{35},out var snapshot)==error && snapshot==null);
            }
            int Empty(ulong session,ulong frame,uint[] cells,uint n,ref WorldPassiveStamp stamp,WorldPassiveRow[]? rows,uint capacity,out uint count)
            {Check(rows==null);stamp=new WorldPassiveStamp{Size=32,Version=1,Session=7,CompletedFrame=0,TimeSeconds=0};count=0;return 0;}
            Check(ColonyWorldPassives.Read(Empty,7,0,new uint[]{35},out var empty)==0 && empty!=null && empty.Rows.Count==0);
        });
        Test("world passive readers reject corrupt rows and mismatched epochs before publication", () =>
        {
            foreach(var bad in new[]{new WorldPassiveRow{PublicCell=35,Identity=1,Quantity=0},new WorldPassiveRow{PublicCell=35,Identity=1,Quantity=-1},
                new WorldPassiveRow{PublicCell=35,Identity=1,Quantity=double.NaN},new WorldPassiveRow{PublicCell=35,Identity=1,Quantity=double.PositiveInfinity},
                new WorldPassiveRow{PublicCell=35,Identity=0,Quantity=1},new WorldPassiveRow{PublicCell=36,Identity=1,Quantity=1},
                new WorldPassiveRow{PublicCell=35,Reserved=1,Identity=1,Quantity=1}})
            {
                int Read(ulong session,ulong frame,uint[] cells,uint n,ref WorldPassiveStamp stamp,WorldPassiveRow[]? rows,uint capacity,out uint count)
                {stamp=new WorldPassiveStamp{Size=32,Version=1,Session=7,CompletedFrame=42,TimeSeconds=8.4};count=1;if(rows!=null)rows[0]=bad;return 0;}
                var refused=false;try{ColonyWorldPassives.Read(Read,0,0,new uint[]{35},out _);}catch(InvalidDataException){refused=true;}Check(refused);
            }
            foreach(var changed in new[]{"session","frame","time","count","order"})
            {
                int Read(ulong session,ulong frame,uint[] cells,uint n,ref WorldPassiveStamp stamp,WorldPassiveRow[]? rows,uint capacity,out uint count)
                {
                    stamp=new WorldPassiveStamp{Size=32,Version=1,Session=7,CompletedFrame=42,TimeSeconds=8.4};count=2;
                    if(rows!=null)
                    {
                        rows[0]=new WorldPassiveRow{PublicCell=35,Identity=1,Quantity=1};rows[1]=new WorldPassiveRow{PublicCell=35,Identity=2,Quantity=1};
                        if(changed=="session")stamp.Session=8;if(changed=="frame")stamp.CompletedFrame=43;
                        if(changed=="time")stamp.TimeSeconds=8.5;if(changed=="count")count=1;if(changed=="order")rows[1].Identity=1;
                    }
                    return 0;
                }
                var refused=false;try{ColonyWorldPassives.Read(Read,7,42,new uint[]{35},out _);}catch(InvalidDataException){refused=true;}Check(refused);
            }
        });
        Test("legacy world germ checks allow stored-order ties and ignore non-disease passives", () =>
        {
            WorldPassiveSnapshot Snapshot(params WorldPassiveRow[] rows)=>new WorldPassiveSnapshot(
                new WorldPassiveStamp{Size=32,Version=1,Session=1},new uint[]{35},rows);
            var tied=Snapshot(new WorldPassiveRow{PublicCell=35,Identity=1,Quantity=3.9},
                new WorldPassiveRow{PublicCell=35,Identity=2,Quantity=3.9},
                new WorldPassiveRow{PublicCell=35,Identity=0x100000000,Quantity=1e30});
            ColonyWorldPassives.ValidateLegacyProjection(tied,35,1,3);
            ColonyWorldPassives.ValidateLegacyProjection(tied,35,2,3);
            ColonyWorldPassives.ValidateLegacyProjection(Snapshot(new WorldPassiveRow{PublicCell=35,Identity=1,Quantity=.49}),35,0,0);
            ColonyWorldPassives.ValidateLegacyProjection(Snapshot(new WorldPassiveRow{PublicCell=35,Identity=1,Quantity=.5}),35,1,0);
            ColonyWorldPassives.ValidateLegacyProjection(Snapshot(new WorldPassiveRow{PublicCell=35,Identity=1,Quantity=1e30}),35,1,int.MaxValue);
            foreach(var invalid in new[]{(0u,0),(1u,4),(3u,3)})
            {
                var refused=false;try{ColonyWorldPassives.ValidateLegacyProjection(tied,35,invalid.Item1,invalid.Item2);}
                catch(InvalidDataException){refused=true;}Check(refused);
            }
        });
        Test("publication geometry maps offset rectangles and same-count shape changes", () =>
        {
            Check(ColonyPublicationMap.Create(6,5,2,1,3,2).SequenceEqual(new[]{8,9,10,14,15,16}));
            Check(ColonyPublicationMap.Create(6,5,1,0,2,3).SequenceEqual(new[]{1,2,7,8,13,14}));
            Check(ColonyPublicationMap.Create(7,4,0,0,7,4).SequenceEqual(Enumerable.Range(0,28)));
            Check(ColonyPublicationMap.Create(6,5,5,4,1,1).SequenceEqual(new[]{29}));
            foreach(var rectangle in new[]{(0,5,0,0,1,1),(6,5,-1,0,1,1),(6,5,0,-1,1,1),(6,5,0,0,0,1),
                (6,5,4,0,3,1),(6,5,0,4,1,2),(int.MaxValue,2,0,0,1,1)})
            {
                var refused=false;
                try{_=ColonyPublicationMap.Create(rectangle.Item1,rectangle.Item2,rectangle.Item3,rectangle.Item4,rectangle.Item5,rectangle.Item6);}
                catch(ArgumentOutOfRangeException){refused=true;}Check(refused);
            }
        });
        Test("moving parcel coordinates retain the native ABI and explicit potential reference", () =>
        {
            Check(Marshal.SizeOf<ColonyParcelContext>()==72 &&
                Marshal.OffsetOf<ColonyParcelContext>(nameof(ColonyParcelContext.Session)).ToInt32()==16 &&
                Marshal.OffsetOf<ColonyParcelContext>(nameof(ColonyParcelContext.CellWidthMetres)).ToInt32()==48);
            var context=ColonyParcelContext.Empty;Check(context.Size==72 && context.Version==1 && context.Reserved==0);
        });
        Test("quit cleanup preserves native instructions and only guards its two destroyed-root reads", () =>
        {
            var getter=HarmonyLib.AccessTools.PropertyGetter(typeof(UnityEngine.Component),"gameObject");
            var safe=HarmonyLib.AccessTools.Method(typeof(ColonyQuitCleanup),nameof(ColonyQuitCleanup.ExistingGameObject));
            var original=HarmonyLib.PatchProcessor.GetOriginalInstructions(HarmonyLib.AccessTools.Method(typeof(Game),"StopBE")).ToArray();
            var transformed=ColonyQuitCleanup.Transpiler(original.Select(i=>new HarmonyLib.CodeInstruction(i))).ToArray();
            Check(transformed.Length==original.Length);var changed=0;
            for(var i=0;i<original.Length;++i)
            {
                var from=original[i];var to=transformed[i];
                Check(from.labels.SequenceEqual(to.labels) && from.blocks.SequenceEqual(to.blocks));
                if(Equals(from.operand,getter)) { Check(to.opcode==System.Reflection.Emit.OpCodes.Call && Equals(to.operand,safe));++changed; }
                else Check(from.opcode==to.opcode && Equals(from.operand,to.operand));
            }
            Check(changed==2);
            foreach(var count in new[]{0,1,3})
            {
                var refused=false;
                try { _=ColonyQuitCleanup.Transpiler(Enumerable.Range(0,count).Select(_=>new HarmonyLib.CodeInstruction(System.Reflection.Emit.OpCodes.Callvirt,getter))).ToArray(); }
                catch(NotSupportedException) { refused=true; }
                Check(refused);
            }
        });
        Test("owner windows count newly advanced frames at zero, high restores and the integer limit", () =>
        {
            var window=new ColonyFrameWindow();
            foreach(var start in new[]{0UL,93UL,300000UL,ulong.MaxValue-30})
            {
                window.Reset();window.Start(start);window.Start(start+1);
                Check(window.Baseline==start && !window.HasReached(start,30) && !window.HasReached(start+29,30));
                Check(window.HasReached(start+30,30));
                if(start>0) Check(!window.HasReached(start-1,30));
            }
        });
        Test("owner menu reload retires sessions and preserves source, profile and colony identity", () =>
        {
            var profile=new ColonyPhysicalProfile(8,21,1,400,800);
            var missing=false;try{new ColonyMenuReload().Begin(7,5,"colony","hash",null,"image");}catch(InvalidDataException){missing=true;}Check(missing);
            var flow=new ColonyMenuReload();flow.Begin(7,5,"colony","hash",profile,"image");
            var refused=false;try{flow.MainMenu(false);}catch(InvalidDataException){refused=true;}Check(refused);
            flow.Shutdown();refused=false;try{flow.MainMenu(true);}catch(InvalidDataException){refused=true;}Check(refused);
            flow.MainMenu(false);
            foreach(var value in new[]{(7UL,5U,"colony","hash",2,profile,"image"),(8UL,3U,"colony","hash",2,profile,"image"),(8UL,5U,"other","hash",2,profile,"image"),
                (8UL,5U,"colony","changed",2,profile,"image"),(8UL,5U,"colony","hash",1,profile,"image"),(8UL,5U,"colony","hash",2,new ColonyPhysicalProfile(8,21,1,400,200),"image"),
                (8UL,5U,"colony","hash",2,new ColonyPhysicalProfile(8,20,0,200,200),"image"),(8UL,5U,"colony","hash",2,profile,"other")})
            {refused=false;try{flow.Complete(value.Item1,value.Item2,value.Item3,value.Item4,value.Item5,value.Item6,value.Item7);}catch(InvalidDataException){refused=true;}Check(refused&&!flow.Verified);}
            refused=false;try{flow.Complete(8,5,"colony","hash",2,null,"image");}catch(InvalidDataException){refused=true;}Check(refused&&!flow.Verified);
            flow.Complete(8,5,"colony","hash",2,profile,"image");Check(flow.Verified&&flow.ShutdownObserved&&flow.MainMenuObserved);
        });
        Test("checkpoint copies retain complete images when queued input changes the queried size", () =>
        {
            var expected=new byte[]{7,13,29};
            void NativeCheck(int result){if(result!=0)throw new IOException("native result "+result);}
            foreach(var queried in new uint[]{1,8})
            {
                var copied=0;
                int Copy(byte[]? buffer,uint capacity,out uint bytes)
                {
                    bytes=buffer==null?queried:(uint)expected.Length;
                    if(buffer==null)return 0;
                    ++copied;
                    if(capacity<bytes)return 5;
                    expected.CopyTo(buffer,0);return 0;
                }
                Check(ColonyCheckpointCopy.Read(Copy,NativeCheck).SequenceEqual(expected));
                Check(copied==(queried<expected.Length?2:1));
            }
            var attempts=0;
            int Growing(byte[]? buffer,uint capacity,out uint bytes){++attempts;bytes=capacity+1;return buffer==null?0:5;}
            var refused=false;try{ColonyCheckpointCopy.Read(Growing,NativeCheck);}catch(InvalidOperationException){refused=true;}
            Check(refused&&attempts==9);
            int Failed(byte[]? buffer,uint capacity,out uint bytes){bytes=3;return buffer==null?0:3;}
            refused=false;try{ColonyCheckpointCopy.Read(Failed,NativeCheck);}catch(IOException error){refused=error.Message=="native result 3";}
            Check(refused);
            int Invalid(byte[]? buffer,uint capacity,out uint bytes){bytes=capacity+1;return 0;}
            refused=false;try{ColonyCheckpointCopy.Read(Invalid,NativeCheck);}catch(InvalidDataException){refused=true;}
            Check(refused);
        });
        Test("save frame metadata follows durable image across reservoir and trace framing", () =>
        {
            const ulong durable=0x100000002;
            foreach(var version in new uint[]{6,7,8,9,10,11,12})
            {
                using var stream=new MemoryStream();using var writer=new BinaryWriter(stream);
                writer.Write(System.Text.Encoding.ASCII.GetBytes("MACOLN01"));writer.Write(version);writer.Write(new byte[56]);
                writer.Write(2u);writer.Write(new byte[2*168]);writer.Write(9u);writer.Write(new byte[9]);
                writer.Write(9u);writer.Write(new byte[9]);writer.Write(3u);writer.Write(new byte[3*32]);
                writer.Write(new byte[(version>=7?6*2+8:5*2+5)*16]);if(version>=7)writer.Write(new byte[16]);
                writer.Write(durable);var image=stream.ToArray();
                Check(ColonyCheckpointMetadata.CompletedFrames(image)==durable);
                foreach(var length in new[]{0,8,12,68,image.Length-1})
                {
                    var rejected=false;try{ColonyCheckpointMetadata.CompletedFrames(image.AsSpan(0,length));}catch(InvalidDataException){rejected=true;}
                    Check(rejected);
                }
                var corrupt=(byte[])image.Clone();BinaryPrimitives.WriteUInt32LittleEndian(corrupt.AsSpan(68,4),uint.MaxValue);
                var refused=false;try{ColonyCheckpointMetadata.CompletedFrames(corrupt);}catch(InvalidDataException){refused=true;}Check(refused);
            }
        });
        Test("material admission metadata preserves canonical terms and native catalogue order", () =>
        {
            foreach(var version in new uint[]{6,7,8,9,10,11,12})
            {
                using var stream=new MemoryStream();using var writer=new BinaryWriter(stream);
                writer.Write(System.Text.Encoding.ASCII.GetBytes("MACOLN01"));writer.Write(version);writer.Write(new byte[56]);writer.Write(2u);
                // Saved caller order differs from the sorted runtime catalogue.
                foreach(var pair in new[]{(hash:222,index:900u),(hash:111,index:100u)})
                {
                    var material=new byte[168];BinaryPrimitives.WriteInt32LittleEndian(material.AsSpan(8,4),pair.hash);
                    BinaryPrimitives.WriteUInt32LittleEndian(material.AsSpan(12,4),pair.index);writer.Write(material);
                }
                writer.Write(9u);writer.Write(new byte[9]);writer.Write(9u);writer.Write(new byte[9]);writer.Write(3u);writer.Write(new byte[3*32]);
                var ledger=(int)stream.Position;writer.Write(new byte[(version>=7?6*2+8:5*2+5)*16]);if(version>=7)writer.Write(new byte[16]);
                writer.Write(73UL);var image=stream.ToArray();
                ColonyCheckpointMetadata.RefuseRemovedNitrogen(image);
                foreach (var hash in new[] { -1109340048, 1078696428, 418600943 })
                {
                    var removed = (byte[])image.Clone();
                    BinaryPrimitives.WriteInt32LittleEndian(removed.AsSpan(12 + 56 + 4 + 168 + 8, 4), hash);
                    var refused = false;
                    try { ColonyCheckpointMetadata.RefuseRemovedNitrogen(removed); }
                    catch (NotSupportedException error) { refused = error.Message.Contains("removed nitrogen"); }
                    Check(refused);
                }

                const double tiny=1.3877787807814457e-17;
                void Term(int ordinal,double high,double low)
                {
                    BinaryPrimitives.WriteInt64LittleEndian(image.AsSpan(ledger+(2+ordinal)*16,8),BitConverter.DoubleToInt64Bits(high));
                    BinaryPrimitives.WriteInt64LittleEndian(image.AsSpan(ledger+(2+ordinal)*16+8,8),BitConverter.DoubleToInt64Bits(low));
                }
                Term(0,3.5,tiny);Term(1,.125,-tiny);var unchanged=(byte[])image.Clone();
                Check(ColonyCheckpointMetadata.InjectedMass(image,111)==(3.5,tiny));
                Check(ColonyCheckpointMetadata.InjectedMass(image,222)==(.125,-tiny));
                Check(ColonyCheckpointMetadata.CompletedFrames(image)==73&&image.SequenceEqual(unchanged));
                void Refuse(System.Action action){var refused=false;try{action();}catch(InvalidDataException){refused=true;}Check(refused);}
                Refuse(()=>ColonyCheckpointMetadata.InjectedMass(image,333));
                foreach(var length in new[]{0,8,12,68,72+168-1,ledger-1,ledger+2*16+15})
                    Refuse(()=>ColonyCheckpointMetadata.InjectedMass(image.AsSpan(0,length),111));
                var corrupt=(byte[])image.Clone();BinaryPrimitives.WriteUInt32LittleEndian(corrupt.AsSpan(68,4),uint.MaxValue);
                Refuse(()=>ColonyCheckpointMetadata.InjectedMass(corrupt,111));
                if(version<8)continue;
                // Wrapper13 (callback-owned requests) encloses the same base framing.
                byte[] Wrapped(uint wrapper,uint basis,uint requests)
                {
                    var head=new byte[20];image.AsSpan(0,8).CopyTo(head);BinaryPrimitives.WriteUInt32LittleEndian(head.AsSpan(8),wrapper);
                    BinaryPrimitives.WriteUInt32LittleEndian(head.AsSpan(12),basis);BinaryPrimitives.WriteUInt32LittleEndian(head.AsSpan(16),requests);
                    return head.Concat(image.Skip(12)).ToArray();
                }
                var wrapped=Wrapped(13,version,2);
                Check(ColonyCheckpointMetadata.InjectedMass(wrapped,111)==(3.5,tiny)&&ColonyCheckpointMetadata.CompletedFrames(wrapped)==73&&
                    ColonyCheckpointMetadata.OwnerVersion(wrapped)==version);
                Refuse(()=>ColonyCheckpointMetadata.CompletedFrames(Wrapped(13,version,0)));
                Refuse(()=>ColonyCheckpointMetadata.CompletedFrames(Wrapped(13,13,1)));
                Refuse(()=>ColonyCheckpointMetadata.CompletedFrames(Wrapped(13,7,1)));
                Refuse(()=>ColonyCheckpointMetadata.CompletedFrames(Wrapped(14,version,1)));
            }
        });
        Test("sectioned colony payload keeps legacy bytes without sections and refuses malformed sections", () =>
        {
            var config=Enumerable.Range(0,56).Select(i=>(byte)i).ToArray();var owner=new byte[]{9,8,7,6,5};
            var legacy=ColonySavePayload.Encode(config,owner);
            var expected=System.Text.Encoding.ASCII.GetBytes("MACOLP01").Concat(BitConverter.GetBytes(56)).Concat(config).Concat(owner).ToArray();
            Check(legacy.SequenceEqual(expected) && ColonySavePayload.Encode(config,owner,new KeyValuePair<uint,byte[]>[0]).SequenceEqual(expected));
            var old=ColonySavePayload.Decode(legacy);Check(old.Config.SequenceEqual(config) && old.Owner.SequenceEqual(owner) && old.Sections.Count==0);
            var pipes=new byte[]{1,2,3};var particles=new byte[]{4};
            var sectioned=ColonySavePayload.Encode(config,owner,new[]{new KeyValuePair<uint,byte[]>(2,particles),new KeyValuePair<uint,byte[]>(1,pipes)});
            Check(sectioned.AsSpan(0,8).SequenceEqual(System.Text.Encoding.ASCII.GetBytes("MACOLP03")));
            var parsed=ColonySavePayload.Decode(sectioned);
            Check(parsed.Owner.SequenceEqual(owner) && parsed.Config.SequenceEqual(config) && parsed.Section(1)!.SequenceEqual(pipes) && parsed.Section(2)!.SequenceEqual(particles));
            Check(ColonySavePayload.Encode(parsed.Config,parsed.Owner,parsed.Sections).SequenceEqual(sectioned));
            var single=ColonySavePayload.Encode(config,owner,new[]{new KeyValuePair<uint,byte[]>(2,particles)});
            Check(ColonySavePayload.Decode(single).Section(1)==null && ColonySavePayload.Decode(single).Section(2)!.SequenceEqual(particles));
            void Refuse(System.Action action){var refused=false;try{action();}catch(InvalidDataException){refused=true;}catch(NotSupportedException){refused=true;}Check(refused);}
            // Section 3 is the colony settings record (docs/settings.md); 4 the
            // callback-owned request actors (MACCBR01); 5 the native loop
            // record (MACNRC01); 6 is unknown.
            var settings=new byte[]{7,7};
            var three=ColonySavePayload.Encode(config,owner,new[]{new KeyValuePair<uint,byte[]>(3,settings),new KeyValuePair<uint,byte[]>(1,pipes),new KeyValuePair<uint,byte[]>(2,particles)});
            var threeParsed=ColonySavePayload.Decode(three);
            Check(threeParsed.Sections.Count==3 && threeParsed.Section(3)!.SequenceEqual(settings) && ColonySavePayload.Encode(config,owner,threeParsed.Sections).SequenceEqual(three));
            var four=ColonySavePayload.Encode(config,owner,new[]{new KeyValuePair<uint,byte[]>(4,pipes),new KeyValuePair<uint,byte[]>(3,settings),
                new KeyValuePair<uint,byte[]>(1,pipes),new KeyValuePair<uint,byte[]>(2,particles)});
            var fourParsed=ColonySavePayload.Decode(four);
            Check(fourParsed.Sections.Count==4 && fourParsed.Section(4)!.SequenceEqual(pipes) && ColonySavePayload.Encode(config,owner,fourParsed.Sections).SequenceEqual(four));
            var five=ColonySavePayload.Encode(config,owner,fourParsed.Sections.Append(new KeyValuePair<uint,byte[]>(ColonySavePayload.NativeRuntimeClock,particles)));
            var fiveParsed=ColonySavePayload.Decode(five);
            Check(fiveParsed.Sections.Count==5&&fiveParsed.Section(5)!.SequenceEqual(particles)&&ColonySavePayload.Encode(config,owner,fiveParsed.Sections).SequenceEqual(five));
            Refuse(()=>ColonySavePayload.Encode(config,owner,new[]{new KeyValuePair<uint,byte[]>(6,pipes)}));
            Refuse(()=>ColonySavePayload.Encode(config,owner,new[]{new KeyValuePair<uint,byte[]>(1,Array.Empty<byte>())}));
            Refuse(()=>ColonySavePayload.Encode(config,owner,new[]{new KeyValuePair<uint,byte[]>(1,pipes),new KeyValuePair<uint,byte[]>(1,pipes)}));
            Refuse(()=>ColonySavePayload.Encode(new byte[55],owner));
            Refuse(()=>ColonySavePayload.Encode(config,Array.Empty<byte>()));
            var dormant=(byte[])legacy.Clone();dormant[7]=(byte)'2';Refuse(()=>ColonySavePayload.Decode(dormant));
            Refuse(()=>ColonySavePayload.Decode(sectioned.Concat(new byte[]{0}).ToArray()));
            for(var cut=1;cut<sectioned.Length-64;cut+=7) Refuse(()=>ColonySavePayload.Decode(sectioned.AsSpan(0,sectioned.Length-cut)));
            var at=8+4+56+4+owner.Length;
            byte[] Mutate(int offset,int value){var copy=(byte[])sectioned.Clone();BinaryPrimitives.WriteInt32LittleEndian(copy.AsSpan(offset),value);return copy;}
            Refuse(()=>ColonySavePayload.Decode(Mutate(at,0)));                     // no sections
            Refuse(()=>ColonySavePayload.Decode(Mutate(at,6)));                     // too many sections
            Refuse(()=>ColonySavePayload.Decode(Mutate(at+4,2)));                   // out of order (2 then 2)
            Refuse(()=>ColonySavePayload.Decode(Mutate(at+4,6)));                   // unknown tag
            Refuse(()=>ColonySavePayload.Decode(Mutate(at,4)));                     // more sections than present
            Refuse(()=>ColonySavePayload.Decode(Mutate(at+8,0)));                   // empty section
            Refuse(()=>ColonySavePayload.Decode(Mutate(8+4+56,int.MaxValue)));      // owner length
            Refuse(()=>ColonySavePayload.Decode(Mutate(8,55)));                     // config length
        });
        Test("callback-owned request actors round-trip exactly and refuse malformed images", () =>
        {
            var entries=new List<ColonyCallbackEntry>{
                new ColonyCallbackEntry{Kind=ColonyCallbackRequestImage.Consumed,Target=new ColonyCallbackIdentity{Kind=1,Prefab=104,Ordinal=0,Type="OxygenBreather"},
                    Delegate=0,DeclaringType="GasBreatherFromWorldProvider",Method="OnSimConsumeCallback",Debug="GasBreatherFromWorldProvider"},
                new ColonyCallbackEntry{Kind=ColonyCallbackRequestImage.Released},
                new ColonyCallbackEntry{Kind=ColonyCallbackRequestImage.Actor,Target=new ColonyCallbackIdentity{Kind=1,Prefab=-7,Ordinal=2,Type="ElementConsumer"}},
                new ColonyCallbackEntry{Kind=ColonyCallbackRequestImage.Emitted,Target=new ColonyCallbackIdentity{Kind=2,Prefab=9,Ordinal=0,Type="GasAndLiquidConsumerMonitor+Instance"},
                    Delegate=2,DeclaringType="SteamTurbine+<>c",Method="<.cctor>b__62_0",Debug=""}};
            var image=ColonyCallbackRequestImage.Encode(entries);
            Check(image.AsSpan(0,8).SequenceEqual(System.Text.Encoding.ASCII.GetBytes("MACCBR01")));
            var back=ColonyCallbackRequestImage.Decode(image);
            Check(back.Count==4 && ColonyCallbackRequestImage.Encode(back).SequenceEqual(image));
            Check(back[0].Target!.CompareTo(entries[0].Target)==0 && back[0].Method=="OnSimConsumeCallback" && back[1].Target==null &&
                back[2].Target!.Prefab==-7 && back[2].Target!.Ordinal==2 && back[3].Delegate==2 && back[3].Target!.Kind==2);
            void Refuse(System.Action action){var refused=false;try{action();}catch(InvalidDataException){refused=true;}Check(refused);}
            Refuse(()=>ColonyCallbackRequestImage.Encode(new List<ColonyCallbackEntry>()));
            Refuse(()=>ColonyCallbackRequestImage.Encode(new[]{new ColonyCallbackEntry{Kind=ColonyCallbackRequestImage.Actor}}));
            Refuse(()=>ColonyCallbackRequestImage.Decode(image.Concat(new byte[]{0}).ToArray()));
            for(var cut=1;cut<image.Length;cut+=5) Refuse(()=>ColonyCallbackRequestImage.Decode(image.AsSpan(0,image.Length-cut).ToArray()));
            byte[] Set(int offset,byte value){var copy=(byte[])image.Clone();copy[offset]=value;return copy;}
            Refuse(()=>ColonyCallbackRequestImage.Decode(Set(7,(byte)'2')));      // magic
            Refuse(()=>ColonyCallbackRequestImage.Decode(Set(8,2)));              // version
            Refuse(()=>ColonyCallbackRequestImage.Decode(Set(16,9)));             // kind
            Refuse(()=>ColonyCallbackRequestImage.Decode(Set(17,3)));             // identity kind
        });
        if(Environment.GetEnvironmentVariable("MA_PRECISION_TEST_IMAGE") is string precisionImagePath)
            Test("actual native precision-holder checkpoint metadata", () =>
            {
                var image=File.ReadAllBytes(precisionImagePath);
                var profile=ColonyCheckpointMetadata.Profile(image);
                Check(profile.OwnerVersion==11 && profile.PhysicalVersion==23 && ColonyCheckpointMetadata.CompletedFrames(image)==1);
                Check(ColonyCheckpointMetadata.InjectedMass(image,111)==(Math.ScaleB(1,-54),0));
                Check(ColonyCheckpointMetadata.PhysicalImage(image).Slice(0,8).SequenceEqual(System.Text.Encoding.ASCII.GetBytes("MACCLS01")));
            });
        if(Environment.GetEnvironmentVariable("MA_AIRFLOW_TEST_IMAGE") is string airflowImagePath)
            Test("actual native conservative-airflow checkpoint metadata", () =>
            {
                var image=File.ReadAllBytes(airflowImagePath);var unchanged=(byte[])image.Clone();
                Check(ColonyCheckpointMetadata.Profile(image).Equals(new ColonyPhysicalProfile(12,23,1,400,800,8)));
                Check(ColonyCheckpointMetadata.CompletedFrames(image)==1 && image.SequenceEqual(unchanged));
            });
        Test("restored physical profile reads the saved law and caps through every owner section", () =>
        {
            byte[] Owner(uint version,uint physicalVersion,uint law,uint pressure,uint heat,bool jobs,double mixing=.5)
            {
                using var stream=new MemoryStream();using var writer=new BinaryWriter(stream);
                writer.Write(System.Text.Encoding.ASCII.GetBytes("MACOLN01"));writer.Write(version);writer.Write(new byte[56]);
                writer.Write(2u);writer.Write(new byte[2*168]);writer.Write(9u);writer.Write(new byte[9]);
                writer.Write(9u);writer.Write(new byte[9]);writer.Write(3u);writer.Write(new byte[3*32]);
                writer.Write(new byte[(version>=7?6*2+8:5*2+5)*16]);if(version>=7)writer.Write(new byte[16]);
                writer.Write(77UL);writer.Write(new byte[6*8+7*8]);
                writer.Write(2u);writer.Write(new byte[2*48]);writer.Write(1u);writer.Write(new byte[24]);
                writer.Write(3u);writer.Write(new byte[3*8]);writer.Write(1u);writer.Write(new byte[16]);
                if(version>=8)
                {
                    writer.Write(9UL);writer.Write(jobs?2u:0u);
                    if(jobs) for(var k=0;k<2;++k){writer.Write((uint)k);writer.Write(1UL+(ulong)k);writer.Write(2u);writer.Write(new byte[32]);if(version>=9){writer.Write(5u);writer.Write(new byte[5]);}}
                }
                if(version>=10)
                {
                    // World germs: mode, frames, 3 clocks, 2 queued edits, 1 account, audit error.
                    writer.Write(1u);writer.Write(40UL);writer.Write(3u);writer.Write(new byte[3*18]);
                    writer.Write(2u);writer.Write(new byte[2*16]);writer.Write(1u);writer.Write(new byte[8+6*16]);writer.Write(0.0);
                }
                if(version>=11) { writer.Write(12u);writer.Write(System.Text.Encoding.ASCII.GetBytes("MAPREC01"));writer.Write(0u); }
                using var physical=new MemoryStream();using(var p=new BinaryWriter(physical,System.Text.Encoding.UTF8,true))
                {
                    p.Write(System.Text.Encoding.ASCII.GetBytes("MACCLS01"));p.Write(physicalVersion);
                    if(physicalVersion>=20)
                    {
                        p.Write(3u);p.Write(new byte[3]);p.Write(new byte[8+8+4+4+8+8+8+8+1+8+8+4+8]);p.Write(mixing);p.Write(new byte[1+8+8]);
                        if(physicalVersion>=21){p.Write(law);p.Write(pressure);p.Write(heat);}
                    }
                    p.Write(new byte[40]);
                }
                writer.Write((ulong)physical.Length);writer.Write(physical.ToArray());writer.Write(new byte[64]);
                return stream.ToArray();
            }
            foreach(var version in new uint[]{6,7,8,9,10,11,12})
                foreach(var jobs in new[]{false,true})
                {
                    var core21=Owner(version,21,1,400,800,jobs);
                    Check(ColonyCheckpointMetadata.Profile(core21).Equals(new ColonyPhysicalProfile(version,21,1,400,800,.5)));
                    Check(ColonyCheckpointMetadata.Profile(Owner(version,22,1,400,800,jobs,8)).Equals(new ColonyPhysicalProfile(version,22,1,400,800,8)));
                    Check(!ColonyCheckpointMetadata.Profile(Owner(version,22,1,400,800,jobs,8)).Equals(new ColonyPhysicalProfile(version,22,1,400,800,.5)));
                    Check(ColonyCheckpointMetadata.Profile(Owner(version,23,1,400,800,jobs,8)).Equals(new ColonyPhysicalProfile(version,23,1,400,800,8)));
                    Check(ColonyCheckpointMetadata.Profile(Owner(version,24,1,400,800,jobs,8)).Equals(new ColonyPhysicalProfile(version,24,0,0,0)));
                    Check(ColonyCheckpointMetadata.CompletedFrames(core21)==77 && ColonyCheckpointMetadata.OwnerVersion(core21)==version);
                    var physical=ColonyCheckpointMetadata.PhysicalImage(core21);
                    Check(physical.Slice(0,8).SequenceEqual(System.Text.Encoding.ASCII.GetBytes("MACCLS01")) &&
                        core21.AsSpan(core21.Length-64-physical.Length,physical.Length).SequenceEqual(physical));
                    Check(ColonyCheckpointMetadata.Profile(Owner(version,21,1,65536,7,jobs)).Equals(new ColonyPhysicalProfile(version,21,1,65536,7,.5)));
                    Check(ColonyCheckpointMetadata.Profile(Owner(version,20,9,9,9,jobs)).Equals(new ColonyPhysicalProfile(version,20,0,200,200,.5)));
                    Check(ColonyCheckpointMetadata.Profile(Owner(version,19,9,9,9,jobs)).Equals(new ColonyPhysicalProfile(version,19,0,0,0)));
                    foreach(var cut in new[]{1,64,65,core21.Length/2})
                    {
                        var rejected=false;try{ColonyCheckpointMetadata.Profile(core21.AsSpan(0,core21.Length-cut));}catch(InvalidDataException){rejected=true;}
                        Check(rejected);
                    }
                    var longer=new byte[core21.Length+1];core21.CopyTo(longer,0);
                    var refused=false;try{ColonyCheckpointMetadata.Profile(longer);}catch(InvalidDataException){refused=true;}Check(refused);
                }
            var plain=Owner(12,23,1,400,800,true,8);
            var wrapped=new byte[plain.Length+8];
            plain.AsSpan(0,8).CopyTo(wrapped);
            BinaryPrimitives.WriteUInt32LittleEndian(wrapped.AsSpan(8),13);
            BinaryPrimitives.WriteUInt32LittleEndian(wrapped.AsSpan(12),12);
            BinaryPrimitives.WriteUInt32LittleEndian(wrapped.AsSpan(16),2);
            plain.AsSpan(12).CopyTo(wrapped.AsSpan(20));
            var actors=ColonyCallbackRequestImage.Encode(new[]{new ColonyCallbackEntry(),new ColonyCallbackEntry()});
            Check(ColonyCheckpointMetadata.Profile(wrapped).Equals(ColonyCheckpointMetadata.Profile(plain)) &&
                ColonyCheckpointMetadata.CompletedFrames(wrapped)==77 && ColonyCheckpointMetadata.CallbackRequestCount(wrapped)==2);
            ColonyCallbackRequestImage.ValidateOwner(wrapped,actors);
            ColonyCallbackRequestImage.ValidateOwner(plain,null);
            void RefuseActors(byte[] image,byte[]? section)
            {
                var refused=false;try{ColonyCallbackRequestImage.ValidateOwner(image,section);}catch(InvalidDataException){refused=true;}
                Check(refused);
            }
            RefuseActors(wrapped,null);
            RefuseActors(plain,actors);
            RefuseActors(wrapped,ColonyCallbackRequestImage.Encode(new[]{new ColonyCallbackEntry()}));
            BinaryPrimitives.WriteUInt32LittleEndian(wrapped.AsSpan(16),(1u<<20)+1);
            RefuseActors(wrapped,actors);
            var unknownRefused=false;
            try{ColonyCheckpointMetadata.Profile(Owner(13,23,1,400,800,false,8));}catch(InvalidDataException){unknownRefused=true;}
            Check(unknownRefused);
        });
        Test("report readers see complete replacements and failed writes preserve the last report", () =>
        {
            var directory=Path.Combine(Path.GetTempPath(),"ma-report-"+Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(directory);
            try
            {
                var path=Path.Combine(directory,"owner.json");
                ReportJson.WriteAtomicText(path,System.Text.Json.JsonSerializer.Serialize(new { Generation=0, Text=new string('x',8192) }));
                var reads=0; var finished=0;
                using var started=new System.Threading.ManualResetEventSlim();
                var reader=System.Threading.Tasks.Task.Run(() =>
                {
                    do
                    {
                        using var stream=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.ReadWrite|FileShare.Delete);
                        using var text=new StreamReader(stream);
                        using var row=System.Text.Json.JsonDocument.Parse(text.ReadToEnd());
                        var generation=row.RootElement.GetProperty("Generation").GetInt32();
                        Check(row.RootElement.GetProperty("Text").GetString() == new string(generation%2==0?'x':'y',8192+generation));
                        System.Threading.Interlocked.Increment(ref reads); started.Set();
                    } while(System.Threading.Volatile.Read(ref finished)==0);
                });
                try
                {
                    Check(started.Wait(TimeSpan.FromSeconds(10)));
                    for(var generation=1;generation<=100;++generation)
                        ReportJson.WriteAtomicText(path,System.Text.Json.JsonSerializer.Serialize(new { Generation=generation, Text=new string(generation%2==0?'x':'y',8192+generation) }));
                }
                finally { System.Threading.Volatile.Write(ref finished,1); reader.GetAwaiter().GetResult(); }
                Check(reads>0 && !File.Exists(path+".tmp"));
                var expected=File.ReadAllBytes(path); Directory.CreateDirectory(path+".tmp");
                var rejected=false;
                try { ReportJson.WriteAtomicText(path,"replacement"); }
                catch(IOException) { rejected=true; } catch(UnauthorizedAccessException) { rejected=true; }
                Check(rejected && expected.SequenceEqual(File.ReadAllBytes(path)));
                Directory.Delete(path+".tmp");
            }
            finally { Directory.Delete(directory,true); }
        });
        Test("handoff timing keeps queued frames separate from observed completion", () =>
        {
            var timing = new ColonyFrameTiming();
            Check(timing.Submit(0, 100) == 1 && timing.Submit(0, 200) == 2);
            Check(timing.Observe(1, out var first) && first == 100);
            Check(timing.Observe(2, out var second) && second == 200);
            Check(!timing.Observe(2, out _));
            // A fresh process can restore a nonzero owner frame; missed
            // observations must not shift the next submission's identity.
            timing.Reset();
            Check(timing.Submit(32, 300) == 33 && timing.Submit(32, 400) == 34);
            Check(timing.Observe(34, out var fourth) && fourth == 400);
            Check(timing.Submit(34, 500) == 35);
            Check(timing.Observe(35, out var fifth) && fifth == 500);
        });
        Test("all actual game channels are deep copied", () =>
        {
            using var source = new NativeFixture();
            using var copy = OwnedGameDataSnapshot.Capture(source.Pointer, 3, 2);
            copy.AssertEquivalentTo(source.Pointer);
            Check(OwnedGameDataSnapshot.ChannelCount == 47 && OwnedGameDataSnapshot.EventChannelCount == 29);
            var report = SnapshotCopyValidation.CaptureAndValidate(source.Pointer, 3, 2);
            Check(report.ByteIdentical && report.CellCount == 6 && report.ChannelCount == 47 && report.EventChannelCount == 29);
        });
        Test("copy survives source overwrite and release", () =>
        {
            var source = new NativeFixture();
            using var copy = OwnedGameDataSnapshot.Capture(source.Pointer, 3, 2);
            var mass = copy.Pointer->mass[2];
            var eventByte = ((byte*)copy.Pointer->massConsumedCallbacks)[0];
            var texture = ((byte*)copy.Pointer->propertyTextureLiquidData)[7];
            source.Overwrite(); source.Dispose();
            Check(copy.Pointer->mass[2] == mass && ((byte*)copy.Pointer->massConsumedCallbacks)[0] == eventByte &&
                ((byte*)copy.Pointer->propertyTextureLiquidData)[7] == texture);
        });
        Test("custom overlay appends callbacks while preserving native bytes", () =>
        {
            using var source = new NativeFixture();
            var originalMass = source.Pointer->mass[0];
            var originalCount = source.Pointer->numMassConsumedCallbacks;
            OwnedSnapshotOverlay? retained = null;
            using var copy = OwnedGameDataSnapshot.Capture(source.Pointer, 3, 2, writer =>
            {
                retained = writer;
                writer.Masses[0] = 12.5f;
                writer.Temperatures[0] = 300;
                writer.AppendEvents<Sim.MassConsumedCallback>(SnapshotEventChannel.MassConsumedCallbacks,
                    new[] { new Sim.MassConsumedCallback { callbackIdx = 101, elemIdx = 4, mass = 0.25f, temperature = 300, diseaseIdx = 255 } });
            });
            Check(copy.Pointer->mass[0] == 12.5f && source.Pointer->mass[0] == originalMass);
            Check(copy.Pointer->numMassConsumedCallbacks == originalCount + 1);
            Check(copy.Pointer->massConsumedCallbacks[originalCount].callbackIdx == 101);
            Check(new ReadOnlySpan<byte>(source.Pointer->massConsumedCallbacks, originalCount * sizeof(Sim.MassConsumedCallback))
                .SequenceEqual(new ReadOnlySpan<byte>(copy.Pointer->massConsumedCallbacks, originalCount * sizeof(Sim.MassConsumedCallback))));
            Reject(() => { _ = retained!.Masses.Length; });
        });
        Test("physical identity projection owns its cache-update events and retains the native source", () =>
        {
            using var source = new NativeFixture();
            using var retained = OwnedGameDataSnapshot.Capture(source.Pointer, 3, 2);
            using var copy = OwnedGameDataSnapshot.Capture(source.Pointer, 3, 2);
            var original = source.Pointer->elementIdx[1]; var count = source.Pointer->numSubstanceChangeInfo;
            var next = (ushort)(original ^ 1);
            copy.CellBuffer<ushort>(nameof(Sim.GameDataUpdate.elementIdx))[1] = next;
            copy.AppendEvents<Sim.SubstanceChangeInfo>(SnapshotEventChannel.SubstanceChangeInfo,
                new[] { new Sim.SubstanceChangeInfo { cellIdx = 1, newElemIdx = next } });
            Check(copy.Pointer->elementIdx[1] == next && copy.Pointer->numSubstanceChangeInfo == count + 1 &&
                copy.Pointer->substanceChangeInfo[count].cellIdx == 1 && copy.Pointer->substanceChangeInfo[count].newElemIdx == next);
            Check(new ReadOnlySpan<byte>(source.Pointer->substanceChangeInfo, count * sizeof(Sim.SubstanceChangeInfo)).SequenceEqual(
                new ReadOnlySpan<byte>(copy.Pointer->substanceChangeInfo, count * sizeof(Sim.SubstanceChangeInfo))));
            Check(source.Pointer->elementIdx[1] == original && retained.Pointer->elementIdx[1] == original &&
                source.Pointer->numSubstanceChangeInfo == count && retained.Pointer->numSubstanceChangeInfo == count);
            copy.ClearEventsForFault();
            Check(copy.Pointer->elementIdx[1] == next && copy.Pointer->numSubstanceChangeInfo == 0);
        });
        Test("fault freeze retains physical buffers and status tables without replaying callbacks", () =>
        {
            using var source = new NativeFixture();
            using var copy = OwnedGameDataSnapshot.Capture(source.Pointer, 3, 2);
            var mass = copy.Pointer->mass[0]; var texture = ((byte*)copy.Pointer->propertyTextureLiquidData)[7];
            var statusNames = new[] { nameof(Sim.GameDataUpdate.emittedMassEntries), nameof(Sim.GameDataUpdate.elementChunkInfos),
                nameof(Sim.GameDataUpdate.buildingTemperatures), nameof(Sim.GameDataUpdate.diseaseEmittedInfos), nameof(Sim.GameDataUpdate.diseaseConsumedInfos) };
            var statuses = statusNames.Select(field =>
            {
                var pointerOffset = (int)Marshal.OffsetOf<Sim.GameDataUpdate>(field);
                var countOffset = (int)Marshal.OffsetOf<Sim.GameDataUpdate>("num" + char.ToUpperInvariant(field[0]) + field.Substring(1));
                var count = *(int*)((byte*)copy.Pointer + countOffset);
                var address = *(IntPtr*)((byte*)copy.Pointer + pointerOffset);
                var stride = Marshal.SizeOf(typeof(Sim.GameDataUpdate).GetField(field)!.FieldType.GetElementType()!);
                return (pointerOffset, countOffset, count, address, bytes: new ReadOnlySpan<byte>((void*)address, count * stride).ToArray());
            }).ToArray();
            Check(copy.Pointer->numMassConsumedCallbacks > 0 && copy.Pointer->numFramesProcessed > 0);
            copy.ClearEventsForFault(); copy.ClearEventsForFault();
            Check(copy.Pointer->numFramesProcessed == 0 && copy.Pointer->numMassConsumedCallbacks == 0 &&
                copy.Pointer->massConsumedCallbacks == null && copy.Pointer->numMassEmittedCallbacks == 0 &&
                copy.Pointer->numCallbackInfo == 0 && copy.Pointer->mass[0] == mass &&
                ((byte*)copy.Pointer->propertyTextureLiquidData)[7] == texture && source.Pointer->numMassConsumedCallbacks > 0);
            Check(statuses.Length == 5 && statuses.All(status => status.count > 0));
            foreach (var status in statuses) Check(*(int*)((byte*)copy.Pointer + status.countOffset) == status.count &&
                *(IntPtr*)((byte*)copy.Pointer + status.pointerOffset) == status.address &&
                new ReadOnlySpan<byte>((void*)status.address, status.bytes.Length).SequenceEqual(status.bytes));
        });
        Test("wrong event record type rejects unpublished overlay", () =>
        {
            using var source = new NativeFixture();
            Reject(() => OwnedGameDataSnapshot.Capture(source.Pointer, 3, 2,
                writer => writer.AppendEvents<int>(SnapshotEventChannel.MassConsumedCallbacks, new[] { 1 })));
        });
        Test("startup native event replay preserves chronological events and current status tables", () =>
        {
            using var source=new NativeFixture();
            using var earlier=OwnedGameDataSnapshot.Capture(source.Pointer,3,2);
            using var later=OwnedGameDataSnapshot.Capture(source.Pointer,3,2);
            earlier.Pointer->massConsumedCallbacks[0].callbackIdx=101;
            later.Pointer->massConsumedCallbacks[0].callbackIdx=202;
            var currentCount=later.Pointer->numMassConsumedCallbacks;var pastCount=earlier.Pointer->numMassConsumedCallbacks;
            var frames=later.Pointer->numFramesProcessed+earlier.Pointer->numFramesProcessed;
            var currentStatus=later.Pointer->numEmittedMassEntries;
            var statusAddress=(IntPtr)later.Pointer->emittedMassEntries;
            var mass=later.Pointer->mass[0];
            var retained=new HashSet<string>{"emittedMassEntries","elementChunkInfos","buildingTemperatures","diseaseEmittedInfos","diseaseConsumedInfos"};
            var expected=new List<(int pointerOffset,int countOffset,int count,byte[] bytes)>();
            foreach(var field in typeof(Sim.GameDataUpdate).GetFields(BindingFlags.Public|BindingFlags.Instance))
            {
                var countName="num"+char.ToUpperInvariant(field.Name[0])+field.Name.Substring(1);
                if(!field.FieldType.IsPointer || typeof(Sim.GameDataUpdate).GetField(countName)==null) continue;
                var offset=(int)Marshal.OffsetOf<Sim.GameDataUpdate>(field.Name);var countOffset=(int)Marshal.OffsetOf<Sim.GameDataUpdate>(countName);
                var stride=Marshal.SizeOf(field.FieldType.GetElementType()!);
                var beforeCount=*(int*)((byte*)earlier.Pointer+countOffset);var afterCount=*(int*)((byte*)later.Pointer+countOffset);
                var first=new ReadOnlySpan<byte>((void*)*(IntPtr*)((byte*)earlier.Pointer+offset),beforeCount*stride).ToArray();
                var last=new ReadOnlySpan<byte>((void*)*(IntPtr*)((byte*)later.Pointer+offset),afterCount*stride).ToArray();
                expected.Add((offset,countOffset,retained.Contains(field.Name) ? afterCount : beforeCount+afterCount,
                    retained.Contains(field.Name) ? last : first.Concat(last).ToArray()));
            }
            Check(OwnedGameDataSnapshot.HasTransientEvents(earlier.Pointer));
            later.PrependNativeEventsFrom(earlier);
            Check(expected.Count==29);
            foreach(var channel in expected) Check(*(int*)((byte*)later.Pointer+channel.countOffset)==channel.count &&
                new ReadOnlySpan<byte>((void*)*(IntPtr*)((byte*)later.Pointer+channel.pointerOffset),channel.bytes.Length).SequenceEqual(channel.bytes));
            Check(later.Pointer->numMassConsumedCallbacks==pastCount+currentCount && later.Pointer->massConsumedCallbacks[0].callbackIdx==101 &&
                later.Pointer->massConsumedCallbacks[pastCount].callbackIdx==202 && later.Pointer->numFramesProcessed==frames &&
                later.Pointer->numEmittedMassEntries==currentStatus && (IntPtr)later.Pointer->emittedMassEntries==statusAddress && later.Pointer->mass[0]==mass);
            Check(earlier.Pointer->numMassConsumedCallbacks==pastCount && earlier.Pointer->massConsumedCallbacks[0].callbackIdx==101);
            Reject(()=>later.PrependNativeEventsFrom(later));
            using var wrongGrid=OwnedGameDataSnapshot.Capture(source.Pointer,6,1);
            Reject(()=>later.PrependNativeEventsFrom(wrongGrid));
            earlier.ClearEventsForFault();Check(!OwnedGameDataSnapshot.HasTransientEvents(earlier.Pointer));
        });
        Test("late malformed native event leaves previous publication valid", () =>
        {
            using var source = new NativeFixture();
            using var publisher = new OwnedSnapshotPublisher();
            var first = publisher.Publish(source.Pointer, 3, 2);
            var pointer = (IntPtr)first.Pointer;
            source.Pointer->numRadiationConsumedCallbacks = -1;
            Reject(() => publisher.Publish(source.Pointer, 3, 2));
            Check((IntPtr)publisher.CurrentPointer == pointer && first.Pointer->numFramesProcessed == 2);
        });
        Test("overlay failure leaves previous publication valid", () =>
        {
            using var source = new NativeFixture();
            using var publisher = new OwnedSnapshotPublisher();
            var first = publisher.Publish(source.Pointer, 3, 2);
            var pointer = (IntPtr)first.Pointer;
            Reject(() => publisher.Publish(source.Pointer, 3, 2, writer =>
            {
                writer.Masses[0] = 123;
                throw new InvalidDataException("Injected incomplete custom frame");
            }));
            Check((IntPtr)publisher.CurrentPointer == pointer && first.Pointer->mass[0] == source.Pointer->mass[0]);
        });
        Test("next successful publication retires old buffers", () =>
        {
            using var source = new NativeFixture();
            using var publisher = new OwnedSnapshotPublisher();
            var first = publisher.Publish(source.Pointer, 3, 2);
            source.Pointer->numFramesProcessed = 3;
            var second = publisher.Publish(source.Pointer, 3, 2);
            Reject(() => { _ = (IntPtr)first.Pointer; });
            Check(second.Pointer->numFramesProcessed == 3 && publisher.CurrentPointer == second.Pointer);
            publisher.Dispose();
            Reject(() => { _ = (IntPtr)second.Pointer; });
            Reject(() => publisher.Publish(source.Pointer, 3, 2));
        });
        Test("zero-count arrays relinquish native addresses", () =>
        {
            using var source = new NativeFixture();
            source.Pointer->numMassConsumedCallbacks = 0;
            using var copy = OwnedGameDataSnapshot.Capture(source.Pointer, 3, 2);
            Check(source.Pointer->massConsumedCallbacks != null && copy.Pointer->massConsumedCallbacks == null);
            copy.AssertEquivalentTo(source.Pointer);
        });
        Test("required cell and texture pointers reject null", () =>
        {
            using var source = new NativeFixture();
            source.Pointer->mass = null;
            Reject(() => OwnedGameDataSnapshot.Capture(source.Pointer, 3, 2));
            using var textures = new NativeFixture();
            textures.Pointer->propertyTextureLiquid = IntPtr.Zero;
            Reject(() => OwnedGameDataSnapshot.Capture(textures.Pointer, 3, 2));
            using var headless = OwnedGameDataSnapshot.Capture(textures.Pointer, 3, 2, allowMissingTextures: true);
            Check(headless.Pointer->propertyTextureLiquid == IntPtr.Zero);
            headless.AssertEquivalentTo(textures.Pointer, allowMissingTextures: true);
        });
        Test("invalid dimensions and frame metadata reject", () =>
        {
            using var source = new NativeFixture();
            Reject(() => OwnedGameDataSnapshot.Capture(source.Pointer, 0, 2));
            Reject(() => OwnedGameDataSnapshot.Capture(source.Pointer, int.MaxValue, 2));
            Reject(() => OwnedGameDataSnapshot.Capture(null, 3, 2));
            source.Pointer->numFramesProcessed = -1;
            Reject(() => OwnedGameDataSnapshot.Capture(source.Pointer, 3, 2));
        });
        Test("colony envelope owns native and custom state in one frame", () =>
        {
            var native = NativePayload();
            var custom = new byte[] { 1, 2, 3, 4, 5 };
            var catalog = CatalogHash();
            var envelope = ColonySaveEnvelope.Create(744825, 1, 1234, catalog, native, custom);
            var encoded = envelope.CopyEncodedData();
            Check(ColonySaveEnvelope.HasMagic(encoded) && !encoded.AsSpan(0, 8).SequenceEqual(native.AsSpan(0, 8)));
            native[30] ^= 0xff; custom[0] ^= 0xff; catalog[0] ^= 0xff;
            var restored = ColonySaveEnvelope.Parse(encoded, 744825, 1, CatalogHash());
            encoded[0] = 0;
            Check(restored.OniBuild == 744825 && restored.MaterialModelVersion == 1 && restored.CompletedFrame == 1234);
            Check(restored.NativeData.SequenceEqual(NativePayload()) && restored.CustomData.SequenceEqual(new byte[] { 1, 2, 3, 4, 5 }));
            Check(restored.MaterialCatalogHash.SequenceEqual(CatalogHash()));
        });
        Test("length-prefixed envelope consumes exactly its own section", () =>
        {
            var envelope = ColonySaveEnvelope.Create(744825, 1, 0, CatalogHash(), NativePayload(), new byte[] { 9 });
            using var stream = new MemoryStream();
            using (var writer = new BinaryWriter(stream, System.Text.Encoding.UTF8, true))
            {
                envelope.WriteLengthPrefixed(writer); writer.Write(1234567);
            }
            stream.Position = 0;
            using var reader = new BinaryReader(stream);
            var restored = ColonySaveEnvelope.ReadLengthPrefixed(reader, 744825, 1, CatalogHash());
            Check(restored.CompletedFrame == 0 && restored.CustomData[0] == 9 && reader.ReadInt32() == 1234567);
        });
        Test("corrupt header payload and checksum reject", () =>
        {
            var envelope = ColonySaveEnvelope.Create(744825, 1, 55, CatalogHash(), NativePayload(), new byte[] { 9 });
            foreach (var position in new[] { 0, 8, 20, 60, 68, 80, envelope.EncodedLength - 1 })
            {
                var damaged = envelope.CopyEncodedData(); damaged[position] ^= 0x80;
                Reject(() => ColonySaveEnvelope.Parse(damaged, 744825, 1, CatalogHash()));
            }
            var overflow = envelope.CopyEncodedData();
            BinaryPrimitives.WriteInt64LittleEndian(overflow.AsSpan(60), long.MaxValue);
            Reject(() => ColonySaveEnvelope.Parse(overflow, 744825, 1, CatalogHash()));
        });
        Test("incompatible build material model and catalogue reject", () =>
        {
            var envelope = ColonySaveEnvelope.Create(744825, 1, 55, CatalogHash(), NativePayload(), new byte[] { 9 });
            var encoded = envelope.CopyEncodedData();
            Reject(() => ColonySaveEnvelope.Parse(encoded, 744826, 1, CatalogHash()));
            Reject(() => ColonySaveEnvelope.Parse(encoded, 744825, 2, CatalogHash()));
            var other = CatalogHash(); other[0] ^= 0xff;
            Reject(() => ColonySaveEnvelope.Parse(encoded, 744825, 1, other));
            Reject(() => ColonySaveEnvelope.Create(744825, 1, 0, new byte[31], NativePayload(), new byte[] { 9 }));
        });
        Test("incomplete native framing or custom state reject", () =>
        {
            Reject(() => ColonySaveEnvelope.Create(744825, 1, 0, CatalogHash(), NativePayload(), Array.Empty<byte>()));
            var native = NativePayload();
            Reject(() => ColonySaveEnvelope.Create(744825, 1, 0, CatalogHash(), native.AsSpan(0, native.Length - 1), new byte[] { 9 }));
            native[0] = 0;
            Reject(() => ColonySaveEnvelope.Create(744825, 1, 0, CatalogHash(), native, new byte[] { 9 }));
            var envelope = ColonySaveEnvelope.Create(744825, 1, 0, CatalogHash(), NativePayload(), new byte[] { 9 });
            var encoded = envelope.CopyEncodedData();
            Reject(() => ColonySaveEnvelope.Parse(encoded.AsSpan(0, encoded.Length - 1), 744825, 1, CatalogHash()));
            using var stream = new MemoryStream();
            using (var writer = new BinaryWriter(stream, System.Text.Encoding.UTF8, true))
            {
                writer.Write(encoded.Length + 10); writer.Write(encoded);
            }
            stream.Position = 0;
            using var reader = new BinaryReader(stream);
            Reject(() => ColonySaveEnvelope.ReadLengthPrefixed(reader, 744825, 1, CatalogHash()));
        });
        Test("actual colony save pause and coroutine declarations match their adapters", () =>
        {
            const BindingFlags flags=BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic;
            foreach(var field in new (Type Owner,string Name,Type Value)[]{
                (typeof(Game),"simDt",typeof(float)),(typeof(Game),"simSubTick",typeof(int)),
                (typeof(SpeedControlScreen),"pauseCount",typeof(int)),(typeof(PlayerController),"draggingAllowed",typeof(bool)),
                (typeof(SaveLoader),"compressSaveData",typeof(bool)),
                (typeof(Game),"activatePreCB",typeof(Game.SavingPreCB)),(typeof(Game),"activateActiveCB",typeof(Game.SavingActiveCB)),
                (typeof(Game),"activatePostCB",typeof(Game.SavingPostCB))})
                Check(field.Owner.GetField(field.Name,flags)?.FieldType==field.Value);
            foreach(var entry in new (Type Owner,string Name,Type Result,Type[] Arguments)[]{
                (typeof(SaveLoader),"Save",typeof(string),new[]{typeof(string),typeof(bool),typeof(bool)}),
                (typeof(SaveLoader),"Save",typeof(void),new[]{typeof(BinaryWriter)}),
                (typeof(Game),"StartDelayedSave",typeof(void),new[]{typeof(string),typeof(bool),typeof(bool)}),
                (typeof(Game),"SimEveryTick",typeof(void),new[]{typeof(float)}),
                (typeof(Game),"UnsafeSim200ms",typeof(void),new[]{typeof(float)})})
                Check(entry.Owner.GetMethod(entry.Name,flags,null,entry.Arguments,null)?.ReturnType==entry.Result);
            var headers=typeof(SaveGame).GetMethods(flags).Where(method=>method.Name=="GetSaveHeader").ToArray();
            Check(headers.Length==1 && headers[0].ReturnType==typeof(byte[]) &&
                headers[0].GetParameters().Select(parameter=>parameter.ParameterType).SequenceEqual(new[]{typeof(bool),typeof(bool),typeof(SaveGame.Header).MakeByRefType()}));
            Check(typeof(ConfirmDialogScreen).GetField("popupMessage",flags)?.FieldType.FullName=="LocText");
        });
        Test("actual main-menu reload boundaries are managed static game entry points", () =>
        {
            const BindingFlags flags=BindingFlags.Static|BindingFlags.Public|BindingFlags.NonPublic;
            foreach(var entry in new[]{(typeof(PauseScreen),"TriggerQuitGame"),(typeof(LoadScreen),"ForceStopGame")})
            {
                var method=entry.Item1.GetMethod(entry.Item2,flags,null,Type.EmptyTypes,null);
                Check(method!=null && method.ReturnType==typeof(void) && method.GetMethodBody()!=null);
            }
        });
        FluidRouterTests.Run(Test, Check, Reject);
        FluidFrameCoordinatorTests.Run(Test, Check, Reject);
        MixtureRuntimeTests.Run(Test, Check, Reject);
        ActorRegistrationTests();
        CellMassEditTests();
        NativeFluidTransferPlanTests.Run(Test,Check,Reject);
        NativeFrameInputJournalTests.Run(Test,Check,Reject);
        NativeTransientParticleImageTests.Run(Test,Check,Reject);
        NativeBinary32Tests.Run(Test,Check,Reject);
        TileHoverReceiptFileTests.Run(Test,Check);
        BreathingCellReadTests.Run(Test, Check, Reject);
        PressureReadTests.Run(Test, Check, Reject);
        GpuPageReadbackTests.Run(Test, Check, Reject);
        ActorCheckpointImageTests.Run(Test,Check,Reject);
        NativeOrbitalReferenceTests.Run(Test,Check,Reject);
        NativeDomainReadingTests.Run(Test,Check,Reject);
        ColonyValidationLimitsTests.Run(Test,Check,Reject);
        DevelopmentTelemetryTests.Run(Test,Check,Reject);
        ConfiguredDomainLayoutTests.Run(Test,Check,Reject);
        ConfiguredCheckpointPayloadTests.Run(Test,Check,Reject);
        Test("natural day autosave entry is the inspected game scheduler with a cycle argument", () => {
            var method=typeof(GameClock).GetMethod("DoAutoSave",BindingFlags.Instance|BindingFlags.NonPublic);
            Check(method!=null && method.ReturnType==typeof(void) && method.GetMethodBody()!=null &&
                method.GetParameters().Length==1 && method.GetParameters()[0].ParameterType==typeof(int));
        });
        Test("owner save barrier, particle and control targets are the inspected game methods", () => {
            const BindingFlags any=BindingFlags.Instance|BindingFlags.Static|BindingFlags.Public|BindingFlags.NonPublic;
            MethodInfo Find(Type type,string name,params Type[] args)
            {var method=type.GetMethod(name,any,null,args,null);Check(method!=null && method.GetMethodBody()!=null);return method!;}
            Check(Find(typeof(SaveLoader),"Save",typeof(string),typeof(bool),typeof(bool)).ReturnType==typeof(string));
            Check(Find(typeof(Game),"StepTheSim",typeof(float)).ReturnType.IsPointer);
            Check(Find(typeof(Game),"UnsafeSim200ms",typeof(float)).ReturnType==typeof(void));
            Check(Find(typeof(Game),"SimEveryTick",typeof(float)).ReturnType==typeof(void));
            var delayed=Find(typeof(Game),"StartDelayedSave",typeof(string),typeof(bool),typeof(bool));Check(delayed.IsPublic && !delayed.IsStatic);
            Check(Find(typeof(CO2Manager),"Sim33ms",typeof(float)).IsPublic);
            var load=Find(typeof(LoadScreen),"DoLoad",typeof(string));Check(load.IsPublic && load.IsStatic && load.ReturnType==typeof(void));
            Check(typeof(FrontEndManager).GetField("Instance",any)!=null && typeof(ScreenPrefabs).GetField("ConfirmDialogScreen",any)!=null);
            var manager=typeof(ConduitFlow).Assembly.GetType("UtilityNetworkManager`2");Check(manager!=null);
            Check(manager!.GetProperty("IsDirty",any)?.PropertyType==typeof(bool));
            var updateTopology=Find(manager,"Update");Check(updateTopology.IsPublic&&!updateTopology.IsStatic&&updateTopology.ReturnType==typeof(void));
            Check(typeof(ConduitFlow).GetField("networkMgr",any)?.FieldType==typeof(IUtilityNetworkMgr));
            var spawn=Find(typeof(CO2Manager),"SpawnCO2",typeof(UnityEngine.Vector3),typeof(float),typeof(float),typeof(bool),typeof(float));
            Check(spawn.IsPublic && spawn.ReturnType==typeof(CO2));
            Check(typeof(CO2Manager).GetField("co2Items",any)?.FieldType==typeof(List<CO2>));
            foreach(var field in new[]{"velocity","mass","temperature","lifetimeRemaining"}) Check(typeof(CO2).GetField(field,any)!=null);
        });
        if (benchmark) FluidFrameCoordinatorTests.Benchmark();
        Console.WriteLine($"{passed}/{passed} snapshot, persistence and message boundary scenarios passed against actual ONI declarations.");
        return 0;
    }

    private static void Test(string name, System.Action action) { action(); ++passed; Console.WriteLine("PASS " + name); }
    private static void CellMassEditTests()
    {
        byte[] Packets(int count = 2)
        {
            var data = new byte[28 * count];
            for (var index = 0; index < count; ++index)
            {
                var row = data.AsSpan(28 * index, 28);
                BinaryPrimitives.WriteInt32LittleEndian(row, 10 + index);
                BinaryPrimitives.WriteInt32LittleEndian(row.Slice(4), -1);
                BinaryPrimitives.WriteInt32LittleEndian(row.Slice(8), BitConverter.SingleToInt32Bits(310));
                BinaryPrimitives.WriteInt32LittleEndian(row.Slice(12), BitConverter.SingleToInt32Bits(.03125f));
                BinaryPrimitives.WriteUInt16LittleEndian(row.Slice(20), 200);
                row[23] = 255; row[24] = 1;
                row[25] = 91; row[26] = 183; row[27] = 247; // Native padding is not a protocol flag.
            }
            return data;
        }
        uint Species(ushort element) => element == 200 ? 4U : throw new NotSupportedException("Unmodeled material");
        NativeCellMassEdit[] Decode(byte[] data) => NativeCellMassEditPlan.Decode(28, data.Length / 28, data,
            cell => cell == 10 || cell == 11, Species);
        Test("owned callback-less mass edits preserve complete native parcels and ignore padding", () =>
        {
            var plan = Decode(Packets());
            Check(plan.Length == 2 && plan[0].Cell == 10 && plan[1].Cell == 11 &&
                plan.All(edit => edit.Species == 4 && edit.MassKg == .03125f && edit.TemperatureK == 310));
        });
        Test("outside native cell modifications retain native replacement and callback semantics", () =>
        {
            var data = Packets();
            for (var index = 0; index < 2; ++index)
            {
                var row = data.AsSpan(28 * index, 28);
                BinaryPrimitives.WriteInt32LittleEndian(row, 100 + index);
                BinaryPrimitives.WriteInt32LittleEndian(row.Slice(4), 45);
                BinaryPrimitives.WriteInt32LittleEndian(row.Slice(16), 87);
                row[22] = 2; row[24] = 0;
            }
            var before = (byte[])data.Clone();
            Check(NativeCellMassEditPlan.Decode(28, 2, data, _ => false,
                _ => throw new Exception("Outside material must stay native")).Length == 0 && data.SequenceEqual(before));
        });
        Test("late unsupported cell edit rejects the whole packet without rewriting source bytes", () =>
        {
            foreach (var fault in new[] { "callback", "replace", "displace", "disease", "material", "negative", "zero", "nan", "temperature" })
            {
                var data = Packets();
                if (fault == "callback") BinaryPrimitives.WriteInt32LittleEndian(data.AsSpan(32), 42);
                if (fault == "replace") data[50] = 1;
                if (fault == "displace") data[52] = 0;
                if (fault == "disease") BinaryPrimitives.WriteInt32LittleEndian(data.AsSpan(44), 1);
                if (fault == "material") BinaryPrimitives.WriteUInt16LittleEndian(data.AsSpan(48), 199);
                if (fault == "negative") BinaryPrimitives.WriteInt32LittleEndian(data.AsSpan(40), BitConverter.SingleToInt32Bits(-1));
                if (fault == "zero") BinaryPrimitives.WriteInt32LittleEndian(data.AsSpan(40), 0);
                if (fault == "nan") BinaryPrimitives.WriteInt32LittleEndian(data.AsSpan(40), BitConverter.SingleToInt32Bits(float.NaN));
                if (fault == "temperature") BinaryPrimitives.WriteInt32LittleEndian(data.AsSpan(36), BitConverter.SingleToInt32Bits(float.PositiveInfinity));
                var before = (byte[])data.Clone(); Reject(() => Decode(data)); Check(data.SequenceEqual(before));
            }
        });
        Test("mixed-owner mass-edit batches reject before either owner dispatches", () =>
        {
            var data = Packets(); BinaryPrimitives.WriteInt32LittleEndian(data.AsSpan(28), 99);
            Reject(() => Decode(data));
        });
        Test("mass-edit shape and whole-owner capacity limits are explicit", () =>
        {
            Reject(() => NativeCellMassEditPlan.Decode(16, 2, Packets(), _ => true, Species));
            Reject(() => NativeCellMassEditPlan.Decode(28, -1, Array.Empty<byte>(), _ => true, Species));
            Reject(() => NativeCellMassEditPlan.Decode(28, int.MaxValue, Array.Empty<byte>(), _ => true, Species));
            Reject(() => NativeCellMassEditPlan.Decode(28, 2, new byte[55], _ => true, Species));
            Reject(() => NativeCellMassEditPlan.Decode(28, 257, Packets(257), _ => true, Species));
            Check(NativeCellMassEditPlan.Decode(28, 256, Packets(256), _ => true, Species).Length == 256);
        });
    }
    private static void ActorRegistrationTests()
    {
        byte[] Consumers(int firstCallback=41,int secondCallback=42)
        {
            var data=new byte[24];
            for(var i=0;i<2;++i)
            {
                BinaryPrimitives.WriteInt32LittleEndian(data.AsSpan(12*i),10+i);
                BinaryPrimitives.WriteInt32LittleEndian(data.AsSpan(12*i+4),i==0 ? firstCallback : secondCallback);
                BinaryPrimitives.WriteUInt16LittleEndian(data.AsSpan(12*i+10),7);
            }
            return data;
        }
        NativeActorRegistration[] Decode(byte[] data,IEnumerable<int>? pending=null)=>
            NativeActorRegistrationPlan.Decode(2024405073,12,2,data,cell=>cell==10 || cell==11,
                element=>element==7 ? 3U : throw new NotSupportedException("material"),pending ?? Array.Empty<int>());
        Test("actor batch admission preserves both consumer callbacks and phase",()=>
        {
            var plan=Decode(Consumers());
            Check(plan.Length==2 && plan[0].Callback==41 && plan[1].Callback==42 && plan[0].Cell==10 && plan[1].Cell==11 &&
                plan.All(item=>item.Kind==1 && item.Species==3 && item.Blocked==-1 && item.Unblocked==-1));
        });
        Test("late invalid actor registration leaves existing pending bindings untouched",()=>
        {
            var pending=new HashSet<int>{42,90};
            Reject(()=>Decode(Consumers(),pending));Check(pending.SetEquals(new[]{42,90}));
            Reject(()=>Decode(Consumers(41,41)));var bad=Consumers();bad[20]=1;Reject(()=>Decode(bad));
            bad=Consumers();bad[21]=1;Reject(()=>Decode(bad));
            bad=Consumers();BinaryPrimitives.WriteUInt16LittleEndian(bad.AsSpan(22),99);Reject(()=>Decode(bad));
            Reject(()=>Decode(Consumers(41,-1)));
        });
        Test("unowned consumer packet retains its native admission semantics",()=>
        {
            var data=Consumers();BinaryPrimitives.WriteInt32LittleEndian(data.AsSpan(12),900);
            data[20]=255;data[21]=255;BinaryPrimitives.WriteUInt16LittleEndian(data.AsSpan(22),99);
            var plan=Decode(data);Check(plan.Length==1 && plan[0].Callback==41);
        });
        Test("emitter batches preserve registration and blocked callback identities",()=>
        {
            var data=new byte[48];
            for(var i=0;i<3;++i)
            {
                BinaryPrimitives.WriteInt32LittleEndian(data.AsSpan(i*16+4),i==2 ? -1 : 50+i);
                BinaryPrimitives.WriteInt32LittleEndian(data.AsSpan(i*16+8),70+i);
                BinaryPrimitives.WriteInt32LittleEndian(data.AsSpan(i*16+12),80+i);
            }
            var plan=NativeActorRegistrationPlan.Decode(-505471181,16,3,data,_=>throw new Exception("emitter has no cell"),
                _=>throw new Exception("emitter has no material yet"),Array.Empty<int>());
            Check(plan.Length==2 && plan[0].Kind==2 && plan[0].Callback==50 && plan[1].Callback==51 &&
                plan[0].Blocked==70 && plan[1].Unblocked==81);
            Reject(()=>NativeActorRegistrationPlan.Decode(-505471181,16,3,data,_=>true,_=>0,new[]{51}));
        });
        Test("malformed and oversized registration batches reject before dispatch",()=>
        {
            Reject(()=>NativeActorRegistrationPlan.Decode(0,12,2,Consumers(),_=>true,_=>0,Array.Empty<int>()));
            Reject(()=>NativeActorRegistrationPlan.Decode(2024405073,16,2,Consumers(),_=>true,_=>0,Array.Empty<int>()));
            Reject(()=>NativeActorRegistrationPlan.Decode(2024405073,12,-1,Array.Empty<byte>(),_=>true,_=>0,Array.Empty<int>()));
            var data=new byte[16*257];
            for(var i=0;i<257;++i)BinaryPrimitives.WriteInt32LittleEndian(data.AsSpan(16*i+4),i);
            Reject(()=>NativeActorRegistrationPlan.Decode(-505471181,16,257,data,_=>true,_=>0,Array.Empty<int>()));
        });
    }
    private static void Check(bool condition) { if (!condition) throw new InvalidOperationException("Assertion failed"); }
    private static void Reject(System.Action action)
    {
        try { action(); } catch (ArgumentException) { return; } catch (InvalidDataException) { return; }
        catch (InvalidOperationException) { return; } catch (NotSupportedException) { return; }
        catch (EndOfStreamException) { return; }
        throw new InvalidOperationException("Expected a rejection");
    }

    private static byte[] CatalogHash()
    {
        var result = new byte[32]; for (var index = 0; index < result.Length; ++index) result[index] = (byte)(index + 1);
        return result;
    }

    private static byte[] NativePayload()
    {
        var native = new byte[29 + 36 * 3 * 4];
        System.Text.Encoding.ASCII.GetBytes("SIMSAVE\0").CopyTo(native, 0);
        BinaryPrimitives.WriteInt32LittleEndian(native.AsSpan(8), 15);
        BinaryPrimitives.WriteInt32LittleEndian(native.AsSpan(12), 3);
        BinaryPrimitives.WriteInt32LittleEndian(native.AsSpan(16), 4);
        return native;
    }

    internal sealed class NativeFixture : IDisposable
    {
        private readonly List<(IntPtr pointer, int bytes)> buffers = new();
        private IntPtr snapshot;
        internal Sim.GameDataUpdate* Pointer => (Sim.GameDataUpdate*)snapshot;
        internal NativeFixture()
        {
            snapshot = Marshal.AllocHGlobal(sizeof(Sim.GameDataUpdate));
            *Pointer = default;
            Pointer->numFramesProcessed = 2;
            var type = typeof(Sim.GameDataUpdate);
            foreach (var field in type.GetFields(BindingFlags.Public | BindingFlags.Instance))
            {
                if (!field.FieldType.IsPointer && field.FieldType != typeof(IntPtr)) continue;
                var countName = "num" + char.ToUpperInvariant(field.Name[0]) + field.Name.Substring(1);
                var countField = type.GetField(countName);
                var count = countField == null ? 6 : 2;
                if (countField != null) *(int*)((byte*)Pointer + (int)Marshal.OffsetOf<Sim.GameDataUpdate>(countName)) = count;
                var stride = field.FieldType.IsPointer ? Marshal.SizeOf(field.FieldType.GetElementType()!) : 1;
                if (field.Name == nameof(Sim.GameDataUpdate.propertyTextureFlow)) count *= 8;
                if (field.Name is nameof(Sim.GameDataUpdate.propertyTextureLiquid) or nameof(Sim.GameDataUpdate.propertyTextureLiquidData)
                    or nameof(Sim.GameDataUpdate.propertyTextureMaterialData)) count *= 4;
                var bytes = checked(stride * count);
                var buffer = Marshal.AllocHGlobal(bytes);
                for (var index = 0; index < bytes; ++index) ((byte*)buffer)[index] = (byte)(buffers.Count * 7 + index + 1);
                buffers.Add((buffer, bytes));
                *(IntPtr*)((byte*)Pointer + (int)Marshal.OffsetOf<Sim.GameDataUpdate>(field.Name)) = buffer;
            }
        }
        internal void Overwrite()
        {
            foreach (var buffer in buffers) new Span<byte>((void*)buffer.pointer, buffer.bytes).Fill(0xff);
        }
        public void Dispose()
        {
            foreach (var buffer in buffers) Marshal.FreeHGlobal(buffer.pointer);
            buffers.Clear();
            if (snapshot != IntPtr.Zero) Marshal.FreeHGlobal(snapshot);
            snapshot = IntPtr.Zero;
        }
    }
}
