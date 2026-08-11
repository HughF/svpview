#include "sv_ocean.h"
#include <math.h>

double sv_gravity(double lat_deg)
{
    double s = sin(lat_deg * M_PI / 180.0);
    double s2 = s * s;
    return 9.780318 * (1.0 + 5.2788e-3 * s2 + 2.36e-5 * s2 * s2);
}

double sv_depth_from_pressure(double p, double lat_deg)
{
    if (p <= 0.0)
        return 0.0;

    double num = (((-1.82e-15 * p + 2.279e-10) * p - 2.2512e-5) * p + 9.72659) * p;
    return num / (sv_gravity(lat_deg) + 1.092e-6 * p);
}

double sv_soundspeed(double s, double t, double p_dbar)
{
    if (s < 0.0) s = 0.0;
    double p = p_dbar / 10.0;                      /* Chen & Millero use bar */
    double t2 = t * t, t3 = t2 * t, t4 = t3 * t, t5 = t4 * t;
    double p2 = p * p, p3 = p2 * p;

    double cw = (1402.388 + 5.03711 * t - 5.80852e-2 * t2 + 3.3420e-4 * t3
                 - 1.47800e-6 * t4 + 3.1464e-9 * t5)
              + (0.153563 + 6.8982e-4 * t - 8.1788e-6 * t2 + 1.3621e-7 * t3
                 - 6.1185e-10 * t4) * p
              + (3.1260e-5 - 1.7107e-6 * t + 2.5974e-8 * t2 - 2.5335e-10 * t3
                 + 1.0405e-12 * t4) * p2
              + (-9.7729e-9 + 3.8504e-10 * t - 2.3643e-12 * t2) * p3;

    double a = (1.389 - 1.262e-2 * t + 7.166e-5 * t2 + 2.008e-6 * t3
                - 3.21e-8 * t4)
             + (9.4742e-5 - 1.2583e-5 * t - 6.4928e-8 * t2 + 1.0515e-8 * t3
                - 2.0142e-10 * t4) * p
             + (-3.9064e-7 + 9.1061e-9 * t - 1.6009e-10 * t2 + 7.994e-12 * t3) * p2
             + (1.100e-10 + 6.651e-12 * t - 3.391e-13 * t2) * p3;

    double b = (-1.922e-2 - 4.42e-5 * t) + (7.3637e-5 + 1.7950e-7 * t) * p;
    double d = 1.727e-3 - 7.9836e-6 * p;

    return cw + a * s + b * s * sqrt(s) + d * s * s;
}

/* PSS-78 forward: conductivity ratio R -> salinity. */
static double pss78(double r, double t, double p)
{
    static const double a[6] = { 0.0080, -0.1692, 25.3851, 14.0941, -7.0261, 2.7081 };
    static const double b[6] = { 0.0005, -0.0056, -0.0066, -0.0375, 0.0636, -0.0144 };

    double rt_t = 0.6766097 + 2.00564e-2 * t + 1.104259e-4 * t * t
                + -6.9698e-7 * t * t * t + 1.0031e-9 * t * t * t * t;

    double rp = 1.0 + (p * (2.070e-5 + p * (-6.370e-10 + p * 3.989e-15)))
                    / (1.0 + 3.426e-2 * t + 4.464e-4 * t * t
                       + (4.215e-1 - 3.107e-3 * t) * r);

    double rt = r / (rp * rt_t);
    if (rt < 0.0) rt = 0.0;

    double sq = sqrt(rt);
    double sa = a[0], sb = b[0], pw = 1.0;
    for (int i = 1; i < 6; i++) {
        pw *= sq;
        sa += a[i] * pw;
        sb += b[i] * pw;
    }

    double s = sa + (t - 15.0) / (1.0 + 0.0162 * (t - 15.0)) * sb;
    return s < 0.0 ? 0.0 : s;
}

#define C_STANDARD 42.914          /* C(35,15,0) in mS/cm */

double sv_salinity_from_cond(double c, double t, double p)
{
    if (c <= 0.0)
        return 0.0;
    return pss78(c / C_STANDARD, t, p);
}

double sv_cond_from_salinity(double s, double t, double p)
{
    if (s <= 0.0)
        return 0.0;

    /* pss78 is monotonic in R over the useful range; bisect on R. */
    double lo = 0.0, hi = 2.0;
    for (int i = 0; i < 60; i++) {
        double mid = 0.5 * (lo + hi);
        if (pss78(mid, t, p) < s) lo = mid; else hi = mid;
    }
    return 0.5 * (lo + hi) * C_STANDARD;
}

double sv_salinity_from_soundspeed(double c, double t, double p)
{
    double lo = 0.0, hi = 42.0;

    if (c <= sv_soundspeed(lo, t, p)) return 0.0;
    if (c >= sv_soundspeed(hi, t, p)) return hi;

    for (int i = 0; i < 60; i++) {
        double mid = 0.5 * (lo + hi);
        if (sv_soundspeed(mid, t, p) < c) lo = mid; else hi = mid;
    }
    return 0.5 * (lo + hi);
}

double sv_density(double s, double t, double p_dbar)
{
    if (s < 0.0) s = 0.0;
    double t2 = t * t, t3 = t2 * t, t4 = t3 * t, t5 = t4 * t;
    double s15 = s * sqrt(s);

    double rw = 999.842594 + 6.793952e-2 * t - 9.095290e-3 * t2
              + 1.001685e-4 * t3 - 1.120083e-6 * t4 + 6.536332e-9 * t5;

    double r0 = rw
        + s * (0.824493 - 4.0899e-3 * t + 7.6438e-5 * t2
               - 8.2467e-7 * t3 + 5.3875e-9 * t4)
        + s15 * (-5.72466e-3 + 1.0227e-4 * t - 1.6546e-6 * t2)
        + 4.8314e-4 * s * s;

    double p = p_dbar / 10.0;                      /* secant bulk modulus: bar */
    if (p <= 0.0)
        return r0;

    double kw = 19652.21 + 148.4206 * t - 2.327105 * t2
              + 1.360477e-2 * t3 - 5.155288e-5 * t4;

    double k0 = kw
        + s * (54.6746 - 0.603459 * t + 1.09987e-2 * t2 - 6.1670e-5 * t3)
        + s15 * (7.944e-2 + 1.6483e-2 * t - 5.3009e-4 * t2);

    double aw = 3.239908 + 1.43713e-3 * t + 1.16092e-4 * t2 - 5.77905e-7 * t3;
    double a  = aw + s * (2.2838e-3 - 1.0981e-5 * t - 1.6078e-6 * t2)
                   + 1.91075e-4 * s15;

    double bw = 8.50935e-5 - 6.12293e-6 * t + 5.2787e-8 * t2;
    double bb = bw + s * (-9.9348e-7 + 2.0816e-8 * t + 9.1697e-10 * t2);

    double k = k0 + a * p + bb * p * p;
    return r0 / (1.0 - p / k);
}
