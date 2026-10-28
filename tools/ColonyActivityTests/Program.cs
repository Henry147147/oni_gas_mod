using MixedAtmospheres.Oni;

static class Program
{
    static int checks;
    static void Check(bool yes) { ++checks; if (!yes) throw new Exception("Activity check " + checks + " failed."); }
    static void Equal(ReadOnlySpan<uint> actual, params uint[] expected) => Check(actual.SequenceEqual(expected));
    static void Refuse<T>(Action action) where T : Exception
    { try { action(); } catch (T) { ++checks; return; } throw new Exception("Expected " + typeof(T).Name); }
    static void Main(string[] args)
    {
        var footprint = new ColonyActivityFootprint();
        footprint.Begin(4,3); footprint.Radius(0,1); Equal(footprint.Finish(Array.Empty<uint>()),0,1,4,5);
        footprint.Begin(4,3); footprint.Offset(3,1,0); footprint.Offset(0,-1,0); footprint.Offset(0,0,-1); Equal(footprint.Finish(Array.Empty<uint>()));
        foreach (var pair in new[]{(0,10u),(1,2u),(2,0u),(3,8u),(5,8u),(6,2u)})
        { footprint.Begin(4,3); footprint.Offset(5,1,1,pair.Item1); Equal(footprint.Finish(Array.Empty<uint>()),pair.Item2); }
        footprint.Begin(4,3); footprint.Add(5); footprint.Add(5); footprint.Add(-1); footprint.Add(12);
        var prior=footprint.Finish(Array.Empty<uint>());Equal(prior,5);Check(ReferenceEquals(prior,footprint.Finish(prior)));
        Refuse<InvalidOperationException>(()=>footprint.Offset(5,1,1,4));
        Refuse<ArgumentOutOfRangeException>(()=>footprint.Radius(0,256));
        Refuse<ArgumentOutOfRangeException>(()=>footprint.Begin(int.MaxValue,2));
        footprint.Begin(4,3);footprint.Clip(1,1,2,2);footprint.Add(0);footprint.Add(7);footprint.Offset(5,-1,0);footprint.Radius(0,255);
        Equal(footprint.Finish(Array.Empty<uint>()),5,6,9,10);
        Refuse<ArgumentOutOfRangeException>(()=>footprint.Clip(3,1,2,2));
        footprint.Begin(6,5);footprint.Clip(2,1,2,3);footprint.Add(7);footprint.Add(10);footprint.Add(2);footprint.Add(26);
        Equal(footprint.Finish(Array.Empty<uint>()),8,9,20); // external cardinal contacts, no diagonal wrap
        var census = new ColonyActivityCells();
        var supplied=new uint[]{1,2};census.Begin(4,3);census.Observe(1,supplied);census.Observe(1L<<32,new uint[]{2,3});
        var seeded=census.Finish();Equal(seeded.Actors,1,2,3);Equal(seeded.Touched);Check(census.ActorCount==2);supplied[0]=0;
        census.Begin(4,3);census.Observe(1,new uint[]{1,2});census.Observe(1L<<32,new uint[]{2,3});
        var unchanged=census.Finish();Equal(unchanged.Actors,1,2,3);Equal(unchanged.Touched);Equal(seeded.Actors,1,2,3);
        census.Begin(4,3);census.Observe(1,new uint[]{5,6});census.Observe(1L<<32,new uint[]{2,3});
        var moved=census.Finish();Equal(moved.Actors,2,3,5,6);Equal(moved.Touched,1,2,5,6);Equal(unchanged.Actors,1,2,3);
        census.Retire(1);census.Begin(4,3);census.Observe(1L<<32,new uint[]{2,3});var retired=census.Finish();Equal(retired.Actors,2,3);Equal(retired.Touched,5,6);
        census.Begin(4,3);var removed=census.Finish();Equal(removed.Actors);Equal(removed.Touched,2,3);
        census.Begin(4,3);var settled=census.Finish();Equal(settled.Touched);
        foreach(var bad in new[]{new uint[]{2,1},new uint[]{1,1},new uint[]{12}})
        { census.Begin(4,3);Refuse<ArgumentException>(()=>census.Observe(7,bad));Equal(census.Finish().Actors); }
        census.Begin(4,3);census.Observe(7,new uint[]{1});Refuse<InvalidOperationException>(()=>census.Observe(7,new uint[]{2}));
        census.Reset();census.Begin(3,4);census.Observe(7,new uint[]{2});Equal(census.Finish().Touched);
        census.Begin(4,3);census.Observe(7,new uint[]{2});Equal(census.Finish().Touched); // same cell count, different row layout
        var fluid=new HashSet<uint>{1,3,4,5,7,9};
        Check(ColonyActivityMask.Project(new uint[]{4},4,3,fluid.Contains).SetEquals(new uint[]{4}));
        Check(ColonyActivityMask.Project(new uint[]{0},4,3,fluid.Contains).SetEquals(new uint[]{1,4}));
        Check(ColonyActivityMask.Project(new uint[]{3},4,3,c=>c==4).Count==0); // no horizontal row wrap
        Check(ColonyActivityMask.Project(new uint[]{6},4,3,fluid.Contains).SetEquals(new uint[]{5,7}));
        Check(ColonyActivityDelivery.NeedsRefresh(seeded,null));
        Check(!ColonyActivityDelivery.NeedsRefresh(unchanged,seeded));
        Check(ColonyActivityDelivery.NeedsRefresh(moved,unchanged));
        Check(ColonyActivityDelivery.NeedsRefresh(retired,moved));
        Check(ColonyActivityDelivery.NeedsRefresh(removed,retired));
        Check(!ColonyActivityDelivery.NeedsRefresh(settled,removed)); // zero-time flush reaches quiescence
        Check(!ColonyActivityDelivery.NeedsRefresh(settled,settled));
        ColonyActivityDelivery.Validate(7,.2f,7,0,0,0);++checks;
        ColonyActivityDelivery.Validate(7,.2f,7,2,81,.2f);++checks;
        // Stale sampled completion counts never enter the delivery contract.
        Refuse<InvalidOperationException>(()=>ColonyActivityDelivery.Validate(7,.2f,8,1,81,.2f));
        Refuse<InvalidOperationException>(()=>ColonyActivityDelivery.Validate(0,.2f,0,0,0,0));
        Refuse<InvalidOperationException>(()=>ColonyActivityDelivery.Validate(7,.2f,7,1,0,.2f));
        Refuse<InvalidOperationException>(()=>ColonyActivityDelivery.Validate(7,.2f,7,1,81,.2));
        Refuse<InvalidOperationException>(()=>ColonyActivityDelivery.Validate(7,.2f,7,1,81,Math.BitIncrement((double).2f)));
        foreach(var bad in new[]{0.0,-0.0,-1,double.NaN,double.PositiveInfinity})
            Refuse<ArgumentOutOfRangeException>(()=>ColonyActivityDelivery.Validate(7,bad,7,0,0,0));
        ColonyActivityResavePoint Boundary() => new ColonyActivityResavePoint { Session=7,Frame=40,OwnerSeconds=8,GameSeconds=49,
            ActorInputs=3,Committed=40,Next=42,Head=41,HeadSeconds=.2f,Pending=1,Enabled=1 };
        var point=Boundary();var physical=new byte[]{1,2,3};ColonyActivityResaveProof.Validate(point,Boundary(),physical,physical);++checks;
        foreach(var mutate in new Action<ColonyActivityResavePoint>[] { p=>p.Session++,p=>p.Frame++,p=>p.OwnerSeconds=Math.BitIncrement(p.OwnerSeconds),
            p=>p.GameSeconds=Math.BitIncrement(p.GameSeconds),p=>p.ActorInputs++,p=>p.Committed++,p=>p.Next++,p=>p.Head++,
            p=>p.HeadSeconds=.2,p=>p.Pending=0,p=>p.Enabled=0 })
        { var changed=Boundary();mutate(changed);Refuse<InvalidOperationException>(()=>ColonyActivityResaveProof.Validate(point,changed,physical,physical)); }
        Refuse<InvalidOperationException>(()=>ColonyActivityResaveProof.Validate(point,Boundary(),physical,new byte[]{1,2,4}));
        Refuse<InvalidOperationException>(()=>ColonyActivityResaveProof.Validate(point,Boundary(),Array.Empty<byte>(),Array.Empty<byte>()));
        var invalid=Boundary();invalid.Session=0;Refuse<InvalidOperationException>(()=>ColonyActivityResaveProof.Validate(invalid,invalid,physical,physical));
        var liveHandles=new HashSet<int>();var allocations=0;var commits=0;var rollbacks=0;
        int Bind(int index){var handle=++allocations;liveHandles.Add(handle);return handle;}
        void Undo(int handle){Check(liveHandles.Remove(handle));++rollbacks;}
        void Commit(int[] values){++commits;}
        foreach(var late in new[]{"Actor before tracking","Actor before native handle","Consumed data before spawn","Emitted callback before registration"})
        {
            for(var retry=0;retry<4;++retry)
            {
                Check(!ColonyCallbackRebindPlan.TryCommit(new[]{ColonyCallbackReadiness.Ready,ColonyCallbackReadiness.Waiting},false,Bind,Commit,Undo,out int[] none));
                Check(none.Length==0&&allocations==0&&commits==0&&liveHandles.Count==0);
            }
        }
        Check(ColonyCallbackRebindPlan.TryCommit(new[]{ColonyCallbackReadiness.Ready,ColonyCallbackReadiness.Ready,ColonyCallbackReadiness.Unsupported},false,Bind,Commit,Undo,out int[] bound));
        Check(bound.Length==3&&bound[0]>0&&bound[1]>0&&bound[2]==0&&allocations==2&&commits==1);
        foreach(var handle in bound.Where(h=>h!=0))Undo(handle);
        Check(ColonyCallbackRebindPlan.TryCommit(new[]{ColonyCallbackReadiness.Waiting,ColonyCallbackReadiness.Unsupported},true,Bind,Commit,Undo,out int[] released));
        Check(released.SequenceEqual(new[]{0,0})&&allocations==2&&commits==2);
        var beforeRollback=rollbacks;
        Refuse<InvalidOperationException>(()=>ColonyCallbackRebindPlan.TryCommit(new[]{ColonyCallbackReadiness.Ready,ColonyCallbackReadiness.Ready},false,
            i=>i==1?throw new InvalidOperationException("injected create failure"):Bind(i),Commit,Undo,out int[] _));
        Check(liveHandles.Count==0&&rollbacks==beforeRollback+1&&commits==2);
        beforeRollback=rollbacks;
        Refuse<InvalidOperationException>(()=>ColonyCallbackRebindPlan.TryCommit(new[]{ColonyCallbackReadiness.Ready,ColonyCallbackReadiness.Ready},false,
            Bind,_=>throw new InvalidOperationException("injected native commit failure"),Undo,out int[] _));
        Check(liveHandles.Count==0&&rollbacks==beforeRollback+2);
        Refuse<ArgumentException>(()=>ColonyCallbackRebindPlan.TryCommit(new[]{(ColonyCallbackReadiness)99},false,Bind,Commit,Undo,out int[] _));
        checks += ActivityCheckpointChecks.Run();
        if (args.Length != 0) checks += ActivityCheckpointChecks.ReadNativeImages(args[0]);
        Console.WriteLine("PASS colony activity geometry, lifecycle snapshots and exact restored delivery: " + checks + " checks");
    }
}
