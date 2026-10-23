using System;
using System.IO;
using System.Linq;
using System.Reflection;

namespace MixedAtmospheres.Oni
{
    internal static partial class ColonyScenarios
    {
        private static void MixedPipeSaveGuard(int frame)
        {
            if(Once(0,"mixed save refusal arena"))Arena((x,y)=>(SimHashes.Oxygen,1f,300f));
            if(!Once(10,"public mixed save ingress refusal"))return;
            ColonyConduits.StartControl();
            var path=Path.Combine(Path.GetTempPath(),"mixed-pipe-save-guard-"+Guid.NewGuid().ToString("N")+".sav");
            var sentinel=new byte[]{19,37,53,71};File.WriteAllBytes(path,sentinel);
            try
            {
                var methods=ColonyMixedPipeSaveGuard.TargetMethods().ToArray();
                foreach(var method in methods)
                {
                    using(var stream=new MemoryStream())
                    using(var writer=new BinaryWriter(stream))
                    {
                        writer.Write(sentinel);writer.Flush();var position=stream.Position;
                        var parameters=method.GetParameters();
                        var args=parameters.Length==0?Array.Empty<object>():parameters.Length==1?new object[]{writer}:new object[]{path,false,false};
                        var refused=false;var detail="original body returned";
                        try{method.Invoke(method.DeclaringType==typeof(Game)?(object)Game.Instance:SaveLoader.Instance,args);}
                        catch(TargetInvocationException error)when(error.InnerException is NotSupportedException refusal&&refusal.Message.Contains("mixed-pipe"))
                        {refused=true;detail=refusal.Message;}
                        var untouched=stream.Position==position&&stream.ToArray().SequenceEqual(sentinel)&&File.ReadAllBytes(path).SequenceEqual(sentinel);
                        Record("early refusal: "+method.DeclaringType!.Name+"."+method.Name+"/"+parameters.Length,refused&&untouched,detail+"; sentinel/writer unchanged="+untouched);
                        if(!refused||!untouched){Finish();return;}
                    }
                }
                Record("save refusal leaves owner and mixed ownership active",ColonyOwner.Active&&ColonyConduits.Active,"No callbacks, native BeginSave, header or payload were requested.");
                Finish();
            }
            finally{File.Delete(path);}
        }
    }
}
