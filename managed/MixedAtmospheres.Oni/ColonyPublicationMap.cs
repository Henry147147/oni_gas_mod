using System;

namespace MixedAtmospheres.Oni
{
    // Geometry is fixed for one owner session. Keep the exact public cell
    // mapping; native terrain, event and Grid.Element checks still run fresh.
    internal static class ColonyPublicationMap
    {
        internal static int[] Create(int gridWidth,int gridHeight,int x,int y,int width,int height)
        {
            if(gridWidth<=0 || gridHeight<=0 || (long)gridWidth*gridHeight>int.MaxValue ||
                width<=0 || height<=0 || width>gridWidth || height>gridHeight ||
                x<0 || y<0 || x>gridWidth-width || y>gridHeight-height)
                throw new ArgumentOutOfRangeException(nameof(width),"Invalid owner publication rectangle.");
            var result=new int[checked(width*height)];
            for(var row=0;row<height;++row)
            {
                var first=checked((y+row)*gridWidth+x);
                for(var column=0;column<width;++column)result[row*width+column]=first+column;
            }
            return result;
        }
    }
}
