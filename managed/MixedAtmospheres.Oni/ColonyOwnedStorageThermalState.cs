using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace MixedAtmospheres.Oni
{
    [StructLayout(LayoutKind.Sequential)]
    internal readonly struct StorageThermalEndpoint
    {
        internal readonly int Handle;
        internal readonly float Temperature,Capacity;
        internal readonly uint Reserved;
        internal StorageThermalEndpoint(int handle,float temperature,float capacity)
        {
            if(handle==-1||!(temperature>0)||float.IsInfinity(temperature)||BitConverter.SingleToInt32Bits(capacity)<0||float.IsNaN(capacity)||float.IsInfinity(capacity))
                throw new ArgumentException("Invalid exact-storage thermal endpoint.");
            Handle=handle;Temperature=temperature;Capacity=capacity;Reserved=0;
        }
    }

    // An absolute UI update must not queue an older native endpoint after the
    // transfer lease has already installed the final projection. Compare bits,
    // including zero capacity, before suppressing that redundant native write.
    internal sealed class StorageThermalTargets
    {
        private readonly Dictionary<int,StorageThermalEndpoint> targets=new Dictionary<int,StorageThermalEndpoint>();
        private readonly Dictionary<int,StorageThermalEndpoint> sources=new Dictionary<int,StorageThermalEndpoint>();
        private readonly Dictionary<int,StorageThermalEndpoint> publishedSources=new Dictionary<int,StorageThermalEndpoint>();
        internal StorageThermalTargets(StorageThermalEndpoint[] endpoints,StorageThermalEndpoint[]? before=null,StorageThermalEndpoint[]? published=null)
        {
            foreach(var endpoint in endpoints)
            {
                if(endpoint.Reserved!=0)throw new ArgumentException("A storage thermal endpoint has reserved bits.");
                _=new StorageThermalEndpoint(endpoint.Handle,endpoint.Temperature,endpoint.Capacity);
                targets.Add(endpoint.Handle,endpoint);
            }
            foreach(var endpoint in before??endpoints)
            {
                _=new StorageThermalEndpoint(endpoint.Handle,endpoint.Temperature,endpoint.Capacity);
                if(endpoint.Reserved!=0||!targets.ContainsKey(endpoint.Handle))throw new ArgumentException("A storage transition has a foreign thermal source.");
                sources.Add(endpoint.Handle,endpoint);
            }
            if(sources.Count!=targets.Count)throw new ArgumentException("A storage transition is missing a thermal source.");
            foreach(var endpoint in published??before??endpoints)
            {
                _=new StorageThermalEndpoint(endpoint.Handle,endpoint.Temperature,endpoint.Capacity);
                if(endpoint.Reserved!=0||!targets.ContainsKey(endpoint.Handle))throw new ArgumentException("A storage transition has a foreign published source.");
                publishedSources.Add(endpoint.Handle,endpoint);
            }
            if(publishedSources.Count!=targets.Count)throw new ArgumentException("A storage transition is missing a published source.");
        }
        internal bool Matches(int handle,float temperature,float capacity)
        {
            if(!targets.TryGetValue(handle,out var expected))return false;
            if(BitConverter.SingleToInt32Bits(temperature)!=BitConverter.SingleToInt32Bits(expected.Temperature)||
                BitConverter.SingleToInt32Bits(capacity)!=BitConverter.SingleToInt32Bits(expected.Capacity))
                throw new InvalidOperationException("A storage projection changed its prepared thermal endpoint during transfer.");
            return true;
        }
        internal bool Transition(int handle,float temperature,float capacity)
        {
            if(!targets.TryGetValue(handle,out var after))return false;
            var before=sources[handle];var published=publishedSources[handle];
            var t=BitConverter.SingleToInt32Bits(temperature);var c=BitConverter.SingleToInt32Bits(capacity);
            // SetMassTemperature can publish mass and temperature separately.
            // Both intermediate combinations are UI-only while the native
            // lease already holds the final endpoint. Nothing is requeued.
            if((t!=BitConverter.SingleToInt32Bits(before.Temperature)&&t!=BitConverter.SingleToInt32Bits(published.Temperature)&&t!=BitConverter.SingleToInt32Bits(after.Temperature))||
                (c!=BitConverter.SingleToInt32Bits(before.Capacity)&&c!=BitConverter.SingleToInt32Bits(published.Capacity)&&c!=BitConverter.SingleToInt32Bits(after.Capacity)))
                throw new InvalidOperationException("A storage projection left its prepared thermal transition.");
            return true;
        }
    }
}
