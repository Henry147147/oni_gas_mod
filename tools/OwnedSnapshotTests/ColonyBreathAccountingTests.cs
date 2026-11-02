using System;
using MixedAtmospheres.Oni;

internal static class ColonyBreathAccountingTests
{
    private const double Parcel = .03125;
    private static ColonyBreathAccounting Start(double trace = 1e-7) => new ColonyBreathAccounting(7, 10, Parcel, 0, trace);
    internal static void Run(System.Action<string,System.Action> test,System.Action<bool> check)
    {
        test("breath accounting keeps an exact deposit with no booked loss",()=> {
            var a = Start();
            a.Observe(7, 11, 0, 1e-7);
            check(!a.Deposited);
            a.Observe(7, 12, Parcel, 1e-7 + 3e-9); // unrelated colony trace is not a loss
            a.Observe(7, 13, Parcel, 1e-7 + 3e-9);
            check(a.Valid && a.Deposited && a.Compatible && a.LostKg == 0 && a.PairedTraceKg == 0 && a.TotalTraceKg > 0);
        });
        test("breath accounting pairs each loss with the sink booked in the same interval",()=> {
            // The first in-game endpoint: 0.031249999647146417 kg of 0.03125 kg.
            const double endpoint = .031249999647146417;
            var a = Start();
            a.Observe(7, 12, Parcel, 1e-7);
            a.Observe(7, 13, Parcel - 1e-10, 1e-7 + 2.7e-9);
            a.Observe(7, 14, endpoint, 1e-7 + 2.7e-9 + 1e-9);
            check(a.Valid && a.Compatible && a.LossIntervals == 2 && Math.Abs(a.LostKg - (Parcel - endpoint)) < 1e-15);
            check(Math.Abs(a.PairedTraceKg - (2.7e-9 + 1e-9)) < 1e-15 && a.PairedTraceKg < Parcel);
        });
        test("breath accounting refuses a loss when no sink was booked in that interval",()=> {
            var a = Start();
            a.Observe(7, 12, Parcel, 1e-7);
            a.Observe(7, 13, Parcel - 3.5e-10, 1e-7); // loss without any sink
            check(!a.Valid && !a.Compatible);
            // Trace booked in a later interval cannot pay for it.
            var b = Start();
            b.Observe(7, 12, Parcel, 1e-7);
            b.Observe(7, 13, Parcel - 3.5e-10, 1e-7);
            b.Observe(7, 14, Parcel - 3.5e-10, 1e-7 + 1e-6);
            check(!b.Valid && !b.Compatible);
        });
        test("breath accounting refuses a loss larger than that interval's sink",()=> {
            var a = Start();
            a.Observe(7, 12, Parcel, 1e-7);
            a.Observe(7, 13, Parcel - 2e-9, 1e-7 + 1e-9);
            check(!a.Valid && !a.Compatible);
        });
        test("breath accounting refuses missing parcels, excess or rising fluid",()=> {
            var none = Start();
            none.Observe(7, 12, 0, 1e-7 + 1e-9);
            none.Observe(7, 13, 0, 1e-7 + 2e-9);
            check(none.Valid && !none.Deposited && !none.Compatible);
            var excess = Start();
            excess.Observe(7, 12, Parcel + 1e-8, 1e-7);
            check(!excess.Valid && !excess.Compatible);
            var rising = Start();
            rising.Observe(7, 12, Parcel - 1e-9, 1e-7 + 1e-9);
            rising.Observe(7, 13, Parcel, 1e-7 + 1e-9);
            check(!rising.Valid && !rising.Compatible);
            var half = Start();
            half.Observe(7, 12, Parcel / 2, 1e-7 + 1e-9);
            check(!half.Valid && !half.Compatible);
            // The sink's informative bound must stay below the parcel.
            var huge = Start();
            huge.Observe(7, 12, Parcel, 1e-7);
            huge.Observe(7, 13, 1e-9, 1e-7 + 1);
            check(huge.Valid && !huge.Compatible);
        });
        test("breath accounting fails closed on session, clock and scalar faults",()=> {
            var a = Start(); a.Observe(8, 12, Parcel, 1e-7); check(!a.Valid && !a.Compatible);
            var b = Start(); b.Observe(7, 9, Parcel, 1e-7); check(!b.Valid);
            var c = Start(); c.Observe(7, 12, Parcel, 1e-8); check(!c.Valid);
            foreach(var bad in new[]{double.NaN,double.PositiveInfinity,double.NegativeInfinity,-1.0})
            {
                var d = Start(); d.Observe(7, 12, bad, 1e-7); check(!d.Valid && !d.Compatible);
                var e = Start(); e.Observe(7, 12, Parcel, bad); check(!e.Valid && !e.Compatible);
            }
            check(!new ColonyBreathAccounting(0, 10, Parcel, 0, 1e-7).Valid);
            check(!new ColonyBreathAccounting(7, 10, double.NaN, 0, 1e-7).Valid);
            check(!new ColonyBreathAccounting(7, 10, Parcel, 1e-9, 1e-7).Valid);
            check(!new ColonyBreathAccounting(7, 10, Parcel, 0, -1).Valid);
            var same = Start(); same.Observe(7, 10, Parcel, 5); check(same.Valid && !same.Deposited); // a repeated frame is ignored
        });
    }
}
