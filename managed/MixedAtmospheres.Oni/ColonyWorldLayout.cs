using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;

namespace MixedAtmospheres.Oni
{
    // World IDs, rather than the selected camera world or list position, own
    // these rectangles. Rocket interiors are separate worlds as well.
    internal readonly struct ColonyWorldRegion
    {
        internal readonly int Id,X,Y,Width,Height;
        internal readonly bool RocketInterior;
        internal ColonyWorldRegion(int id,int x,int y,int width,int height,bool rocketInterior=false)
        {Id=id;X=x;Y=y;Width=width;Height=height;RocketInterior=rocketInterior;}
        internal bool Contains(int x,int y)=>x>=X&&y>=Y&&(long)x<X+(long)Width&&(long)y<Y+(long)Height;
        internal bool SameRegion(ColonyWorldRegion other)=>Id==other.Id&&X==other.X&&Y==other.Y&&
            Width==other.Width&&Height==other.Height&&RocketInterior==other.RocketInterior;
    }

    internal sealed class ColonyWorldLayout
    {
        internal readonly int GridWidth,GridHeight;
        internal readonly ReadOnlyCollection<ColonyWorldRegion> Worlds;
        internal readonly long ActiveRectangleCells;
        internal ColonyWorldLayout(int gridWidth,int gridHeight,IEnumerable<ColonyWorldRegion> worlds)
        {
            if(gridWidth<=0||gridHeight<=0||(long)gridWidth*gridHeight>int.MaxValue)
                throw new ArgumentOutOfRangeException(nameof(gridWidth),"Invalid cluster grid.");
            if(worlds==null)throw new ArgumentNullException(nameof(worlds));
            var ordered=worlds.OrderBy(w=>w.Id).ToArray();
            if(ordered.Length==0||ordered.Length>256)throw new ArgumentException("A cluster needs 1..256 world rectangles.",nameof(worlds));
            long cells=0;
            for(var i=0;i<ordered.Length;++i)
            {
                var world=ordered[i];
                if(world.Id<0||world.Width<=0||world.Height<=0||world.X<0||world.Y<0||
                    (long)world.X+world.Width>gridWidth||(long)world.Y+world.Height>gridHeight)
                    throw new ArgumentException("A world rectangle leaves the cluster grid.",nameof(worlds));
                if(i>0&&ordered[i-1].Id==world.Id)throw new ArgumentException("Duplicate world identity.",nameof(worlds));
                for(var j=0;j<i;++j)
                {
                    var other=ordered[j];
                    if(world.X<(long)other.X+other.Width&&other.X<(long)world.X+world.Width&&
                        world.Y<(long)other.Y+other.Height&&other.Y<(long)world.Y+world.Height)
                        throw new ArgumentException("World rectangles overlap.",nameof(worlds));
                }
                cells+=(long)world.Width*world.Height;
            }
            GridWidth=gridWidth;GridHeight=gridHeight;Worlds=Array.AsReadOnly(ordered);ActiveRectangleCells=cells;
        }
        internal bool TryFind(int publicCell,out ColonyWorldRegion world)
        {
            if(publicCell>=0&&(long)publicCell<(long)GridWidth*GridHeight)
                foreach(var candidate in Worlds)
                    if(candidate.Contains(publicCell%GridWidth,publicCell/GridWidth)){world=candidate;return true;}
            world=default;return false;
        }
        internal int[] CellWorldIds()
        {
            var ids=new int[checked(GridWidth*GridHeight)];
            for(var i=0;i<ids.Length;++i)ids[i]=-1;
            foreach(var world in Worlds)
                for(var y=world.Y;y<world.Y+world.Height;++y)
                    for(var x=world.X;x<world.X+world.Width;++x)ids[x+y*GridWidth]=world.Id;
            return ids;
        }
        // Existing cell coordinates and identities must survive a layout
        // update. A moved/reused identity is not a newly generated world.
        internal (ColonyWorldRegion[] Added,ColonyWorldRegion[] Removed) ChangesFrom(ColonyWorldLayout previous)
        {
            if(previous==null)throw new ArgumentNullException(nameof(previous));
            if(GridWidth!=previous.GridWidth||GridHeight!=previous.GridHeight)
                throw new InvalidOperationException("The cluster grid changed its public cell coordinates.");
            var old=previous.Worlds.ToDictionary(w=>w.Id);var current=Worlds.ToDictionary(w=>w.Id);
            foreach(var world in Worlds)
                if(old.TryGetValue(world.Id,out var before)&&!world.SameRegion(before))
                    throw new InvalidOperationException("An existing world changed its geometry or kind.");
            return(Worlds.Where(w=>!old.ContainsKey(w.Id)).ToArray(),previous.Worlds.Where(w=>!current.ContainsKey(w.Id)).ToArray());
        }
    }
}
