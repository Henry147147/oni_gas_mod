using MixedAtmospheres.Oni;

static void Check(bool ok,string message){if(!ok)throw new Exception(message);}
static void Refuse(Action action){try{action();}catch(ArgumentException){return;}throw new Exception("Invalid cluster layout accepted.");}
var asteroid=new ColonyWorldRegion(7,0,0,128,256);
var second=new ColonyWorldRegion(2,128,0,128,256);
var rocket=new ColonyWorldRegion(19,0,512,32,32,true);
var input=new[]{rocket,asteroid,second};var layout=new ColonyWorldLayout(256,1024,input);
input[0]=default;
Check(layout.Worlds.Select(w=>w.Id).SequenceEqual(new[]{2,7,19}),"World order or caller mutation changed ownership.");
Check(layout.ActiveRectangleCells==2*128*256+32*32,"Reserved grid holes were counted as active world area.");
Check(layout.TryFind(127,out var a)&&a.Id==7&&layout.TryFind(128,out var b)&&b.Id==2,"Touching world boundaries aliased ownership.");
Check(layout.TryFind(256*512+31,out var r)&&r.Id==19&&r.RocketInterior,"Rocket world was treated as its parent asteroid.");
foreach(var cell in new[]{-1,256*1024,256*400,256*512+32})Check(!layout.TryFind(cell,out _),"Unallocated cells acquired a world.");
Refuse(()=>new ColonyWorldLayout(256,1024,Array.Empty<ColonyWorldRegion>()));
Refuse(()=>new ColonyWorldLayout(int.MaxValue,2,new[]{asteroid}));
Refuse(()=>new ColonyWorldLayout(256,1024,new[]{asteroid,asteroid}));
Refuse(()=>new ColonyWorldLayout(256,1024,new[]{asteroid,new ColonyWorldRegion(8,127,0,2,2,true)}));
Refuse(()=>new ColonyWorldLayout(256,1024,new[]{new ColonyWorldRegion(0,int.MaxValue,0,2,2)}));
Refuse(()=>new ColonyWorldLayout(256,1024,new[]{new ColonyWorldRegion(0,0,0,-1,2)}));
var afterRemoval=new ColonyWorldLayout(256,1024,new[]{asteroid,second});
Check(!afterRemoval.TryFind(256*512,out _)&&layout.TryFind(256*512,out _),"Removing a rocket mutated a prior layout snapshot.");
Console.WriteLine("PASS cluster world layout: stable IDs, immutable snapshots, asteroid/rocket boundaries, unallocated cells, overlap/overflow rejection and removal");
