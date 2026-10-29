using System;
using MixedAtmospheres.Oni;

internal static class ColonyBreathAccountingTests
{
    internal static void Run(System.Action<string,System.Action> test,System.Action<bool> check)
    {
        test("breath trace interval retains exact deposits and explicitly booked endpoint loss",()=> {
            const double parcel=.03125;
            check(ColonyBreathAccounting.Compatible(parcel,parcel,0));
            const double observed=.031249999647146417;
            check(Math.Abs(observed-parcel)>ColonyBreathAccounting.AbsoluteTolerance);
            check(!ColonyBreathAccounting.Compatible(parcel,observed,0));
            check(ColonyBreathAccounting.Compatible(parcel,observed,1e-9));
        });
        test("breath trace interval refuses missing parcels, excess fluid and uninformative sinks",()=> {
            const double parcel=.03125;
            check(!ColonyBreathAccounting.Compatible(parcel,0,0));
            check(!ColonyBreathAccounting.Compatible(parcel,0,parcel));
            check(!ColonyBreathAccounting.Compatible(parcel,parcel+1e-8,1e-9));
            check(!ColonyBreathAccounting.Compatible(parcel,parcel,parcel));
            check(!ColonyBreathAccounting.Compatible(parcel,parcel,-1e-9));
            check(!ColonyBreathAccounting.Compatible(parcel,parcel/2,1e-9));
        });
        test("breath trace interval fails closed on malformed or unsupported scalar inputs",()=> {
            foreach(var bad in new[]{double.NaN,double.PositiveInfinity,double.NegativeInfinity})
            {
                check(!ColonyBreathAccounting.Compatible(bad,.03125,0));
                check(!ColonyBreathAccounting.Compatible(.03125,bad,0));
                check(!ColonyBreathAccounting.Compatible(.03125,.03125,bad));
            }
            check(!ColonyBreathAccounting.Compatible(double.Epsilon,double.Epsilon,0));
        });
    }
}
