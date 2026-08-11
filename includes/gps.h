#ifndef __GPS_H__
#define __GPS_H__

#include <stdint.h>
#include <time.h>

/* Path: includes/gps.h */

/* Coordinates are degrees x 1e7, stored as int32 - the convention u-blox and
 * MediaTek both use internally.
 *
 * Do NOT switch these to float. Sensor_PackedJson() prints floats with
 * "%.2f", and two decimal places of latitude is about 1.1 km of resolution,
 * which destroys the reading. int32 survives the packer intact; divide by
 * 1e7 on the server side. */
typedef struct {
    int32_t  latitude;      /* degrees x 1e7, positive = north */
    int32_t  longitude;     /* degrees x 1e7, positive = east  */
    int32_t  altitude_mm;   /* metres above mean sea level, x1000 */
    uint16_t age_seconds;   /* how long since this fix was refreshed */
    uint8_t  satellites;    /* satellites used in the solution */
    uint8_t  fix_valid;     /* 1 = RMC reported status 'A' */
    time_t   timestamp;     /* UTC unix seconds, from RMC date + time */
} GPS_Fix_t;

/* Brings up UART4, starts the parser thread, and (optionally) sends the
 * PMTK configuration. Returns 1 on success. */
uint8_t GPS_Init(void);

/* Blocks until a valid fix arrives or the timeout expires.
 * Returns 1 on a fresh valid fix, 0 on timeout.
 *
 * Budget the timeout against the L76-LB datasheet: hot start is under 1 s,
 * warm start under 5 s with EASY, cold start under 15 s with EASY and under
 * 35 s without. Keep V_BCKP powered through sleep or every wake-up pays the
 * cold-start price. */
uint8_t GPS_WaitFix(GPS_Fix_t *fix, uint32_t timeout_ticks);

/* Non-blocking. Returns 1 if a valid fix has ever been seen, and copies it.
 * Check fix->age_seconds to decide whether it is still good enough. */
uint8_t GPS_GetLastFix(GPS_Fix_t *fix);

/* The Sensor_t wrapper (latitude and longitude as two INT32 values) is
 * declared in sensor.h alongside the other sensors:
 *
 *     extern Sensor_t gps_sensor;
 */

#endif /* __GPS_H__ */