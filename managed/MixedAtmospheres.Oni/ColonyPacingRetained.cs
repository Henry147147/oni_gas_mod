using System;

namespace MixedAtmospheres.Oni
{
    internal readonly struct ColonyRetainedToken
    {
        internal readonly long Epoch, Sequence;
        internal readonly int Depth;
        internal readonly float Raw, Candidate;
        internal readonly bool Eligible;
        internal ColonyRetainedToken(long epoch,long sequence,int depth,float raw,float candidate,bool eligible)
        {Epoch=epoch;Sequence=sequence;Depth=depth;Raw=raw;Candidate=candidate;Eligible=eligible;}
    }
    internal struct ColonyRetainedTotals
    {
        internal long Calls, Busy, Clipped, Held, Ignored, Vetoed, Exceptions, Resets, Overflows, Errors;
        internal double Offered, Admitted, Dropped, IgnoredSeconds, ResetSeconds, Repaid, MaximumOwed, UnexpectedAdded, ProposedExtra;
        internal long RepaymentOffers, UnclippedLoans, ClippedLoans;
    }
    // Bounded execution pacing state, never physical or checkpoint state.
    internal sealed class ColonyPacingRetained
    {
        internal const double MaximumOwed = .4;
        internal const float MaximumArgument = .2f;
        internal const float MaximumExtra = 1f/60f;
        internal const int Capacity=8;
        private struct Scope {internal long Sequence;internal bool Seen,Busy,Returned,AdmissionSeen,WillRun;internal float Before,After,Admission;}
        private readonly Scope[] scopes=new Scope[Capacity];
        internal double OwedLimit { get; }
        internal float ExtraLimit { get; }
        internal ColonyPacingRetained(double maximumOwed=MaximumOwed,float maximumExtra=MaximumExtra)
        {
            if(double.IsNaN(maximumOwed)||double.IsInfinity(maximumOwed)||maximumOwed<=0||maximumOwed>2)
                throw new ArgumentOutOfRangeException(nameof(maximumOwed));
            if(float.IsNaN(maximumExtra)||float.IsInfinity(maximumExtra)||maximumExtra<=0||maximumExtra>1f/30f)
                throw new ArgumentOutOfRangeException(nameof(maximumExtra));
            OwedLimit=maximumOwed;ExtraLimit=maximumExtra;
        }
        internal static double ReadMaximumOwed(string? raw)
        {
            return double.TryParse(raw,System.Globalization.NumberStyles.Float,System.Globalization.CultureInfo.InvariantCulture,out var value)
                && !double.IsNaN(value) && !double.IsInfinity(value) && value>0 && value<=2 ? value : MaximumOwed;
        }
        internal static float ReadMaximumExtra(string? raw)
        {
            return float.TryParse(raw,System.Globalization.NumberStyles.Float,System.Globalization.CultureInfo.InvariantCulture,out var value)
                && !float.IsNaN(value) && !float.IsInfinity(value) && value>0 && value<=1f/30f ? value : MaximumExtra;
        }
        private long epoch=1,sequence;
        private int depth;
        private double owed;
        private ColonyRetainedTotals totals;
        internal double Owed=>owed;
        internal int Depth=>depth;
        internal ColonyRetainedTotals Snapshot=>totals;
        private static float Positive(float x)=>!float.IsNaN(x)&&!float.IsInfinity(x)&&x>0?x:0;
        private static float FloorFloat(double x)
        {
            var result=(float)x;
            if(result>x)result=BitConverter.Int32BitsToSingle(BitConverter.SingleToInt32Bits(result)-1);
            return result;
        }
        internal ColonyRetainedToken Prepare(ref float dt,bool eligible)
        {
            var raw=Positive(dt);
            var next=raw;
            if(eligible&&raw>0)
            {
                // Nested positive calls admit their own capped time, never borrow
                // debt tentatively offered to the outer call.
                next=FloorFloat(Math.Min(MaximumArgument,(double)raw+(depth==0?Math.Min(owed,ExtraLimit):0)));
                dt=next;
            }
            if(depth==0 && eligible && raw>0)
            {
                var proposed=Math.Max(0,(double)next-raw);totals.ProposedExtra+=proposed;
                if(proposed>0)++totals.RepaymentOffers;
            }
            var token=new ColonyRetainedToken(epoch,++sequence,++depth,raw,next,eligible&&raw>0);
            if(depth<=Capacity)scopes[depth-1]=new Scope{Sequence=token.Sequence};
            else ++totals.Overflows;
            return token;
        }
        internal void Pacing(float before,float after,bool returned,bool busy)
        {
            if(depth<=0||depth>Capacity)return;
            ref var scope=ref scopes[depth-1];
            scope.Seen=true;scope.Busy=busy;scope.Returned=returned;scope.Before=Positive(before);scope.After=returned?Positive(after):0;
        }
        internal void Admission(float dt,bool originalWillRun)
        {
            if(depth<=0||depth>Capacity)return;
            ref var scope=ref scopes[depth-1];scope.AdmissionSeen=true;scope.WillRun=originalWillRun;scope.Admission=dt;
        }
        private bool Take(ColonyRetainedToken token,out Scope scope)
        {
            scope=default;
            if(token.Epoch!=epoch||token.Depth<=0||token.Depth>depth)return false;
            if(token.Depth!=depth){++totals.Errors;Reset();return false;}
            if(depth<=Capacity)
            {
                if(scopes[depth-1].Sequence!=token.Sequence)return false;
                scope=scopes[depth-1];scopes[depth-1]=default;
            }
            --depth;return true;
        }
        internal void Commit(ColonyRetainedToken token,float finalDt,bool originalRan,bool allowRetention=true)
        {
            if(!Take(token,out var scope))return;
            ++totals.Calls;totals.Offered+=token.Raw;
            var admitted=originalRan?Math.Min(MaximumArgument,Positive(scope.AdmissionSeen?scope.Admission:finalDt)):0;
            totals.Admitted+=admitted;
            if(scope.Busy)++totals.Busy;
            if(scope.Busy&&scope.Returned&&scope.After<scope.Before)++totals.Clipped;
            var held=scope.Seen&&scope.Busy&&!scope.Returned&&scope.Before>0;
            if(held)++totals.Held;
            var finalUnchanged=scope.AdmissionSeen&&scope.WillRun==originalRan&&scope.Admission==finalDt;
            var verified=finalUnchanged&&scope.Seen&&scope.Returned&&scope.Admission==scope.After;
            if(token.Candidate>token.Raw)
            {
                if(originalRan && admitted==token.Candidate && verified)++totals.UnclippedLoans;
                else ++totals.ClippedLoans;
            }
            var retain=allowRetention&&token.Eligible&&token.Depth<=Capacity&&((originalRan&&admitted>0&&verified)||(!originalRan&&held&&finalUnchanged));
            if(!retain)
            {
                // A guard's positive truncation is intentional. Never replay
                // its discarded input. Consume only debt actually admitted;
                // unused tentative repayment stays owed after a veto.
                var loan=Math.Max(0,(double)admitted-token.Raw);
                totals.Repaid+=Math.Min(loan,owed);
                var added=Math.Max(0,loan-owed);totals.UnexpectedAdded+=added;
                owed=Math.Max(0,owed-loan);
                var ignored=Math.Max(0,(double)token.Raw-admitted);
                totals.Dropped+=ignored;totals.IgnoredSeconds+=ignored;++totals.Ignored;
                if(token.Eligible)++totals.Vetoed;
                if(added>0||admitted>token.Candidate){++totals.Errors;Reset();}
                // A postfix changed the argument after its observed use.
                // Account that known pre-original input, then clear debt.
                if(scope.AdmissionSeen&&scope.Admission!=finalDt)Reset();
                return;
            }
            var available=owed+token.Raw;
            if(admitted>available||admitted>token.Candidate)
            {++totals.Errors;Reset();return;}
            totals.Repaid+=Math.Max(0,(double)admitted-token.Raw);
            var next=available-admitted;
            var overflow=Math.Max(0,next-OwedLimit);
            totals.Dropped+=overflow;owed=next-overflow;
            totals.MaximumOwed=Math.Max(totals.MaximumOwed,owed);
        }
        internal void Exception(ColonyRetainedToken token)
        {
            if(!Take(token,out _))return;
            ++totals.Exceptions;++totals.Calls;totals.Offered+=token.Raw;totals.Dropped+=token.Raw;
            Reset();
        }
        internal void Cancel(ColonyRetainedToken token){Take(token,out _);}
        internal void Reset()
        {
            if(owed>0){++totals.Resets;totals.ResetSeconds+=owed;totals.Dropped+=owed;owed=0;}
            ++epoch;depth=0;Array.Clear(scopes,0,scopes.Length);
        }
        internal ColonyRetainedTotals Drain(){var result=totals;totals=default;return result;}
    }
}
