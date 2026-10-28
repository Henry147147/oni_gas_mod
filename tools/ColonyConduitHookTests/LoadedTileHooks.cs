using System.Reflection;
using System.Reflection.Emit;
using System.Runtime.InteropServices;
using System.Runtime.CompilerServices;

internal static class LoadedTileHooks
{
    internal static void Run(Assembly assembly)
    {
        const BindingFlags all=BindingFlags.Static|BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic;
        void Check(bool ok,string reason){if(!ok)throw new Exception(reason);}
        var runtime=assembly.GetType("MixedAtmospheres.Oni.ColonyLoadedTiles")!;
        var current=runtime.GetField("current",all)!;
        var scope=runtime.GetNestedType("Scope",all)!;
        var projection=assembly.GetType("MixedAtmospheres.Oni.ColonyTileLoadProjection")!;
        var reset=runtime.GetMethod("Reset",all)!;
        var method=new DynamicMethod("ObservedTileRewrite",typeof(void),new[]{typeof(int),typeof(int),typeof(IntPtr)},assembly.ManifestModule,true);
        var il=method.GetILGenerator();il.Emit(OpCodes.Ldarg_0);il.Emit(OpCodes.Ldarg_1);il.Emit(OpCodes.Ldarg_2);il.Emit(OpCodes.Conv_U);
        il.Emit(OpCodes.Call,runtime.GetMethod("Rewrite",all)!);il.Emit(OpCodes.Ret);
        var rewrite=(Action<int,int,IntPtr>)method.CreateDelegate(typeof(Action<int,int,IntPtr>));
        object Cells()
        {
            var rows=Activator.CreateInstance(current.FieldType)!;
            var saved=Activator.CreateInstance(projection,all,null,new object[]{7,(ushort)68,100f,299.5f,(byte)255,0},null)!;
            current.FieldType.GetMethod("Add")!.Invoke(rows,new[]{(object)7,saved});return rows;
        }
        IDisposable Scope(object? rows)=>(IDisposable)Activator.CreateInstance(scope,all,null,new[]{rows},null)!;
        byte[] Packet()
        {
            var value=new byte[28];BitConverter.GetBytes(7).CopyTo(value,0);BitConverter.GetBytes(350f).CopyTo(value,8);
            BitConverter.GetBytes(100f).CopyTo(value,12);BitConverter.GetBytes((ushort)68).CopyTo(value,20);value[22]=2;value[23]=255;return value;
        }
        byte[] Apply(int message=-1252920804)
        {
            var value=Packet();var handle=GCHandle.Alloc(value,GCHandleType.Pinned);
            try{rewrite(message,value.Length,handle.AddrOfPinnedObject());}finally{handle.Free();}return value;
        }
        reset.Invoke(null,null);
        Check(Apply().SequenceEqual(Packet()),"Ordinary construction was changed outside saved-tile startup.");
        var rows=Cells();var outer=Scope(rows);
        try
        {
            Check(BitConverter.ToSingle(Apply(),8)==299.5f,"A live saved-tile scope did not use its loaded cell.");
            Check(Apply(818320644).SequenceEqual(Packet()),"A heat message was treated as tile recreation.");
            using(Scope(null))Check(Apply().SequenceEqual(Packet()),"Nested unrelated construction inherited an outer tile scope.");
            Check(ReferenceEquals(current.GetValue(null),rows),"Nested scope lost its parent.");
            var isolated=false;var thread=new Thread(()=>isolated=current.GetValue(null)==null&&Apply().SequenceEqual(Packet()));thread.Start();thread.Join();
            Check(isolated,"A tile startup scope leaked to another thread.");
            var failure=new InvalidOperationException("original OnSpawn failure");
            var finalizer=assembly.GetType("MixedAtmospheres.Oni.ColonyLoadedTileSpawnPatch")!.GetMethod("Finalizer",all)!;
            Check(ReferenceEquals(finalizer.Invoke(null,new object[]{failure,outer}),failure)&&current.GetValue(null)==null,
                "An original startup failure was hidden or left its rewrite scope active.");
            // This object is only an identity key; no Unity component method
            // or equality-to-null is called in this pending-entry check.
            var tile=RuntimeHelpers.GetUninitializedObject(typeof(PrimaryElement));
            var pending=(System.Collections.IDictionary)runtime.GetField("tiles",all)!.GetValue(null)!;
            pending.Add(tile,rows);
            using(var failed=Scope(rows))
            {
                scope.GetMethod("Track",all)!.Invoke(failed,new[]{tile});
                finalizer.Invoke(null,new object?[]{failure,failed});
                Check(pending.Count==1&&current.GetValue(null)==null,"Failed tile startup was accepted as completed.");
            }
            using(var succeeded=Scope(rows))
            {
                scope.GetMethod("Track",all)!.Invoke(succeeded,new[]{tile});
                finalizer.Invoke(null,new object?[]{null,succeeded});
                Check(pending.Count==0&&current.GetValue(null)==null,"Successful startup kept an unresolved tile.");
            }
            using var stale=Scope(rows);reset.Invoke(null,null);stale.Dispose();
            Check(current.GetValue(null)==null,"A disposed scope revived tile state from a prior colony session.");
        }
        finally{outer.Dispose();reset.Invoke(null,null);}
        Check(Apply().SequenceEqual(Packet()),"Completed startup changed later construction.");
        var prefix=assembly.GetType("MixedAtmospheres.Oni.ColonyLoadedTileSpawnPatch")!.GetMethod("Prefix",all)!;
        Check(prefix.ReturnType==typeof(void),"The tile scope skips the game's native registration/callback path.");
        Console.WriteLine("PASS saved tile scopes: inactive/foreign messages unchanged, nested construction isolated, thread/session separation and original failure cleanup");
    }
}
