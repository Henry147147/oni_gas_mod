using System.Reflection;
using System.Runtime.CompilerServices;

internal static class StoredBiologyHooks
{
    internal static void Run(Assembly assembly)
    {
        const BindingFlags flags=BindingFlags.NonPublic|BindingFlags.Static;
        var owner=assembly.GetType("MixedAtmospheres.Oni.ColonyOwnedStorage")!;
        var begin=owner.GetMethod("BeginBiology",flags)!;
        if(begin.Invoke(null,new object?[]{null,.2f})!=null||begin.Invoke(null,new object?[]{null,0f})!=null)
            throw new Exception("Inactive storage biology touched a native manager without a game.");
        var access=owner.GetNestedType("StoredBiologyAccess",BindingFlags.NonPublic)!;
        RuntimeHelpers.RunClassConstructor(access.TypeHandle);
        foreach(var name in new[]{"Delta","Subtotal","Evaluate","Tags"})
            if(access.GetField(name,flags)!.GetValue(null) is not Delegate)
                throw new Exception("Stored biology native delegate did not bind: "+name);
        var headers=(FieldInfo)access.GetField("Headers",flags)!.GetValue(null)!;
        var payloads=(FieldInfo)access.GetField("Payloads",flags)!.GetValue(null)!;
        var modifiers=(FieldInfo)access.GetField("Modifiers",flags)!.GetValue(null)!;
        if(headers.FieldType.GetGenericArguments()[0]!=typeof(DiseaseHeader)||payloads.FieldType.GetGenericArguments()[0]!=typeof(DiseaseContainer)||
            modifiers.DeclaringType!=typeof(Storage)||modifiers.FieldType!=typeof(List<Storage.StoredItemModifier>))
            throw new Exception("Stored biology native culture rows or modifier declarations changed.");
        Console.WriteLine("PASS stored biology native bindings: inactive pass-through, original complete/subtotal/cache/tag delegates and declared culture/modifier fields");
    }
}
