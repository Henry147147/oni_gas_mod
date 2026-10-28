using System;

namespace MixedAtmospheres.Oni
{
    internal enum ColonyCallbackReadiness { Unsupported, Waiting, Ready }

    // Preflight every request before allocating any callback handle. A later
    // unresolved actor cannot leak an earlier consumed callback on each retry.
    internal static class ColonyCallbackRebindPlan
    {
        internal static bool TryCommit<T>(ReadOnlySpan<ColonyCallbackReadiness> states, bool final,
            Func<int,T> bind, Action<T[]> commit, Action<T> rollback, out T[] values)
        {
            values=Array.Empty<T>();var waiting=false;
            foreach(var state in states)
            {
                if((uint)state>(uint)ColonyCallbackReadiness.Ready)
                    throw new ArgumentException("Unknown callback readiness.",nameof(states));
                waiting|=state==ColonyCallbackReadiness.Waiting;
            }
            if(waiting&&!final)return false;
            var prepared=new T[states.Length];var created=new bool[states.Length];
            try
            {
                for(var k=0;k<states.Length;++k)
                    if(states[k]==ColonyCallbackReadiness.Ready){prepared[k]=bind(k);created[k]=true;}
                commit(prepared);values=prepared;return true;
            }
            catch(Exception original)
            {
                Exception? cleanup=null;
                for(var k=prepared.Length-1;k>=0;--k)if(created[k])
                    try{rollback(prepared[k]);}catch(Exception error){cleanup=cleanup==null?error:new AggregateException(cleanup,error);}
                if(cleanup!=null)throw new AggregateException(original,cleanup);
                throw;
            }
        }
    }
}
