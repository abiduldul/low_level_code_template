#include "gps.h"
#include "sensor.h"
#include "uart4.h"
#include "rtos.h"
#include "log.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------------- */
/* Configuration                                                             */
/* ------------------------------------------------------------------------- */
#define GPS_FIX_MAX_AGE_SEC 10U
#define GPS_LINE_MAX        96U     /* longest NMEA sentence we care about */
#define GPS_LINE_SLOTS      4U      /* ring of line buffers                */
#define GPS_QUEUE_DEPTH     8U
#define NMEA_MAX_FIELDS     20U
#define GPS_READ_TIMEOUT_TICKS  8000U
#define GPS_FLAG_FIX        0x01U

/* Send PMTK configuration on start-up.
 *
 * SET THIS TO 0 IF YOUR BOARD HAS NO LEVEL SHIFTER ON PA0. The standard
 * L76-LB has 2.7-2.9 V I/O; driving its RX pin from a 3.3 V STM32 output
 * can exceed the module's absolute maximum rating. Receiving is always
 * safe - 2.8 V clears the STM32's 2.31 V input threshold - so with this
 * set to 0 the driver still works, just on the module's defaults. */
#define GPS_SEND_PMTK_CONFIG    0

/* ------------------------------------------------------------------------- */
/* State                                                                     */
/* ------------------------------------------------------------------------- */

static TX_THREAD            tx_gps;
static TX_QUEUE             gps_queue;
static TX_EVENT_FLAGS_GROUP gps_flags;

static char     line_slot[GPS_LINE_SLOTS][GPS_LINE_MAX];
static uint8_t  slot_idx;
static uint16_t line_len;

static volatile uint32_t dbg_rx_bytes;      /* byte mentah dari UART4      */
static volatile uint32_t dbg_lines;         /* kalimat lengkap terkumpul   */
static volatile uint32_t dbg_bad_checksum;  /* kalimat gagal checksum      */
static volatile uint8_t  dbg_raw_shown;     /* berapa baris mentah dicetak */

static volatile GPS_Fix_t last_fix;
static volatile uint8_t   ever_valid;

/* Values handed to the sensor framework. */
static int32_t gps_lat_1e7;
static int32_t gps_lon_1e7;

static void gps_thread_entry(ULONG);
static void gps_uart_rx_cb(const uint8_t *data, uint16_t length);
static void gps_parse_line(char *line);
static void gps_publish(const GPS_Fix_t *fix);

static void    gps_sensor_init(void);
static uint8_t gps_sensor_read(void);
static void    gps_sensor_config(void);
static void    gps_sensor_error_handler(void);

/* ------------------------------------------------------------------------- */
/* Sensor framework wrapper                                                  */
/* ------------------------------------------------------------------------- */

static int32_t gps_lat_1e7;
static int32_t gps_lon_1e7;
static int32_t gps_alt_mm;
static int32_t gps_sats;
 
static SensorValue_t _values[] = {
    { .data_type = DATATYPE_INT32, .value = &gps_lat_1e7 },
    { .data_type = DATATYPE_INT32, .value = &gps_lon_1e7 },
    { .data_type = DATATYPE_INT32, .value = &gps_alt_mm  },
    { .data_type = DATATYPE_INT32, .value = &gps_sats    }
};
 
Sensor_t gps_sensor = {
    .id            = 0x1030,
    .init          = gps_sensor_init,
    .read          = gps_sensor_read,
    .config        = gps_sensor_config,
    .error_handler = gps_sensor_error_handler,
    .nb_values     = 4,
    .values        = _values
};


/* ------------------------------------------------------------------------- */
/* NMEA helpers                                                              */
/* ------------------------------------------------------------------------- */

static uint8_t hex_nibble(char c)
{
    if ((c >= '0') && (c <= '9')) return (uint8_t)(c - '0');
    if ((c >= 'A') && (c <= 'F')) return (uint8_t)(c - 'A' + 10);
    if ((c >= 'a') && (c <= 'f')) return (uint8_t)(c - 'a' + 10);

    return 0xFFU;
}

/* Verifies the trailing *HH and truncates the line at the '*' so the last
 * comma-separated field comes out clean. Returns 1 if the checksum matches. */
static uint8_t nmea_checksum_ok(char *line)
{
    uint8_t  computed = 0U;
    uint8_t  hi, lo;
    uint16_t i;

    if (line[0] != '$') {
        return 0U;
    }

    for (i = 1U; (line[i] != '\0') && (line[i] != '*'); i++) {
        computed ^= (uint8_t)line[i];
    }

    if (line[i] != '*') {
        return 0U;
    }

    hi = hex_nibble(line[i + 1U]);
    lo = hex_nibble(line[i + 2U]);

    if ((hi == 0xFFU) || (lo == 0xFFU)) {
        return 0U;
    }

    line[i] = '\0';     /* drop "*HH" from the payload */

    return (computed == (uint8_t)((hi << 4) | lo)) ? 1U : 0U;
}

/* Splits on commas in place. Empty fields stay as valid empty strings, which
 * matters because NMEA leaves fields blank when there is no fix. */
static uint8_t nmea_split(char *line, char **fields, uint8_t max)
{
    uint8_t n = 0U;

    fields[n++] = line;

    for (char *p = line; *p != '\0'; p++) {
        if (*p == ',') {
            *p = '\0';

            if (n < max) {
                fields[n++] = p + 1;
            }
        }
    }

    return n;
}

/* ddmm.mmmm / dddmm.mmmm -> degrees x 1e7.
 *
 * Done in integer arithmetic on purpose. A float carries about 7 significant
 * digits, and a coordinate like -7.7970680 needs exactly that - so float
 * would sit right on the edge of losing metres. */
static int32_t nmea_coord(const char *s, uint8_t deg_digits, char dir)
{
    int64_t deg = 0, min_int = 0, frac = 0, scale = 1, minutes_1e6, result;
    uint8_t frac_digits = 0U;
    uint8_t i = 0U;

    if ((s == NULL) || (s[0] == '\0')) {
        return 0;
    }

    for (i = 0U; i < deg_digits; i++) {
        if ((s[i] < '0') || (s[i] > '9')) {
            return 0;
        }
        deg = (deg * 10) + (s[i] - '0');
    }

    for (; (s[i] >= '0') && (s[i] <= '9'); i++) {
        min_int = (min_int * 10) + (s[i] - '0');
    }

    if (s[i] == '.') {
        i++;
        for (; (s[i] >= '0') && (s[i] <= '9') && (frac_digits < 6U); i++) {
            frac = (frac * 10) + (s[i] - '0');
            scale *= 10;
            frac_digits++;
        }
    }

    minutes_1e6 = min_int * 1000000LL;

    if (frac_digits > 0U) {
        minutes_1e6 += (frac * 1000000LL) / scale;
    }

    /* 60 minutes == 1 degree == 1e7. */
    result = (deg * 10000000LL) + ((minutes_1e6 * 10LL) / 60LL);

    if ((dir == 'S') || (dir == 'W')) {
        result = -result;
    }

    return (int32_t)result;
}

/* "123.4" with out_decimals = 3 -> 123400 */
static int32_t nmea_scaled(const char *s, uint8_t out_decimals)
{
    int64_t whole = 0, frac = 0, scale = 1, result;
    uint8_t frac_digits = 0U;
    uint8_t neg = 0U;
    uint8_t i = 0U;

    if ((s == NULL) || (s[0] == '\0')) {
        return 0;
    }

    if (s[i] == '-') {
        neg = 1U;
        i++;
    }

    for (; (s[i] >= '0') && (s[i] <= '9'); i++) {
        whole = (whole * 10) + (s[i] - '0');
    }

    if (s[i] == '.') {
        i++;
        for (; (s[i] >= '0') && (s[i] <= '9') && (frac_digits < out_decimals); i++) {
            frac = (frac * 10) + (s[i] - '0');
            scale *= 10;
            frac_digits++;
        }
    }

    result = whole;

    for (uint8_t k = 0U; k < out_decimals; k++) {
        result *= 10;
    }

    if (frac_digits > 0U) {
        int64_t mult = 1;

        for (uint8_t k = 0U; k < out_decimals; k++) {
            mult *= 10;
        }

        result += (frac * mult) / scale;
    }

    return neg ? (int32_t)(-result) : (int32_t)result;
}

static uint8_t two_digits(const char *s)
{
    return (uint8_t)(((s[0] - '0') * 10) + (s[1] - '0'));
}

/* date = "ddmmyy", time = "hhmmss.sss" -> UTC unix seconds. */
static time_t nmea_datetime(const char *date, const char *tm_str)
{
    struct tm t = {0};

    if ((date == NULL) || (tm_str == NULL)) {
        return 0;
    }

    if ((strlen(date) < 6U) || (strlen(tm_str) < 6U)) {
        return 0;
    }

    t.tm_mday = two_digits(&date[0]);
    t.tm_mon  = two_digits(&date[2]) - 1;      
    t.tm_year = two_digits(&date[4]) + 100;    

    t.tm_hour = two_digits(&tm_str[0]);
    t.tm_min  = two_digits(&tm_str[2]);
    t.tm_sec  = two_digits(&tm_str[4]);

    t.tm_isdst = 0;     /* must be zeroed; mktime() assumes UTC on newlib */

    return mktime(&t);
}

/* ------------------------------------------------------------------------- */
/* Sentence handlers                                                         */
/* ------------------------------------------------------------------------- */

static void parse_rmc(char **f, uint8_t n)
{
    if (n < 10U) {
        return;
    }

    /* Field 2 is the status: 'A' = valid, 'V' = void (navigation warning). */
    if (f[2][0] != 'A') {
        last_fix.fix_valid = 0U;

        return;
    }

    last_fix.latitude    = nmea_coord(f[3], 2U, f[4][0]);
    last_fix.longitude   = nmea_coord(f[5], 3U, f[6][0]);
    last_fix.timestamp   = nmea_datetime(f[9], f[1]);
    last_fix.age_seconds = 0U;
    last_fix.fix_valid   = 1U;

    ever_valid = 1U;

    (void)tx_event_flags_set(&gps_flags, GPS_FLAG_FIX, TX_OR);
}

static void parse_gga(char **f, uint8_t n)
{
    if (n < 10U) {
        return;
    }

    /* Field 6 is fix quality: 0 = no fix. */
    if (f[6][0] == '0') {
        return;
    }

    last_fix.satellites  = (uint8_t)nmea_scaled(f[7], 0U);
    last_fix.altitude_mm = nmea_scaled(f[9], 3U);
}

static void gps_parse_line(char *line)
{
    char   *f[NMEA_MAX_FIELDS];
    uint8_t n;
 
    /* Cetak beberapa kalimat mentah pertama apa adanya. Kalau ini keluar
     * sebagai sampah, masalahnya baud atau kabel - bukan parser. */
    if (dbg_raw_shown < 6U) {
        dbg_raw_shown++;
    }
 
    if (!nmea_checksum_ok(line)) {
        dbg_bad_checksum++;
 
        return;
    }
 
    n = nmea_split(line, f, NMEA_MAX_FIELDS);
 
    if ((n < 1U) || (strlen(f[0]) < 6U)) {
        return;
    }
 
    if (memcmp(&f[0][3], "RMC", 3) == 0) {
        parse_rmc(f, n);
    } else if (memcmp(&f[0][3], "GGA", 3) == 0) {
        parse_gga(f, n);
    }
}

/* ------------------------------------------------------------------------- */
/* UART plumbing                                                             */
/* ------------------------------------------------------------------------- */

/* Interrupt context. Assemble lines only - no parsing here. */
static void gps_uart_rx_cb(const uint8_t *data, uint16_t length)
{
    dbg_rx_bytes += length;
    for (uint16_t i = 0U; i < length; i++) {
        char c = (char)data[i];

        if (c == '\r') {
            continue;
        }

        if (c == '\n') {
            if (line_len > 0U) {
                ULONG msg = slot_idx;

                line_slot[slot_idx][line_len] = '\0';
                dbg_lines++;

                if (tx_queue_send(&gps_queue, &msg, TX_NO_WAIT) == TX_SUCCESS) {
                    slot_idx = (uint8_t)((slot_idx + 1U) % GPS_LINE_SLOTS);
                }

                line_len = 0U;
            }

            continue;
        }

        /* A '$' always starts a fresh sentence: resynchronise on it rather
         * than trusting that the previous line ended cleanly. */
        if (c == '$') {
            line_len = 0U;
        }

        if (line_len < (GPS_LINE_MAX - 1U)) {
            line_slot[slot_idx][line_len++] = c;
        } else {
            line_len = 0U;      /* oversized sentence: drop it */
        }
    }
}

#if GPS_SEND_PMTK_CONFIG
/* Builds "$<body>*HH\r\n" and sends it. Checksum computed at runtime so
 * there is no hand-calculated hex to get wrong. */
static void pmtk_send(const char *body)
{
    char     msg[80];
    uint8_t  cs = 0U;
    int      len;

    for (const char *p = body; *p != '\0'; p++) {
        cs ^= (uint8_t)*p;
    }

    len = snprintf(msg, sizeof(msg), "$%s*%02X\r\n", body, cs);

    if (len > 0) {
        UART4_Transmit((const uint8_t *)msg, (uint16_t)len);
    }
}
#endif

/* ------------------------------------------------------------------------- */
/* Thread                                                                    */
/* ------------------------------------------------------------------------- */

static void gps_thread_entry(ULONG input)
{
    ULONG    idx;
    uint32_t tick = 0U;

    (void)input;

    UART4_SetRx_Callback(gps_uart_rx_cb);
     GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* PB2 = PERST# on the mPCIe socket, active low. Pulse it low, then
     * release, so the module is guaranteed to come out of reset rather
     * than relying on a board pull-up that may or may not exist. */
    gpio.Pin   = GPIO_PIN_2;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &gpio);

    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_2, GPIO_PIN_RESET);
    tx_thread_sleep(50);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_2, GPIO_PIN_SET);
    tx_thread_sleep(500);      /* modul boot */

    UART4_Config();
    // LOG_INFO("UART4 en=%lu  PA0/1 moder=%02lX  DMA4 ccr=%03lX ndtr=%lu  PC6=%u PC7=%u",
    //      (unsigned long)((RCC->APB1ENR1 & RCC_APB1ENR1_UART4EN) ? 1U : 0U),
    //      (unsigned long)(GPIOA->MODER & 0x0FUL),
    //      (unsigned long)(DMA1_Channel4->CCR & 0x7FFUL),
    //      (unsigned long)DMA1_Channel4->CNDTR,
    //      (unsigned)HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_6),
    //      (unsigned)HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_7));

    LOG_INFO("GPS: UART4 up at 115200, waiting for NMEA");

#if GPS_SEND_PMTK_CONFIG
    tx_thread_sleep(1000);                  /* let the module finish booting */

    /* Output RMC and GGA only. Field order is GLL,RMC,VTG,GGA,GSA,GSV,... */
    pmtk_send("PMTK314,0,1,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0");

    /* EASY: predicts orbits up to 3 days ahead and cuts cold-start TTFF
     * from under 35 s to under 15 s. */
    pmtk_send("PMTK869,1,1");

    LOG_INFO("GPS: PMTK configuration sent");
#endif

    while (1) {
        if (tx_queue_receive(&gps_queue, &idx, 1000) == TX_SUCCESS) {
            if (idx < GPS_LINE_SLOTS) {
                gps_parse_line(line_slot[idx]);
            }

            continue;
        }

        /* Timeout branch: roughly one second passed with no sentence. */
        if (last_fix.age_seconds < 0xFFFFU) {
            last_fix.age_seconds++;
        }
 
        if ((++tick % 10U) == 0U) {
            LOG_INFO("GPS diag: rx=%lu lines=%lu bad=%lu fix=%u sats=%u",
                     (unsigned long)dbg_rx_bytes,
                     (unsigned long)dbg_lines,
                     (unsigned long)dbg_bad_checksum,
                     (unsigned)last_fix.fix_valid,
                     (unsigned)last_fix.satellites);
        }

    }
}

/* ------------------------------------------------------------------------- */
/* Public API                                                                */
/* ------------------------------------------------------------------------- */

uint8_t GPS_Init(void)
{
    if (tx_event_flags_create(&gps_flags, "GPS Flags") != TX_SUCCESS) {
        return 0U;
    }

    if (RTOS_CreateQueue(&gps_queue, "GPS Queue", GPS_QUEUE_DEPTH) != TX_SUCCESS) {
        return 0U;
    }

    if (RTOS_CreateThread(&tx_gps, "GPS Thread", gps_thread_entry,
                          1536, 11, 1) != TX_SUCCESS) {
        return 0U;
    }

    return 1U;
}

uint8_t GPS_WaitFix(GPS_Fix_t *fix, uint32_t timeout_ticks)
{
    ULONG actual;

    if (fix == NULL) {
        return 0U;
    }

    /* TX_OR_CLEAR: only a fix that arrives from here on counts as fresh. */
    if (tx_event_flags_get(&gps_flags, GPS_FLAG_FIX, TX_OR_CLEAR,
                           &actual, timeout_ticks) != TX_SUCCESS) {
        return 0U;
    }

    *fix = *(GPS_Fix_t *)&last_fix;

    return fix->fix_valid;
}

uint8_t GPS_GetLastFix(GPS_Fix_t *fix)
{
    if ((fix == NULL) || !ever_valid) {
        return 0U;
    }

    *fix = *(GPS_Fix_t *)&last_fix;

    return 1U;
}

/* ------------------------------------------------------------------------- */
/* Sensor framework hooks                                                    */
/* ------------------------------------------------------------------------- */

static void gps_sensor_init(void)
{
    (void)GPS_Init();
}

static uint8_t gps_sensor_read(void)
{
    GPS_Fix_t fix;
 
    if (GPS_GetLastFix(&fix) && fix.fix_valid &&
        (fix.age_seconds <= GPS_FIX_MAX_AGE_SEC)) {
 
        gps_publish(&fix);
        gps_sensor.error_codes = 0U;
 
        return 1U;
    }
 
    if (GPS_WaitFix(&fix, GPS_READ_TIMEOUT_TICKS)) {
        gps_publish(&fix);
        gps_sensor.error_codes = 0U;
 
        return 1U;
    }

    gps_sats = 0;
    gps_sensor.error_codes = 1U;
 
    return 0U;
}

static void gps_publish(const GPS_Fix_t *fix)
{
    gps_lat_1e7 = fix->latitude;
    gps_lon_1e7 = fix->longitude;
    gps_alt_mm  = fix->altitude_mm;
    gps_sats    = (int32_t)fix->satellites;
}

static void gps_sensor_config(void)
{
    return;
}

static void gps_sensor_error_handler(void)
{
    LOG_WARNING("GPS: read failed, no valid fix available");
}
