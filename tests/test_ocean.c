/*
 * test_ocean.c — oceanographic conversions against published check values.
 *
 * Sources:
 *   Chen & Millero 1977 / UNESCO 1983  — sound speed
 *   UNESCO 1983 (Fofonoff & Millard)   — PSS-78 salinity, EOS-80 density,
 *                                        pressure-to-depth
 */
#include "sv_test.h"
#include "../src/sv_ocean.h"

TEST_MAIN("sv_ocean",
{
    /* Sound speed. c(35,0,0) is exactly the sum of the S-terms on top of
     * C00 = 1402.388, so this one is tight. */
    CHECK_NEAR(sv_soundspeed(35.0, 0.0, 0.0), 1449.14, 0.01);

    /* Monotonic in all three variables over the operating range. */
    CHECK(sv_soundspeed(35.0, 10.0, 0.0) > sv_soundspeed(35.0, 0.0, 0.0));
    CHECK(sv_soundspeed(35.0, 10.0, 1000.0) > sv_soundspeed(35.0, 10.0, 0.0));
    CHECK(sv_soundspeed(35.0, 10.0, 0.0) > sv_soundspeed(30.0, 10.0, 0.0));

    /* Plausible open-ocean surface value. */
    CHECK_NEAR(sv_soundspeed(35.0, 15.0, 0.0), 1507.0, 2.0);

    /* PSS-78: R = 1 at T = 15, P = 0 is salinity 35 by definition. */
    CHECK_NEAR(sv_salinity_from_cond(42.914, 15.0, 0.0), 35.0, 1e-6);
    CHECK_NEAR(sv_salinity_from_cond(0.0, 15.0, 0.0), 0.0, 1e-9);
    CHECK(sv_salinity_from_cond(50.0, 15.0, 0.0) > 35.0);

    /* Conductivity inverse round-trips. */
    for (double s = 5.0; s <= 40.0; s += 5.0) {
        double c = sv_cond_from_salinity(s, 12.0, 100.0);
        CHECK_NEAR(sv_salinity_from_cond(c, 12.0, 100.0), s, 1e-4);
    }

    /* Salinity from sound speed — the SWiFT SVP path. Round-trip through
     * the forward model. */
    for (double s = 0.0; s <= 40.0; s += 8.0) {
        double c = sv_soundspeed(s, 14.0, 250.0);
        CHECK_NEAR(sv_salinity_from_soundspeed(c, 14.0, 250.0), s, 1e-4);
    }
    /* Out-of-range clamps rather than diverging. */
    CHECK_NEAR(sv_salinity_from_soundspeed(1000.0, 14.0, 0.0), 0.0, 1e-9);
    CHECK(sv_salinity_from_soundspeed(2000.0, 14.0, 0.0) <= 42.0);

    /* EOS-80 density check values (UNESCO 1983). */
    CHECK_NEAR(sv_density(0.0, 5.0, 0.0), 999.96675, 1e-3);
    CHECK_NEAR(sv_density(35.0, 5.0, 0.0), 1027.67547, 1e-3);
    CHECK_NEAR(sv_density(35.0, 25.0, 10000.0), 1062.53817, 1e-3);

    /* Depth from pressure: UNESCO check value is 9712.653 m at 10000 dBar,
     * latitude 30. */
    CHECK_NEAR(sv_depth_from_pressure(10000.0, 30.0), 9712.653, 0.01);
    CHECK_NEAR(sv_depth_from_pressure(0.0, 50.0), 0.0, 1e-9);

    /* ~1 dBar per metre near the surface. */
    CHECK_NEAR(sv_depth_from_pressure(100.0, 50.0), 99.2, 0.6);

    /* Gravity increases towards the pole. */
    CHECK(sv_gravity(60.0) > sv_gravity(0.0));
})
