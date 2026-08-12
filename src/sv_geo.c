#include "sv_geo.h"

#include <math.h>
#include <stdio.h>

#define DEG2RAD (M_PI / 180.0)
#define EARTH_R 6371008.8            /* mean radius, m */

/* Beyond this separation the tangent plane is dropped for the spherical
 * formulae. A third of a degree is ~37 km, well inside the plane's accurate
 * range and well outside any survey area's cast spacing. */
#define PLANE_LIMIT_DEG 0.33

bool sv_geo_valid(double lat, double lon)
{
    if (!isfinite(lat) || !isfinite(lon))
        return false;
    if (fabs(lat) > 90.0 || fabs(lon) > 180.0)
        return false;
    return true;
}

/* WGS84 series expansions — standard, and good to under a millimetre. */
double sv_geo_m_per_deg_lat(double lat_deg)
{
    double p = lat_deg * DEG2RAD;
    return 111132.92 - 559.82 * cos(2 * p) + 1.175 * cos(4 * p)
           - 0.0023 * cos(6 * p);
}

double sv_geo_m_per_deg_lon(double lat_deg)
{
    double p = lat_deg * DEG2RAD;
    double m = 111412.84 * cos(p) - 93.5 * cos(3 * p) + 0.118 * cos(5 * p);
    return m < 0.0 ? 0.0 : m;                     /* at the poles */
}

/* Signed east/north separation on a plane at the mean latitude. */
static void plane_delta(double lat1, double lon1, double lat2, double lon2,
                        double *east, double *north)
{
    double mid = (lat1 + lat2) / 2.0;
    double dlon = lon2 - lon1;

    /* Shortest way round the meridian, so a pair either side of the date line
     * is a short hop and not a trip round the world. */
    if (dlon > 180.0)  dlon -= 360.0;
    if (dlon < -180.0) dlon += 360.0;

    *east  = dlon * sv_geo_m_per_deg_lon(mid);
    *north = (lat2 - lat1) * sv_geo_m_per_deg_lat(mid);
}

static bool far_apart(double lat1, double lon1, double lat2, double lon2)
{
    double dlon = fabs(lon2 - lon1);
    if (dlon > 180.0)
        dlon = 360.0 - dlon;
    return fabs(lat2 - lat1) > PLANE_LIMIT_DEG || dlon > PLANE_LIMIT_DEG;
}

double sv_geo_distance_m(double lat1, double lon1, double lat2, double lon2)
{
    if (!sv_geo_valid(lat1, lon1) || !sv_geo_valid(lat2, lon2))
        return -1.0;

    if (!far_apart(lat1, lon1, lat2, lon2)) {
        double e, n;
        plane_delta(lat1, lon1, lat2, lon2, &e, &n);
        return sqrt(e * e + n * n);
    }

    double p1 = lat1 * DEG2RAD, p2 = lat2 * DEG2RAD;
    double dp = p2 - p1;
    double dl = (lon2 - lon1) * DEG2RAD;
    double a = sin(dp / 2) * sin(dp / 2)
             + cos(p1) * cos(p2) * sin(dl / 2) * sin(dl / 2);
    if (a < 0.0) a = 0.0;
    if (a > 1.0) a = 1.0;
    return 2.0 * EARTH_R * asin(sqrt(a));
}

double sv_geo_bearing_deg(double lat1, double lon1, double lat2, double lon2)
{
    if (!sv_geo_valid(lat1, lon1) || !sv_geo_valid(lat2, lon2))
        return -1.0;

    double b;
    if (!far_apart(lat1, lon1, lat2, lon2)) {
        double e, n;
        plane_delta(lat1, lon1, lat2, lon2, &e, &n);
        if (e == 0.0 && n == 0.0)
            return 0.0;
        b = atan2(e, n) / DEG2RAD;
    } else {
        double p1 = lat1 * DEG2RAD, p2 = lat2 * DEG2RAD;
        double dl = (lon2 - lon1) * DEG2RAD;
        b = atan2(sin(dl) * cos(p2),
                  cos(p1) * sin(p2) - sin(p1) * cos(p2) * cos(dl)) / DEG2RAD;
    }

    b = fmod(b, 360.0);
    if (b < 0.0)
        b += 360.0;
    return b;
}

/* ---- local tangent plane ------------------------------------------- */

void sv_geo_proj_init(SvGeoProj *p, double lat0, double lon0)
{
    p->lat0  = lat0;
    p->lon0  = lon0;
    p->m_lat = sv_geo_m_per_deg_lat(lat0);
    p->m_lon = sv_geo_m_per_deg_lon(lat0);
    if (p->m_lon < 1.0)
        p->m_lon = 1.0;              /* keep the inverse finite at the pole */
}

void sv_geo_forward(const SvGeoProj *p, double lat, double lon,
                    double *east_m, double *north_m)
{
    double dlon = lon - p->lon0;
    if (dlon > 180.0)  dlon -= 360.0;
    if (dlon < -180.0) dlon += 360.0;

    *east_m  = dlon * p->m_lon;
    *north_m = (lat - p->lat0) * p->m_lat;
}

void sv_geo_inverse(const SvGeoProj *p, double east_m, double north_m,
                    double *lat, double *lon)
{
    *lat = p->lat0 + north_m / p->m_lat;
    *lon = p->lon0 + east_m / p->m_lon;
}

/* ---- bounds -------------------------------------------------------- */

void sv_geo_bounds_reset(SvGeoBounds *b)
{
    b->min_lat = b->max_lat = 0.0;
    b->min_lon = b->max_lon = 0.0;
    b->n = 0;
}

void sv_geo_bounds_add(SvGeoBounds *b, double lat, double lon)
{
    if (!sv_geo_valid(lat, lon))
        return;

    if (b->n == 0) {
        b->min_lat = b->max_lat = lat;
        b->min_lon = b->max_lon = lon;
    } else {
        if (lat < b->min_lat) b->min_lat = lat;
        if (lat > b->max_lat) b->max_lat = lat;
        if (lon < b->min_lon) b->min_lon = lon;
        if (lon > b->max_lon) b->max_lon = lon;
    }
    b->n++;
}

bool sv_geo_bounds_centre(const SvGeoBounds *b, double *lat, double *lon)
{
    if (b->n == 0)
        return false;
    *lat = (b->min_lat + b->max_lat) / 2.0;
    *lon = (b->min_lon + b->max_lon) / 2.0;
    return true;
}

void sv_geo_bounds_extent_m(const SvGeoBounds *b, double *w_m, double *h_m)
{
    if (b->n == 0) {
        *w_m = *h_m = 0.0;
        return;
    }
    double mid = (b->min_lat + b->max_lat) / 2.0;
    *w_m = (b->max_lon - b->min_lon) * sv_geo_m_per_deg_lon(mid);
    *h_m = (b->max_lat - b->min_lat) * sv_geo_m_per_deg_lat(mid);
}

/* ---- graticule and scale ------------------------------------------- */

double sv_geo_nice_step(double v)
{
    if (!(v > 0.0) || !isfinite(v))
        return 1.0;

    double mag  = pow(10.0, floor(log10(v)));
    double norm = v / mag;
    double step = (norm >= 5.0) ? 5.0 : (norm >= 2.0) ? 2.0 : 1.0;
    return step * mag;
}

double sv_geo_grid_step_deg(double span_deg, int target_divisions)
{
    /* Degrees, then whole minutes, then whole seconds, then fractions of a
     * second. A chart's gridlines fall on these and nowhere else. */
    static const double STEPS[] = {
        30.0, 10.0, 5.0, 2.0, 1.0,                    /* degrees */
        30.0 / 60, 20.0 / 60, 10.0 / 60, 5.0 / 60,
        2.0 / 60, 1.0 / 60,                           /* minutes */
        30.0 / 3600, 20.0 / 3600, 10.0 / 3600, 5.0 / 3600,
        2.0 / 3600, 1.0 / 3600,                       /* seconds */
        0.5 / 3600, 0.2 / 3600, 0.1 / 3600,
        0.05 / 3600, 0.02 / 3600, 0.01 / 3600
    };
    const int N = (int)(sizeof STEPS / sizeof STEPS[0]);

    if (target_divisions < 1)
        target_divisions = 1;
    if (!(span_deg > 0.0) || !isfinite(span_deg))
        return STEPS[N - 1];

    double want = span_deg / target_divisions;

    /* Largest step not larger than the target, so the graticule is never
     * denser than asked for. */
    for (int i = 0; i < N; i++)
        if (STEPS[i] <= want)
            return STEPS[i];

    return STEPS[N - 1];
}

/* Decimals of a minute worth printing at a given graticule step. */
static int minute_decimals(double step_deg)
{
    double step_min = step_deg * 60.0;
    if (step_min >= 1.0)   return 1;
    if (step_min >= 0.1)   return 2;
    if (step_min >= 0.01)  return 3;
    return 4;
}

static void format_ll(char *dst, size_t cap, double v, double step_deg,
                      char pos, char neg)
{
    if (!cap)
        return;
    if (!isfinite(v)) {
        snprintf(dst, cap, "—");
        return;
    }

    char hemi = (v < 0.0) ? neg : pos;
    double a = fabs(v);
    int deg = (int)a;
    double min = (a - deg) * 60.0;
    int dec = minute_decimals(step_deg);

    /* Rounding the minutes can reach 60.000 and would otherwise print
     * "50 60.000'". */
    double limit = 60.0 - 0.5 * pow(10.0, -dec);
    if (min >= limit) {
        min = 0.0;
        deg += 1;
    }

    snprintf(dst, cap, "%d\xc2\xb0 %.*f' %c", deg, dec, min, hemi);
}

void sv_geo_format_lat(char *dst, size_t cap, double lat, double step_deg)
{
    format_ll(dst, cap, lat, step_deg, 'N', 'S');
}

void sv_geo_format_lon(char *dst, size_t cap, double lon, double step_deg)
{
    format_ll(dst, cap, lon, step_deg, 'E', 'W');
}

void sv_geo_format_distance(char *dst, size_t cap, double metres)
{
    if (!cap)
        return;
    if (!isfinite(metres) || metres < 0.0) {
        snprintf(dst, cap, "—");
        return;
    }
    if (metres < 1000.0)
        snprintf(dst, cap, "%.0f m", metres);
    else
        snprintf(dst, cap, "%.2f km  (%.2f nm)",
                 metres / 1000.0, metres / 1852.0);
}
