using System;
using System.Reflection;
using HarmonyLib;

namespace MixedAtmospheres.Oni
{
    [HarmonyPatch(typeof(ElementFilter),"OnConduitTick")]
    internal static class ColonyMixedPipeFilterPatch
    {
        private static readonly FieldInfo Port=AccessTools.Field(typeof(ElementFilter),"portInfo"),
            Input=AccessTools.Field(typeof(ElementFilter),"inputCell"),Output=AccessTools.Field(typeof(ElementFilter),"outputCell"),
            Filtered=AccessTools.Field(typeof(ElementFilter),"filteredCell"),Filter=AccessTools.Field(typeof(ElementFilter),"filterable"),
            Operation=AccessTools.Field(typeof(ElementFilter),"operational"),Last=AccessTools.Field(typeof(ElementFilter),"lastelementMoved"),
            Controller=AccessTools.Field(typeof(ElementFilter),"controller"),Selectable=AccessTools.Field(typeof(ElementFilter),"selectable"),
            BlockedGuid=AccessTools.Field(typeof(ElementFilter),"conduitBlockedStatusItemGuid");
        private static readonly Operational.Flag RoomFlag=(Operational.Flag)AccessTools.Field(typeof(RequireOutputs),"pipesHaveRoomFlag").GetValue(null);
        private static readonly FieldInfo PortType=AccessTools.Field(Port.FieldType,"conduitType");
        private static bool Prefix(ElementFilter __instance)
        {
            if(!TryInputs(__instance,out var flow,out var source,out var contents,out var wanted))return true;
            RefreshRoom(__instance,flow,contents,wanted);
            var filteredCell=(int)Filtered.GetValue(__instance);var outputCell=(int)Output.GetValue(__instance);
            var operational=(Operational)Operation.GetValue(__instance);
            if(!operational.IsOperational){operational.SetActive(false,false);return false;}
            var filtered=ColonyConduits.TransferSelected(flow,source,filteredCell,wanted);
            var passed=ColonyConduits.TransferSelected(flow,source,outputCell,wanted,true);
            var moved=filtered.Count>0||passed.Count>0;
            if(moved)
            {
                var element=ColonyConduits.ElementHash(contents.Dominant);
                if(flow.conduitType==ConduitType.Liquid&&(SimHashes)Last.GetValue(__instance)!=element)
                    GameUtil.TintLiquidSymbolOnBuilding("liquid",(KBatchedAnimController)Controller.GetValue(__instance),ElementLoader.FindElementByHash(element));
                Last.SetValue(__instance,element);
            }
            operational.SetActive(moved,false);return false;
        }
        private static bool TryInputs(ElementFilter instance,out ConduitFlow flow,out int source,out PipeInventory contents,out int wanted)
        {
            flow=null!;source=-1;contents=PipeInventory.Empty;wanted=-1;if(!ColonyConduits.Active)return false;
            var type=(ConduitType)PortType.GetValue(Port.GetValue(instance));if(type!=ConduitType.Gas&&type!=ConduitType.Liquid)return false;
            flow=Conduit.GetFlowManager(type);source=(int)Input.GetValue(instance);
            if(!ColonyConduits.TryRead(flow,source,out contents))return false;
            var selected=((Filterable)Filter.GetValue(instance)).SelectedTag;
            foreach(var part in contents.Parts)if(ElementLoader.FindElementByHash(ColonyConduits.ElementHash(part.Id)).tag==selected){wanted=part.Id;break;}
            return true;
        }
        private static void RefreshRoom(ElementFilter instance,ConduitFlow flow,PipeInventory contents,int wanted)
        {
            var room=Room(instance,flow,contents,wanted,out var filteredRoom);
            ((Operational)Operation.GetValue(instance)).SetFlag(RoomFlag,room);
            var blocked=(Guid)BlockedGuid.GetValue(instance);
            if((blocked!=Guid.Empty)==filteredRoom)
                BlockedGuid.SetValue(instance,((KSelectable)Selectable.GetValue(instance)).ToggleStatusItem(
                    Db.Get().BuildingStatusItems.ConduitBlockedMultiples,blocked,!filteredRoom,null));
        }
        private static bool Room(ElementFilter instance,ConduitFlow flow,PipeInventory contents,int wanted,out bool filteredRoom)
        {
            filteredRoom=ColonyConduits.HasRoom(flow,(int)Filtered.GetValue(instance));var outputRoom=ColonyConduits.HasRoom(flow,(int)Output.GetValue(instance));
            var selected=wanted>=0;var others=contents.Count>(selected?1:0);
            return selected&&filteredRoom||others&&outputRoom||contents.Count==0&&(filteredRoom||outputRoom);
        }
        internal static void ReadOutputRoom(RequireOutputs component,ref bool room)
        {
            if(!ColonyConduits.Active)return;
            var filter=component.GetComponent<ElementFilter>();
            if(filter!=null&&TryInputs(filter,out var flow,out _,out var contents,out var wanted))room=Room(filter,flow,contents,wanted,out _);
        }
    }
    [HarmonyPatch(typeof(RequireOutputs),"OutputPipeIsEmpty")]
    internal static class ColonyMixedFilterRoomPatch
    {private static void Postfix(RequireOutputs __instance,ref bool __result)=>ColonyMixedPipeFilterPatch.ReadOutputRoom(__instance,ref __result);}
}
