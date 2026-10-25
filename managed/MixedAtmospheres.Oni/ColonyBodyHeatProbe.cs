using System;
using System.Globalization;
using System.IO;

namespace MixedAtmospheres.Oni
{
    // Private diagnostic reader. A zero-energy request traverses the normal
    // native message path; the backend trace supplies actual registration.
    internal static class ColonyBodyHeatProbe
    {
        internal readonly struct Sample
        {
            internal readonly ulong Frame;
            internal readonly float Before,After,PerCellCapacity,Capacity;
            internal readonly int Area,Validity;
            internal Sample(ulong frame,uint before,uint after,uint capacity,int area,int validity)
            {
                Frame=frame;Before=BitConverter.Int32BitsToSingle(unchecked((int)before));After=BitConverter.Int32BitsToSingle(unchecked((int)after));
                PerCellCapacity=BitConverter.Int32BitsToSingle(unchecked((int)capacity));Area=area;Validity=validity;
                Capacity=(float)area*PerCellCapacity;
            }
            internal bool Valid=>Validity==3&&Area>0&&Capacity>0&&!float.IsNaN(Capacity)&&!float.IsInfinity(Capacity)&&
                Before>0&&After>0&&!float.IsNaN(Before)&&!float.IsInfinity(Before)&&!float.IsNaN(After)&&!float.IsInfinity(After);
            internal bool Unchanged=>BitConverter.SingleToInt32Bits(Before)==BitConverter.SingleToInt32Bits(After);
        }
        internal static bool TryReadZeroWork(int handle,out Sample sample)
        {
            sample=default;var path=Environment.GetEnvironmentVariable("MA_ONI_BODY_HEAT_TRACE");
            if(string.IsNullOrEmpty(path)||!File.Exists(path))return false;
            var found=false;
            using(var stream=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.ReadWrite))
            using(var reader=new StreamReader(stream))
            {
                string? line;
                while((line=reader.ReadLine())!=null)
                {
                    var fields=line.Split('\t');
                    if(fields.Length!=10||!ulong.TryParse(fields[0],NumberStyles.None,CultureInfo.InvariantCulture,out var frame)||
                        !int.TryParse(fields[1],NumberStyles.Integer,CultureInfo.InvariantCulture,out var body)||body!=handle||
                        !uint.TryParse(fields[2],NumberStyles.HexNumber,CultureInfo.InvariantCulture,out var energy)||(energy&0x7fffffff)!=0)continue;
                    if(!uint.TryParse(fields[5],NumberStyles.HexNumber,CultureInfo.InvariantCulture,out var before)||
                        !uint.TryParse(fields[6],NumberStyles.HexNumber,CultureInfo.InvariantCulture,out var after)||
                        !uint.TryParse(fields[7],NumberStyles.HexNumber,CultureInfo.InvariantCulture,out var capacity)||
                        !int.TryParse(fields[8],NumberStyles.Integer,CultureInfo.InvariantCulture,out var area)||
                        !int.TryParse(fields[9],NumberStyles.Integer,CultureInfo.InvariantCulture,out var validity))
                        throw new InvalidDataException("Malformed native zero-energy body observation.");
                    if(found&&frame<sample.Frame)throw new InvalidDataException("Native body observations are out of frame order.");
                    sample=new Sample(frame,before,after,capacity,area,validity);found=true;
                }
            }
            return found;
        }
    }
}
