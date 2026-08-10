/*
 * Home assistant sensor and switch control demo
 */
#include "lwesp/apps/lwesp_mqtt_client_api.h"
#include "lwesp/lwesp_mem.h"
#include "math.h"
#include "mqtt_client_api.h"

#define ASZ(x) (sizeof(x) / sizeof((x)[0]))
#define RUN_LWESP_API(api_call)                                                                                        \
    do {                                                                                                               \
        lwespr_t err = (api_call);                                                                                     \
        if (err != lwespOK) {                                                                                          \
            printf("API CALL on line %u did not return OK. Err code: %u\r\n", (unsigned)__LINE__, (unsigned)err);      \
        }                                                                                                              \
    } while (0)

/* Setup device information */
#define DEVICE_MODEL                       "8R-1L-1TS"
#define DEVICE_UNIQUE_ID                   "123498DB"
#define DEVICE_UNIQUE_TOPIC_ID             DEVICE_MODEL "-" DEVICE_UNIQUE_ID
#define DEVICE_SERIAL_NUMBER               "ABCDEF-2321312-32143432-432424324324"
#define PREFIX_DISCOVERY                   "homeassistant"
#define DEVICE_OP_TOPIC_PREFIX             "ha/tm/" DEVICE_UNIQUE_TOPIC_ID

#define SENSOR_DESC_TOPIC_DISCOVERY_CONFIG PREFIX_DISCOVERY "/sensor/" DEVICE_UNIQUE_TOPIC_ID "/ts_%02u/config"
#define SENSOR_DESC_TOPIC_PREFIX           DEVICE_OP_TOPIC_PREFIX "/ts/%02u"
#define SENSOR_DESC_TOPIC_STATE            SENSOR_DESC_TOPIC_PREFIX "/s/stat"
#define SENSOR_DESC_CONFIG                                                                                             \
    "{"                                                                                                                \
    "    \"~\": \"" SENSOR_DESC_TOPIC_PREFIX "\","                                                                     \
    "    \"name\": \"Temp sens %02u\","                                                                                \
    "    \"object_id\": \"temperature_%02u\","                                                                         \
    "    \"unique_id\": \"ts_" DEVICE_UNIQUE_ID "_%02u\","                                                             \
    "    \"device_class\": \"temperature\","                                                                           \
    "    \"stat_t\": \"~/s/stat\","                                                                                    \
    "    \"unit_of_measurement\": \"°C\","                                                                            \
    "    \"value_template\": \"{{ value_json.temperature }}\","                                                        \
    "    \"device\": {"                                                                                                \
    "        \"identifiers\": ["                                                                                       \
    "            \"switch_unit_DEADBEEF\""                                                                             \
    "        ],"                                                                                                       \
    "        \"name\": \"" DEVICE_MODEL "-" DEVICE_UNIQUE_ID "\","                                                     \
    "        \"model\": \"" DEVICE_MODEL "\","                                                                         \
    "        \"manufacturer\": \"MaJerle\","                                                                           \
    "        \"serial_number\": \"" DEVICE_SERIAL_NUMBER "\","                                                         \
    "        \"sw_version\": \"1.2.3-" __DATE__ "-" __TIME__ "\","                                                     \
    "        \"hw_version\": \"4.5.6\""                                                                                \
    "    }"                                                                                                            \
    "}"

#define SWITCH_DESC_TOPIC_DISCOVERY_CONFIG PREFIX_DISCOVERY "/switch/" DEVICE_UNIQUE_TOPIC_ID "/switch_%02u/config"
#define SWITCH_DESC_TOPIC_PREFIX           DEVICE_OP_TOPIC_PREFIX "/switch/%02u"
#define SWITCH_DESC_TOPIC_COMMAND          SWITCH_DESC_TOPIC_PREFIX "/c/cmd"
#define SWITCH_DESC_TOPIC_STATE            SWITCH_DESC_TOPIC_PREFIX "/s/stat"
#define SWITCH_DESC_CONFIG                                                                                             \
    "{"                                                                                                                \
    "    \"~\": \"" SWITCH_DESC_TOPIC_PREFIX "\","                                                                     \
    "    \"name\": \"Switch %02u\","                                                                                   \
    "    \"object_id\": \"switch_%02u\","                                                                              \
    "    \"unique_id\": \"switch_" DEVICE_UNIQUE_ID "_%02u\","                                                         \
    "    \"device_class\": \"switch\","                                                                                \
    "    \"cmd_t\": \"~/c/cmd\","                                                                                      \
    "    \"stat_t\": \"~/s/stat\","                                                                                    \
    "    \"device\": {"                                                                                                \
    "        \"identifiers\": ["                                                                                       \
    "            \"switch_unit_DEADBEEF\""                                                                             \
    "        ]"                                                                                                        \
    "    }"                                                                                                            \
    "}"

#define TRIAC_DESC_TOPIC_DISCOVERY_CONFIG PREFIX_DISCOVERY "/light/" DEVICE_UNIQUE_TOPIC_ID "/triac_%02u/config"
#define TRIAC_DESC_TOPIC_PREFIX           DEVICE_OP_TOPIC_PREFIX "/triac/%02u"
#define TRIAC_DESC_TOPIC_COMMAND          TRIAC_DESC_TOPIC_PREFIX "/c/cmd"
#define TRIAC_DESC_TOPIC_STATE            TRIAC_DESC_TOPIC_PREFIX "/s/stat"
#define TRIAC_DESC_TOPIC_BRIGHTNESS       TRIAC_DESC_TOPIC_PREFIX "/c/bri"
#define TRIAC_DESC_TOPIC_BRIGHTNESS_STATE TRIAC_DESC_TOPIC_PREFIX "/s/bri"
#define TRIAC_DESC_CONFIG                                                                                              \
    "{"                                                                                                                \
    "    \"~\": \"" TRIAC_DESC_TOPIC_PREFIX "\","                                                                      \
    "    \"name\": \"Triac %02u\","                                                                                    \
    "    \"object_id\": \"triac_%02u\","                                                                               \
    "    \"unique_id\": \"triac_" DEVICE_UNIQUE_ID "_%02u\","                                                          \
    "    \"device_class\": \"light\","                                                                                 \
    "    \"cmd_t\": \"~/c/cmd\","                                                                                      \
    "    \"stat_t\": \"~/s/stat\","                                                                                    \
    "    \"bri_cmd_t\": \"~/c/bri\","                                                                                  \
    "    \"bri_stat_t\": \"~/s/bri\","                                                                                 \
    "    \"brightness_value_template\": \"{{ value_json.brightness }}\","                                              \
    "    \"icon\": \"mdi:lightbulb-on-50\","                                                                           \
    "    \"device\": {"                                                                                                \
    "        \"identifiers\": ["                                                                                       \
    "            \"switch_unit_DEADBEEF\""                                                                             \
    "        ]"                                                                                                        \
    "    }"                                                                                                            \
    "}"

#define RGBSTRIP_DESC_TOPIC_DISCOVERY_CONFIG PREFIX_DISCOVERY "/light/" DEVICE_UNIQUE_TOPIC_ID "/rgbstrip_%02u/config"
#define RGBSTRIP_DESC_TOPIC_PREFIX           DEVICE_OP_TOPIC_PREFIX "/rgbstrip/%02u"
#define RGBSTRIP_DESC_TOPIC_COMMAND          RGBSTRIP_DESC_TOPIC_PREFIX "/c/cmd"
#define RGBSTRIP_DESC_TOPIC_STATE            RGBSTRIP_DESC_TOPIC_PREFIX "/s/stat"
#define RGBSTRIP_DESC_TOPIC_BRIGHTNESS       RGBSTRIP_DESC_TOPIC_PREFIX "/c/bri"
#define RGBSTRIP_DESC_TOPIC_BRIGHTNESS_STATE RGBSTRIP_DESC_TOPIC_PREFIX "/s/bri"
#define RGBSTRIP_DESC_TOPIC_COLOR_TEMP       RGBSTRIP_DESC_TOPIC_PREFIX "/c/clr_temp"
#define RGBSTRIP_DESC_TOPIC_COLOR_TEMP_STATE RGBSTRIP_DESC_TOPIC_PREFIX "/s/clr_temp"
#define RGBSTRIP_DESC_TOPIC_COLOR_MODE       RGBSTRIP_DESC_TOPIC_PREFIX "/c/clrm"
#define RGBSTRIP_DESC_TOPIC_COLOR_MODE_STATE RGBSTRIP_DESC_TOPIC_PREFIX "/s/clrm"
#define RGBSTRIP_DESC_TOPIC_RGB              RGBSTRIP_DESC_TOPIC_PREFIX "/c/rgb"
#define RGBSTRIP_DESC_TOPIC_RGB_STATE        RGBSTRIP_DESC_TOPIC_PREFIX "/s/rgb"
#define RGBSTRIP_DESC_TOPIC_EFFECT           RGBSTRIP_DESC_TOPIC_PREFIX "/c/effect"
#define RGBSTRIP_DESC_TOPIC_EFFECT_STATE     RGBSTRIP_DESC_TOPIC_PREFIX "/s/effect"
#define RGBSTRIP_DESC_CONFIG                                                                                           \
    "{"                                                                                                                \
    "    \"~\": \"" RGBSTRIP_DESC_TOPIC_PREFIX "\","                                                                   \
    "    \"name\": \"RGB strip %02u\","                                                                                \
    "    \"object_id\": \"rgbstrip_%02u\","                                                                            \
    "    \"unique_id\": \"rgbstrip_" DEVICE_UNIQUE_ID "_%02u\","                                                       \
    "    \"device_class\": \"light\","                                                                                 \
    "" /* "    \"schema\": \"json\","  - enabled if you want everything in a single command  */                        \
    "    \"cmd_t\": \"~/c/cmd\","                                                                                      \
    "    \"stat_t\": \"~/s/stat\","                                                                                    \
    "    \"bri_cmd_t\": \"~/c/bri\","                                                                                  \
    "    \"bri_stat_t\": \"~/s/bri\","                                                                                 \
    "    \"brightness_value_template\": \"{{ value_json.brightness }}\","                                              \
    "    \"clr_temp_cmd_t\": \"~/c/clr_temp\","                                                                        \
    "    \"clr_temp_stat_t\": \"~/s/clr_temp\","                                                                       \
    "    \"rgb_cmd_t\": \"~/c/rgb\","                                                                                  \
    "    \"rgb_stat_t\": \"~/s/rgb\","                                                                                 \
    "    \"sup_clrm\": [\"color_temp\",\"rgb\",\"white\"],"                                                            \
    "    \"effect_command_topic\": \"~/c/effect\","                                                                    \
    "    \"effect_state_topic\": \"~/s/effect\","                                                                      \
    "    \"effect\": true,"                                                                                            \
    "    \"effect_list\": ["                                                                                           \
    "        \"Static\","                                                                                              \
    "        \"Blink\","                                                                                               \
    "        \"Breath\","                                                                                              \
    "        \"Color Wipe\""                                                                                           \
    "    ],"                                                                                                           \
    "    \"device\": {"                                                                                                \
    "        \"identifiers\": ["                                                                                       \
    "            \"switch_unit_DEADBEEF\""                                                                             \
    "        ]"                                                                                                        \
    "    }"                                                                                                            \
    "}"

#define LOCK_DESC_TOPIC_DISCOVERY_CONFIG PREFIX_DISCOVERY "/lock/" DEVICE_UNIQUE_TOPIC_ID "/lock_%02u/config"
#define LOCK_DESC_TOPIC_PREFIX           DEVICE_OP_TOPIC_PREFIX "/lock/%02u"
#define LOCK_DESC_TOPIC_COMMAND          LOCK_DESC_TOPIC_PREFIX "/c/cmd"
#define LOCK_DESC_TOPIC_STATE            LOCK_DESC_TOPIC_PREFIX "/s/stat"
#define LOCK_DESC_CONFIG                                                                                               \
    "{"                                                                                                                \
    "    \"~\": \"" LOCK_DESC_TOPIC_PREFIX "\","                                                                       \
    "    \"name\": \"Lock %02u\","                                                                                     \
    "    \"object_id\": \"lock_%02u\","                                                                                \
    "    \"unique_id\": \"lock_" DEVICE_UNIQUE_ID "_%02u\","                                                           \
    "    \"device_class\": \"lock\","                                                                                  \
    "    \"cmd_t\": \"~/c/cmd\","                                                                                      \
    "    \"stat_t\": \"~/s/stat\","                                                                                    \
    "    \"code_format\": \"^\\\\d{4}$\","                                                                             \
    "    \"command_template\": \"{ \\\"action\\\": \\\"{{ value }}\\\", \\\"code\\\":\\\"{{ code }}\\\" }\","          \
    "    \"device\": {"                                                                                                \
    "        \"identifiers\": ["                                                                                       \
    "            \"switch_unit_DEADBEEF\""                                                                             \
    "        ]"                                                                                                        \
    "    }"                                                                                                            \
    "}"

/**
 * \brief           Kelvin to RGB conversion structure
 * 
 */
typedef struct {
    uint16_t mired;
    uint16_t kelvin;
    uint8_t r, g, b;
} mired_kelvin_rgb_pair_t;

/* mired * kelvin = 10000000 */
#define MIRED_KELVIN_TO_RGB_ENTRY(_mired, _kelvin, _r, _g, _b)                                                         \
    { .mired = _mired, .kelvin = _kelvin, .r = _r, .g = _g, .b = _b }
static const mired_kelvin_rgb_pair_t mired_kelvin_rgb_pairs[] = {
    MIRED_KELVIN_TO_RGB_ENTRY(100, 10000, 201, 218, 255), MIRED_KELVIN_TO_RGB_ENTRY(101, 9900, 202, 218, 255),
    MIRED_KELVIN_TO_RGB_ENTRY(102, 9803, 203, 218, 255),  MIRED_KELVIN_TO_RGB_ENTRY(103, 9708, 203, 219, 255),
    MIRED_KELVIN_TO_RGB_ENTRY(104, 9615, 204, 219, 255),  MIRED_KELVIN_TO_RGB_ENTRY(105, 9523, 205, 220, 255),
    MIRED_KELVIN_TO_RGB_ENTRY(106, 9433, 205, 220, 255),  MIRED_KELVIN_TO_RGB_ENTRY(107, 9345, 206, 221, 255),
    MIRED_KELVIN_TO_RGB_ENTRY(108, 9259, 207, 221, 255),  MIRED_KELVIN_TO_RGB_ENTRY(109, 9174, 208, 221, 255),
    MIRED_KELVIN_TO_RGB_ENTRY(110, 9090, 208, 222, 255),  MIRED_KELVIN_TO_RGB_ENTRY(111, 9009, 209, 222, 255),
    MIRED_KELVIN_TO_RGB_ENTRY(112, 8928, 210, 223, 255),  MIRED_KELVIN_TO_RGB_ENTRY(113, 8849, 211, 223, 255),
    MIRED_KELVIN_TO_RGB_ENTRY(114, 8771, 211, 224, 255),  MIRED_KELVIN_TO_RGB_ENTRY(115, 8695, 212, 224, 255),
    MIRED_KELVIN_TO_RGB_ENTRY(116, 8620, 213, 225, 255),  MIRED_KELVIN_TO_RGB_ENTRY(117, 8547, 214, 225, 255),
    MIRED_KELVIN_TO_RGB_ENTRY(118, 8474, 215, 226, 255),  MIRED_KELVIN_TO_RGB_ENTRY(119, 8403, 215, 226, 255),
    MIRED_KELVIN_TO_RGB_ENTRY(120, 8333, 216, 227, 255),  MIRED_KELVIN_TO_RGB_ENTRY(121, 8264, 217, 227, 255),
    MIRED_KELVIN_TO_RGB_ENTRY(122, 8196, 218, 228, 255),  MIRED_KELVIN_TO_RGB_ENTRY(123, 8130, 219, 228, 255),
    MIRED_KELVIN_TO_RGB_ENTRY(124, 8064, 220, 229, 255),  MIRED_KELVIN_TO_RGB_ENTRY(125, 8000, 221, 229, 255),
    MIRED_KELVIN_TO_RGB_ENTRY(126, 7936, 222, 230, 255),  MIRED_KELVIN_TO_RGB_ENTRY(127, 7874, 223, 230, 255),
    MIRED_KELVIN_TO_RGB_ENTRY(128, 7812, 224, 231, 255),  MIRED_KELVIN_TO_RGB_ENTRY(129, 7751, 225, 232, 255),
    MIRED_KELVIN_TO_RGB_ENTRY(130, 7692, 226, 232, 255),  MIRED_KELVIN_TO_RGB_ENTRY(131, 7633, 227, 233, 255),
    MIRED_KELVIN_TO_RGB_ENTRY(132, 7575, 228, 233, 255),  MIRED_KELVIN_TO_RGB_ENTRY(133, 7518, 229, 234, 255),
    MIRED_KELVIN_TO_RGB_ENTRY(134, 7462, 230, 235, 255),  MIRED_KELVIN_TO_RGB_ENTRY(135, 7407, 231, 235, 255),
    MIRED_KELVIN_TO_RGB_ENTRY(136, 7352, 233, 236, 255),  MIRED_KELVIN_TO_RGB_ENTRY(137, 7299, 234, 237, 255),
    MIRED_KELVIN_TO_RGB_ENTRY(138, 7246, 235, 238, 255),  MIRED_KELVIN_TO_RGB_ENTRY(139, 7194, 236, 238, 255),
    MIRED_KELVIN_TO_RGB_ENTRY(140, 7142, 238, 239, 255),  MIRED_KELVIN_TO_RGB_ENTRY(141, 7092, 239, 240, 255),
    MIRED_KELVIN_TO_RGB_ENTRY(142, 7042, 241, 241, 255),  MIRED_KELVIN_TO_RGB_ENTRY(143, 6993, 242, 242, 255),
    MIRED_KELVIN_TO_RGB_ENTRY(144, 6944, 244, 243, 255),  MIRED_KELVIN_TO_RGB_ENTRY(145, 6896, 246, 244, 255),
    MIRED_KELVIN_TO_RGB_ENTRY(146, 6849, 247, 245, 255),  MIRED_KELVIN_TO_RGB_ENTRY(147, 6802, 249, 246, 255),
    MIRED_KELVIN_TO_RGB_ENTRY(148, 6756, 251, 247, 255),  MIRED_KELVIN_TO_RGB_ENTRY(149, 6711, 253, 248, 255),
    MIRED_KELVIN_TO_RGB_ENTRY(150, 6666, 255, 249, 255),  MIRED_KELVIN_TO_RGB_ENTRY(151, 6622, 255, 250, 255),
    MIRED_KELVIN_TO_RGB_ENTRY(152, 6578, 255, 255, 251),  MIRED_KELVIN_TO_RGB_ENTRY(153, 6535, 255, 254, 250),
    MIRED_KELVIN_TO_RGB_ENTRY(154, 6493, 255, 254, 249),  MIRED_KELVIN_TO_RGB_ENTRY(155, 6451, 255, 253, 248),
    MIRED_KELVIN_TO_RGB_ENTRY(156, 6410, 255, 252, 247),  MIRED_KELVIN_TO_RGB_ENTRY(157, 6369, 255, 252, 246),
    MIRED_KELVIN_TO_RGB_ENTRY(158, 6329, 255, 251, 245),  MIRED_KELVIN_TO_RGB_ENTRY(159, 6289, 255, 250, 244),
    MIRED_KELVIN_TO_RGB_ENTRY(160, 6250, 255, 250, 243),  MIRED_KELVIN_TO_RGB_ENTRY(161, 6211, 255, 249, 242),
    MIRED_KELVIN_TO_RGB_ENTRY(162, 6172, 255, 248, 241),  MIRED_KELVIN_TO_RGB_ENTRY(163, 6134, 255, 248, 240),
    MIRED_KELVIN_TO_RGB_ENTRY(164, 6097, 255, 247, 239),  MIRED_KELVIN_TO_RGB_ENTRY(165, 6060, 255, 247, 238),
    MIRED_KELVIN_TO_RGB_ENTRY(166, 6024, 255, 246, 237),  MIRED_KELVIN_TO_RGB_ENTRY(167, 5988, 255, 245, 236),
    MIRED_KELVIN_TO_RGB_ENTRY(168, 5952, 255, 245, 235),  MIRED_KELVIN_TO_RGB_ENTRY(169, 5917, 255, 244, 234),
    MIRED_KELVIN_TO_RGB_ENTRY(170, 5882, 255, 244, 233),  MIRED_KELVIN_TO_RGB_ENTRY(171, 5847, 255, 243, 232),
    MIRED_KELVIN_TO_RGB_ENTRY(172, 5813, 255, 242, 231),  MIRED_KELVIN_TO_RGB_ENTRY(173, 5780, 255, 242, 230),
    MIRED_KELVIN_TO_RGB_ENTRY(174, 5747, 255, 241, 229),  MIRED_KELVIN_TO_RGB_ENTRY(175, 5714, 255, 241, 228),
    MIRED_KELVIN_TO_RGB_ENTRY(176, 5681, 255, 240, 227),  MIRED_KELVIN_TO_RGB_ENTRY(177, 5649, 255, 240, 226),
    MIRED_KELVIN_TO_RGB_ENTRY(178, 5617, 255, 239, 225),  MIRED_KELVIN_TO_RGB_ENTRY(179, 5586, 255, 239, 224),
    MIRED_KELVIN_TO_RGB_ENTRY(180, 5555, 255, 238, 223),  MIRED_KELVIN_TO_RGB_ENTRY(181, 5524, 255, 237, 222),
    MIRED_KELVIN_TO_RGB_ENTRY(182, 5494, 255, 237, 222),  MIRED_KELVIN_TO_RGB_ENTRY(183, 5464, 255, 236, 221),
    MIRED_KELVIN_TO_RGB_ENTRY(184, 5434, 255, 236, 220),  MIRED_KELVIN_TO_RGB_ENTRY(185, 5405, 255, 235, 219),
    MIRED_KELVIN_TO_RGB_ENTRY(186, 5376, 255, 235, 218),  MIRED_KELVIN_TO_RGB_ENTRY(187, 5347, 255, 234, 217),
    MIRED_KELVIN_TO_RGB_ENTRY(188, 5319, 255, 234, 216),  MIRED_KELVIN_TO_RGB_ENTRY(189, 5291, 255, 233, 215),
    MIRED_KELVIN_TO_RGB_ENTRY(190, 5263, 255, 233, 214),  MIRED_KELVIN_TO_RGB_ENTRY(191, 5235, 255, 232, 213),
    MIRED_KELVIN_TO_RGB_ENTRY(192, 5208, 255, 232, 212),  MIRED_KELVIN_TO_RGB_ENTRY(193, 5181, 255, 231, 212),
    MIRED_KELVIN_TO_RGB_ENTRY(194, 5154, 255, 231, 211),  MIRED_KELVIN_TO_RGB_ENTRY(195, 5128, 255, 230, 210),
    MIRED_KELVIN_TO_RGB_ENTRY(196, 5102, 255, 230, 209),  MIRED_KELVIN_TO_RGB_ENTRY(197, 5076, 255, 229, 208),
    MIRED_KELVIN_TO_RGB_ENTRY(198, 5050, 255, 229, 207),  MIRED_KELVIN_TO_RGB_ENTRY(199, 5025, 255, 228, 206),
    MIRED_KELVIN_TO_RGB_ENTRY(200, 5000, 255, 228, 205),  MIRED_KELVIN_TO_RGB_ENTRY(201, 4975, 255, 227, 205),
    MIRED_KELVIN_TO_RGB_ENTRY(202, 4950, 255, 227, 204),  MIRED_KELVIN_TO_RGB_ENTRY(203, 4926, 255, 226, 203),
    MIRED_KELVIN_TO_RGB_ENTRY(204, 4901, 255, 226, 202),  MIRED_KELVIN_TO_RGB_ENTRY(205, 4878, 255, 225, 201),
    MIRED_KELVIN_TO_RGB_ENTRY(206, 4854, 255, 225, 200),  MIRED_KELVIN_TO_RGB_ENTRY(207, 4830, 255, 224, 199),
    MIRED_KELVIN_TO_RGB_ENTRY(208, 4807, 255, 224, 199),  MIRED_KELVIN_TO_RGB_ENTRY(209, 4784, 255, 223, 198),
    MIRED_KELVIN_TO_RGB_ENTRY(210, 4761, 255, 223, 197),  MIRED_KELVIN_TO_RGB_ENTRY(211, 4739, 255, 222, 196),
    MIRED_KELVIN_TO_RGB_ENTRY(212, 4716, 255, 222, 195),  MIRED_KELVIN_TO_RGB_ENTRY(213, 4694, 255, 221, 194),
    MIRED_KELVIN_TO_RGB_ENTRY(214, 4672, 255, 221, 194),  MIRED_KELVIN_TO_RGB_ENTRY(215, 4651, 255, 220, 193),
    MIRED_KELVIN_TO_RGB_ENTRY(216, 4629, 255, 220, 192),  MIRED_KELVIN_TO_RGB_ENTRY(217, 4608, 255, 219, 191),
    MIRED_KELVIN_TO_RGB_ENTRY(218, 4587, 255, 219, 190),  MIRED_KELVIN_TO_RGB_ENTRY(219, 4566, 255, 218, 190),
    MIRED_KELVIN_TO_RGB_ENTRY(220, 4545, 255, 218, 189),  MIRED_KELVIN_TO_RGB_ENTRY(221, 4524, 255, 218, 188),
    MIRED_KELVIN_TO_RGB_ENTRY(222, 4504, 255, 217, 187),  MIRED_KELVIN_TO_RGB_ENTRY(223, 4484, 255, 217, 186),
    MIRED_KELVIN_TO_RGB_ENTRY(224, 4464, 255, 216, 186),  MIRED_KELVIN_TO_RGB_ENTRY(225, 4444, 255, 216, 185),
    MIRED_KELVIN_TO_RGB_ENTRY(226, 4424, 255, 215, 184),  MIRED_KELVIN_TO_RGB_ENTRY(227, 4405, 255, 215, 183),
    MIRED_KELVIN_TO_RGB_ENTRY(228, 4385, 255, 214, 182),  MIRED_KELVIN_TO_RGB_ENTRY(229, 4366, 255, 214, 182),
    MIRED_KELVIN_TO_RGB_ENTRY(230, 4347, 255, 214, 181),  MIRED_KELVIN_TO_RGB_ENTRY(231, 4329, 255, 213, 180),
    MIRED_KELVIN_TO_RGB_ENTRY(232, 4310, 255, 213, 179),  MIRED_KELVIN_TO_RGB_ENTRY(233, 4291, 255, 212, 178),
    MIRED_KELVIN_TO_RGB_ENTRY(234, 4273, 255, 212, 178),  MIRED_KELVIN_TO_RGB_ENTRY(235, 4255, 255, 211, 177),
    MIRED_KELVIN_TO_RGB_ENTRY(236, 4237, 255, 211, 176),  MIRED_KELVIN_TO_RGB_ENTRY(237, 4219, 255, 211, 175),
    MIRED_KELVIN_TO_RGB_ENTRY(238, 4201, 255, 210, 175),  MIRED_KELVIN_TO_RGB_ENTRY(239, 4184, 255, 210, 174),
    MIRED_KELVIN_TO_RGB_ENTRY(240, 4166, 255, 209, 173),  MIRED_KELVIN_TO_RGB_ENTRY(241, 4149, 255, 209, 172),
    MIRED_KELVIN_TO_RGB_ENTRY(242, 4132, 255, 209, 172),  MIRED_KELVIN_TO_RGB_ENTRY(243, 4115, 255, 208, 171),
    MIRED_KELVIN_TO_RGB_ENTRY(244, 4098, 255, 208, 170),  MIRED_KELVIN_TO_RGB_ENTRY(245, 4081, 255, 207, 169),
    MIRED_KELVIN_TO_RGB_ENTRY(246, 4065, 255, 207, 169),  MIRED_KELVIN_TO_RGB_ENTRY(247, 4048, 255, 207, 168),
    MIRED_KELVIN_TO_RGB_ENTRY(248, 4032, 255, 206, 167),  MIRED_KELVIN_TO_RGB_ENTRY(249, 4016, 255, 206, 166),
    MIRED_KELVIN_TO_RGB_ENTRY(250, 4000, 255, 205, 166),  MIRED_KELVIN_TO_RGB_ENTRY(251, 3984, 255, 205, 165),
    MIRED_KELVIN_TO_RGB_ENTRY(252, 3968, 255, 205, 164),  MIRED_KELVIN_TO_RGB_ENTRY(253, 3952, 255, 204, 163),
    MIRED_KELVIN_TO_RGB_ENTRY(254, 3937, 255, 204, 163),  MIRED_KELVIN_TO_RGB_ENTRY(255, 3921, 255, 203, 162),
    MIRED_KELVIN_TO_RGB_ENTRY(256, 3906, 255, 203, 161),  MIRED_KELVIN_TO_RGB_ENTRY(257, 3891, 255, 203, 160),
    MIRED_KELVIN_TO_RGB_ENTRY(258, 3875, 255, 202, 160),  MIRED_KELVIN_TO_RGB_ENTRY(259, 3861, 255, 202, 159),
    MIRED_KELVIN_TO_RGB_ENTRY(260, 3846, 255, 201, 158),  MIRED_KELVIN_TO_RGB_ENTRY(261, 3831, 255, 201, 158),
    MIRED_KELVIN_TO_RGB_ENTRY(262, 3816, 255, 201, 157),  MIRED_KELVIN_TO_RGB_ENTRY(263, 3802, 255, 200, 156),
    MIRED_KELVIN_TO_RGB_ENTRY(264, 3787, 255, 200, 155),  MIRED_KELVIN_TO_RGB_ENTRY(265, 3773, 255, 200, 155),
    MIRED_KELVIN_TO_RGB_ENTRY(266, 3759, 255, 199, 154),  MIRED_KELVIN_TO_RGB_ENTRY(267, 3745, 255, 199, 153),
    MIRED_KELVIN_TO_RGB_ENTRY(268, 3731, 255, 198, 153),  MIRED_KELVIN_TO_RGB_ENTRY(269, 3717, 255, 198, 152),
    MIRED_KELVIN_TO_RGB_ENTRY(270, 3703, 255, 198, 151),  MIRED_KELVIN_TO_RGB_ENTRY(271, 3690, 255, 197, 150),
    MIRED_KELVIN_TO_RGB_ENTRY(272, 3676, 255, 197, 150),  MIRED_KELVIN_TO_RGB_ENTRY(273, 3663, 255, 197, 149),
    MIRED_KELVIN_TO_RGB_ENTRY(274, 3649, 255, 196, 148),  MIRED_KELVIN_TO_RGB_ENTRY(275, 3636, 255, 196, 148),
    MIRED_KELVIN_TO_RGB_ENTRY(276, 3623, 255, 195, 147),  MIRED_KELVIN_TO_RGB_ENTRY(277, 3610, 255, 195, 146),
    MIRED_KELVIN_TO_RGB_ENTRY(278, 3597, 255, 195, 146),  MIRED_KELVIN_TO_RGB_ENTRY(279, 3584, 255, 194, 145),
    MIRED_KELVIN_TO_RGB_ENTRY(280, 3571, 255, 194, 144),  MIRED_KELVIN_TO_RGB_ENTRY(281, 3558, 255, 194, 144),
    MIRED_KELVIN_TO_RGB_ENTRY(282, 3546, 255, 193, 143),  MIRED_KELVIN_TO_RGB_ENTRY(283, 3533, 255, 193, 142),
    MIRED_KELVIN_TO_RGB_ENTRY(284, 3521, 255, 193, 141),  MIRED_KELVIN_TO_RGB_ENTRY(285, 3508, 255, 192, 141),
    MIRED_KELVIN_TO_RGB_ENTRY(286, 3496, 255, 192, 140),  MIRED_KELVIN_TO_RGB_ENTRY(287, 3484, 255, 192, 139),
    MIRED_KELVIN_TO_RGB_ENTRY(288, 3472, 255, 191, 139),  MIRED_KELVIN_TO_RGB_ENTRY(289, 3460, 255, 191, 138),
    MIRED_KELVIN_TO_RGB_ENTRY(290, 3448, 255, 191, 137),  MIRED_KELVIN_TO_RGB_ENTRY(291, 3436, 255, 190, 137),
    MIRED_KELVIN_TO_RGB_ENTRY(292, 3424, 255, 190, 136),  MIRED_KELVIN_TO_RGB_ENTRY(293, 3412, 255, 190, 135),
    MIRED_KELVIN_TO_RGB_ENTRY(294, 3401, 255, 189, 135),  MIRED_KELVIN_TO_RGB_ENTRY(295, 3389, 255, 189, 134),
    MIRED_KELVIN_TO_RGB_ENTRY(296, 3378, 255, 189, 133),  MIRED_KELVIN_TO_RGB_ENTRY(297, 3367, 255, 188, 133),
    MIRED_KELVIN_TO_RGB_ENTRY(298, 3355, 255, 188, 132),  MIRED_KELVIN_TO_RGB_ENTRY(299, 3344, 255, 187, 131),
    MIRED_KELVIN_TO_RGB_ENTRY(300, 3333, 255, 187, 131),  MIRED_KELVIN_TO_RGB_ENTRY(301, 3322, 255, 187, 130),
    MIRED_KELVIN_TO_RGB_ENTRY(302, 3311, 255, 187, 129),  MIRED_KELVIN_TO_RGB_ENTRY(303, 3300, 255, 186, 129),
    MIRED_KELVIN_TO_RGB_ENTRY(304, 3289, 255, 186, 128),  MIRED_KELVIN_TO_RGB_ENTRY(305, 3278, 255, 186, 127),
    MIRED_KELVIN_TO_RGB_ENTRY(306, 3267, 255, 185, 127),  MIRED_KELVIN_TO_RGB_ENTRY(307, 3257, 255, 185, 126),
    MIRED_KELVIN_TO_RGB_ENTRY(308, 3246, 255, 185, 125),  MIRED_KELVIN_TO_RGB_ENTRY(309, 3236, 255, 184, 125),
    MIRED_KELVIN_TO_RGB_ENTRY(310, 3225, 255, 184, 124),  MIRED_KELVIN_TO_RGB_ENTRY(311, 3215, 255, 184, 124),
    MIRED_KELVIN_TO_RGB_ENTRY(312, 3205, 255, 183, 123),  MIRED_KELVIN_TO_RGB_ENTRY(313, 3194, 255, 183, 122),
    MIRED_KELVIN_TO_RGB_ENTRY(314, 3184, 255, 183, 122),  MIRED_KELVIN_TO_RGB_ENTRY(315, 3174, 255, 182, 121),
    MIRED_KELVIN_TO_RGB_ENTRY(316, 3164, 255, 182, 120),  MIRED_KELVIN_TO_RGB_ENTRY(317, 3154, 255, 182, 120),
    MIRED_KELVIN_TO_RGB_ENTRY(318, 3144, 255, 181, 119),  MIRED_KELVIN_TO_RGB_ENTRY(319, 3134, 255, 181, 118),
    MIRED_KELVIN_TO_RGB_ENTRY(320, 3125, 255, 181, 118),  MIRED_KELVIN_TO_RGB_ENTRY(321, 3115, 255, 180, 117),
    MIRED_KELVIN_TO_RGB_ENTRY(322, 3105, 255, 180, 117),  MIRED_KELVIN_TO_RGB_ENTRY(323, 3095, 255, 180, 116),
    MIRED_KELVIN_TO_RGB_ENTRY(324, 3086, 255, 180, 115),  MIRED_KELVIN_TO_RGB_ENTRY(325, 3076, 255, 179, 115),
    MIRED_KELVIN_TO_RGB_ENTRY(326, 3067, 255, 179, 114),  MIRED_KELVIN_TO_RGB_ENTRY(327, 3058, 255, 179, 113),
    MIRED_KELVIN_TO_RGB_ENTRY(328, 3048, 255, 178, 113),  MIRED_KELVIN_TO_RGB_ENTRY(329, 3039, 255, 178, 112),
    MIRED_KELVIN_TO_RGB_ENTRY(330, 3030, 255, 178, 111),  MIRED_KELVIN_TO_RGB_ENTRY(331, 3021, 255, 177, 111),
    MIRED_KELVIN_TO_RGB_ENTRY(332, 3012, 255, 177, 110),  MIRED_KELVIN_TO_RGB_ENTRY(333, 3003, 255, 177, 110),
    MIRED_KELVIN_TO_RGB_ENTRY(334, 2994, 255, 177, 109),  MIRED_KELVIN_TO_RGB_ENTRY(335, 2985, 255, 176, 108),
    MIRED_KELVIN_TO_RGB_ENTRY(336, 2976, 255, 176, 108),  MIRED_KELVIN_TO_RGB_ENTRY(337, 2967, 255, 176, 107),
    MIRED_KELVIN_TO_RGB_ENTRY(338, 2958, 255, 175, 106),  MIRED_KELVIN_TO_RGB_ENTRY(339, 2949, 255, 175, 106),
    MIRED_KELVIN_TO_RGB_ENTRY(340, 2941, 255, 175, 105),  MIRED_KELVIN_TO_RGB_ENTRY(341, 2932, 255, 174, 105),
    MIRED_KELVIN_TO_RGB_ENTRY(342, 2923, 255, 174, 104),  MIRED_KELVIN_TO_RGB_ENTRY(343, 2915, 255, 174, 103),
    MIRED_KELVIN_TO_RGB_ENTRY(344, 2906, 255, 174, 103),  MIRED_KELVIN_TO_RGB_ENTRY(345, 2898, 255, 173, 102),
    MIRED_KELVIN_TO_RGB_ENTRY(346, 2890, 255, 173, 102),  MIRED_KELVIN_TO_RGB_ENTRY(347, 2881, 255, 173, 101),
    MIRED_KELVIN_TO_RGB_ENTRY(348, 2873, 255, 172, 100),  MIRED_KELVIN_TO_RGB_ENTRY(349, 2865, 255, 172, 100),
    MIRED_KELVIN_TO_RGB_ENTRY(350, 2857, 255, 172, 99),   MIRED_KELVIN_TO_RGB_ENTRY(351, 2849, 255, 172, 99),
    MIRED_KELVIN_TO_RGB_ENTRY(352, 2840, 255, 171, 98),   MIRED_KELVIN_TO_RGB_ENTRY(353, 2832, 255, 171, 97),
    MIRED_KELVIN_TO_RGB_ENTRY(354, 2824, 255, 171, 97),   MIRED_KELVIN_TO_RGB_ENTRY(355, 2816, 255, 170, 96),
    MIRED_KELVIN_TO_RGB_ENTRY(356, 2808, 255, 170, 95),   MIRED_KELVIN_TO_RGB_ENTRY(357, 2801, 255, 170, 95),
    MIRED_KELVIN_TO_RGB_ENTRY(358, 2793, 255, 170, 94),   MIRED_KELVIN_TO_RGB_ENTRY(359, 2785, 255, 169, 94),
    MIRED_KELVIN_TO_RGB_ENTRY(360, 2777, 255, 169, 93),   MIRED_KELVIN_TO_RGB_ENTRY(361, 2770, 255, 169, 92),
    MIRED_KELVIN_TO_RGB_ENTRY(362, 2762, 255, 168, 92),   MIRED_KELVIN_TO_RGB_ENTRY(363, 2754, 255, 168, 91),
    MIRED_KELVIN_TO_RGB_ENTRY(364, 2747, 255, 168, 91),   MIRED_KELVIN_TO_RGB_ENTRY(365, 2739, 255, 168, 90),
    MIRED_KELVIN_TO_RGB_ENTRY(366, 2732, 255, 167, 89),   MIRED_KELVIN_TO_RGB_ENTRY(367, 2724, 255, 167, 89),
    MIRED_KELVIN_TO_RGB_ENTRY(368, 2717, 255, 167, 88),   MIRED_KELVIN_TO_RGB_ENTRY(369, 2710, 255, 167, 88),
    MIRED_KELVIN_TO_RGB_ENTRY(370, 2702, 255, 166, 87),   MIRED_KELVIN_TO_RGB_ENTRY(371, 2695, 255, 166, 86),
    MIRED_KELVIN_TO_RGB_ENTRY(372, 2688, 255, 166, 86),   MIRED_KELVIN_TO_RGB_ENTRY(373, 2680, 255, 165, 85),
    MIRED_KELVIN_TO_RGB_ENTRY(374, 2673, 255, 165, 85),   MIRED_KELVIN_TO_RGB_ENTRY(375, 2666, 255, 165, 84),
    MIRED_KELVIN_TO_RGB_ENTRY(376, 2659, 255, 165, 84),   MIRED_KELVIN_TO_RGB_ENTRY(377, 2652, 255, 164, 83),
    MIRED_KELVIN_TO_RGB_ENTRY(378, 2645, 255, 164, 82),   MIRED_KELVIN_TO_RGB_ENTRY(379, 2638, 255, 164, 82),
    MIRED_KELVIN_TO_RGB_ENTRY(380, 2631, 255, 164, 81),   MIRED_KELVIN_TO_RGB_ENTRY(381, 2624, 255, 163, 81),
    MIRED_KELVIN_TO_RGB_ENTRY(382, 2617, 255, 163, 80),   MIRED_KELVIN_TO_RGB_ENTRY(383, 2610, 255, 163, 79),
    MIRED_KELVIN_TO_RGB_ENTRY(384, 2604, 255, 163, 79),   MIRED_KELVIN_TO_RGB_ENTRY(385, 2597, 255, 162, 78),
    MIRED_KELVIN_TO_RGB_ENTRY(386, 2590, 255, 162, 78),   MIRED_KELVIN_TO_RGB_ENTRY(387, 2583, 255, 162, 77),
    MIRED_KELVIN_TO_RGB_ENTRY(388, 2577, 255, 162, 77),   MIRED_KELVIN_TO_RGB_ENTRY(389, 2570, 255, 161, 76),
    MIRED_KELVIN_TO_RGB_ENTRY(390, 2564, 255, 161, 75),   MIRED_KELVIN_TO_RGB_ENTRY(391, 2557, 255, 161, 75),
    MIRED_KELVIN_TO_RGB_ENTRY(392, 2551, 255, 161, 74),   MIRED_KELVIN_TO_RGB_ENTRY(393, 2544, 255, 160, 74),
    MIRED_KELVIN_TO_RGB_ENTRY(394, 2538, 255, 160, 73),   MIRED_KELVIN_TO_RGB_ENTRY(395, 2531, 255, 160, 72),
    MIRED_KELVIN_TO_RGB_ENTRY(396, 2525, 255, 160, 72),   MIRED_KELVIN_TO_RGB_ENTRY(397, 2518, 255, 159, 71),
    MIRED_KELVIN_TO_RGB_ENTRY(398, 2512, 255, 159, 71),   MIRED_KELVIN_TO_RGB_ENTRY(399, 2506, 255, 159, 70),
    MIRED_KELVIN_TO_RGB_ENTRY(400, 2500, 255, 159, 70),   MIRED_KELVIN_TO_RGB_ENTRY(401, 2493, 255, 158, 69),
    MIRED_KELVIN_TO_RGB_ENTRY(402, 2487, 255, 158, 68),   MIRED_KELVIN_TO_RGB_ENTRY(403, 2481, 255, 158, 68),
    MIRED_KELVIN_TO_RGB_ENTRY(404, 2475, 255, 158, 67),   MIRED_KELVIN_TO_RGB_ENTRY(405, 2469, 255, 157, 67),
    MIRED_KELVIN_TO_RGB_ENTRY(406, 2463, 255, 157, 66),   MIRED_KELVIN_TO_RGB_ENTRY(407, 2457, 255, 157, 66),
    MIRED_KELVIN_TO_RGB_ENTRY(408, 2450, 255, 157, 65),   MIRED_KELVIN_TO_RGB_ENTRY(409, 2444, 255, 156, 64),
    MIRED_KELVIN_TO_RGB_ENTRY(410, 2439, 255, 156, 64),   MIRED_KELVIN_TO_RGB_ENTRY(411, 2433, 255, 156, 63),
    MIRED_KELVIN_TO_RGB_ENTRY(412, 2427, 255, 156, 63),   MIRED_KELVIN_TO_RGB_ENTRY(413, 2421, 255, 155, 62),
    MIRED_KELVIN_TO_RGB_ENTRY(414, 2415, 255, 155, 61),   MIRED_KELVIN_TO_RGB_ENTRY(415, 2409, 255, 155, 61),
    MIRED_KELVIN_TO_RGB_ENTRY(416, 2403, 255, 155, 60),   MIRED_KELVIN_TO_RGB_ENTRY(417, 2398, 255, 154, 60),
    MIRED_KELVIN_TO_RGB_ENTRY(418, 2392, 255, 154, 59),   MIRED_KELVIN_TO_RGB_ENTRY(419, 2386, 255, 154, 59),
    MIRED_KELVIN_TO_RGB_ENTRY(420, 2380, 255, 154, 58),   MIRED_KELVIN_TO_RGB_ENTRY(421, 2375, 255, 153, 58),
    MIRED_KELVIN_TO_RGB_ENTRY(422, 2369, 255, 153, 57),   MIRED_KELVIN_TO_RGB_ENTRY(423, 2364, 255, 153, 56),
    MIRED_KELVIN_TO_RGB_ENTRY(424, 2358, 255, 153, 56),   MIRED_KELVIN_TO_RGB_ENTRY(425, 2352, 255, 152, 55),
    MIRED_KELVIN_TO_RGB_ENTRY(426, 2347, 255, 152, 55),   MIRED_KELVIN_TO_RGB_ENTRY(427, 2341, 255, 152, 54),
    MIRED_KELVIN_TO_RGB_ENTRY(428, 2336, 255, 152, 54),   MIRED_KELVIN_TO_RGB_ENTRY(429, 2331, 255, 152, 53),
    MIRED_KELVIN_TO_RGB_ENTRY(430, 2325, 255, 151, 52),   MIRED_KELVIN_TO_RGB_ENTRY(431, 2320, 255, 151, 52),
    MIRED_KELVIN_TO_RGB_ENTRY(432, 2314, 255, 151, 51),   MIRED_KELVIN_TO_RGB_ENTRY(433, 2309, 255, 151, 51),
    MIRED_KELVIN_TO_RGB_ENTRY(434, 2304, 255, 150, 50),   MIRED_KELVIN_TO_RGB_ENTRY(435, 2298, 255, 150, 50),
    MIRED_KELVIN_TO_RGB_ENTRY(436, 2293, 255, 150, 49),   MIRED_KELVIN_TO_RGB_ENTRY(437, 2288, 255, 150, 48),
    MIRED_KELVIN_TO_RGB_ENTRY(438, 2283, 255, 150, 48),   MIRED_KELVIN_TO_RGB_ENTRY(439, 2277, 255, 149, 47),
    MIRED_KELVIN_TO_RGB_ENTRY(440, 2272, 255, 149, 47),   MIRED_KELVIN_TO_RGB_ENTRY(441, 2267, 255, 149, 46),
    MIRED_KELVIN_TO_RGB_ENTRY(442, 2262, 255, 149, 46),   MIRED_KELVIN_TO_RGB_ENTRY(443, 2257, 255, 148, 45),
    MIRED_KELVIN_TO_RGB_ENTRY(444, 2252, 255, 148, 45),   MIRED_KELVIN_TO_RGB_ENTRY(445, 2247, 255, 148, 44),
    MIRED_KELVIN_TO_RGB_ENTRY(446, 2242, 255, 148, 43),   MIRED_KELVIN_TO_RGB_ENTRY(447, 2237, 255, 148, 43),
    MIRED_KELVIN_TO_RGB_ENTRY(448, 2232, 255, 147, 42),   MIRED_KELVIN_TO_RGB_ENTRY(449, 2227, 255, 147, 42),
    MIRED_KELVIN_TO_RGB_ENTRY(450, 2222, 255, 147, 41),   MIRED_KELVIN_TO_RGB_ENTRY(451, 2217, 255, 147, 41),
    MIRED_KELVIN_TO_RGB_ENTRY(452, 2212, 255, 146, 40),   MIRED_KELVIN_TO_RGB_ENTRY(453, 2207, 255, 146, 39),
    MIRED_KELVIN_TO_RGB_ENTRY(454, 2202, 255, 146, 39),   MIRED_KELVIN_TO_RGB_ENTRY(455, 2197, 255, 146, 38),
    MIRED_KELVIN_TO_RGB_ENTRY(456, 2192, 255, 145, 38),   MIRED_KELVIN_TO_RGB_ENTRY(457, 2188, 255, 145, 37),
    MIRED_KELVIN_TO_RGB_ENTRY(458, 2183, 255, 145, 37),   MIRED_KELVIN_TO_RGB_ENTRY(459, 2178, 255, 145, 36),
    MIRED_KELVIN_TO_RGB_ENTRY(460, 2173, 255, 145, 36),   MIRED_KELVIN_TO_RGB_ENTRY(461, 2169, 255, 144, 35),
    MIRED_KELVIN_TO_RGB_ENTRY(462, 2164, 255, 144, 34),   MIRED_KELVIN_TO_RGB_ENTRY(463, 2159, 255, 144, 34),
    MIRED_KELVIN_TO_RGB_ENTRY(464, 2155, 255, 144, 33),   MIRED_KELVIN_TO_RGB_ENTRY(465, 2150, 255, 144, 33),
    MIRED_KELVIN_TO_RGB_ENTRY(466, 2145, 255, 143, 32),   MIRED_KELVIN_TO_RGB_ENTRY(467, 2141, 255, 143, 32),
    MIRED_KELVIN_TO_RGB_ENTRY(468, 2136, 255, 143, 31),   MIRED_KELVIN_TO_RGB_ENTRY(469, 2132, 255, 143, 31),
    MIRED_KELVIN_TO_RGB_ENTRY(470, 2127, 255, 142, 30),   MIRED_KELVIN_TO_RGB_ENTRY(471, 2123, 255, 142, 29),
    MIRED_KELVIN_TO_RGB_ENTRY(472, 2118, 255, 142, 29),   MIRED_KELVIN_TO_RGB_ENTRY(473, 2114, 255, 142, 28),
    MIRED_KELVIN_TO_RGB_ENTRY(474, 2109, 255, 142, 28),   MIRED_KELVIN_TO_RGB_ENTRY(475, 2105, 255, 141, 27),
    MIRED_KELVIN_TO_RGB_ENTRY(476, 2100, 255, 141, 27),   MIRED_KELVIN_TO_RGB_ENTRY(477, 2096, 255, 141, 26),
    MIRED_KELVIN_TO_RGB_ENTRY(478, 2092, 255, 141, 26),   MIRED_KELVIN_TO_RGB_ENTRY(479, 2087, 255, 141, 25),
    MIRED_KELVIN_TO_RGB_ENTRY(480, 2083, 255, 140, 24),   MIRED_KELVIN_TO_RGB_ENTRY(481, 2079, 255, 140, 24),
    MIRED_KELVIN_TO_RGB_ENTRY(482, 2074, 255, 140, 23),   MIRED_KELVIN_TO_RGB_ENTRY(483, 2070, 255, 140, 23),
    MIRED_KELVIN_TO_RGB_ENTRY(484, 2066, 255, 140, 22),   MIRED_KELVIN_TO_RGB_ENTRY(485, 2061, 255, 139, 22),
    MIRED_KELVIN_TO_RGB_ENTRY(486, 2057, 255, 139, 21),   MIRED_KELVIN_TO_RGB_ENTRY(487, 2053, 255, 139, 21),
    MIRED_KELVIN_TO_RGB_ENTRY(488, 2049, 255, 139, 20),   MIRED_KELVIN_TO_RGB_ENTRY(489, 2044, 255, 139, 19),
    MIRED_KELVIN_TO_RGB_ENTRY(490, 2040, 255, 138, 19),   MIRED_KELVIN_TO_RGB_ENTRY(491, 2036, 255, 138, 18),
    MIRED_KELVIN_TO_RGB_ENTRY(492, 2032, 255, 138, 18),   MIRED_KELVIN_TO_RGB_ENTRY(493, 2028, 255, 138, 17),
    MIRED_KELVIN_TO_RGB_ENTRY(494, 2024, 255, 138, 17),   MIRED_KELVIN_TO_RGB_ENTRY(495, 2020, 255, 137, 16),
    MIRED_KELVIN_TO_RGB_ENTRY(496, 2016, 255, 137, 16),   MIRED_KELVIN_TO_RGB_ENTRY(497, 2012, 255, 137, 15),
    MIRED_KELVIN_TO_RGB_ENTRY(498, 2008, 255, 137, 15),   MIRED_KELVIN_TO_RGB_ENTRY(499, 2004, 255, 137, 14),
    MIRED_KELVIN_TO_RGB_ENTRY(500, 2000, 255, 136, 13),   MIRED_KELVIN_TO_RGB_ENTRY(501, 1996, 255, 136, 13),
    MIRED_KELVIN_TO_RGB_ENTRY(502, 1992, 255, 136, 12),   MIRED_KELVIN_TO_RGB_ENTRY(503, 1988, 255, 136, 12),
    MIRED_KELVIN_TO_RGB_ENTRY(504, 1984, 255, 136, 11),   MIRED_KELVIN_TO_RGB_ENTRY(505, 1980, 255, 135, 11),
    MIRED_KELVIN_TO_RGB_ENTRY(506, 1976, 255, 135, 10),   MIRED_KELVIN_TO_RGB_ENTRY(507, 1972, 255, 135, 9),
    MIRED_KELVIN_TO_RGB_ENTRY(508, 1968, 255, 135, 9),    MIRED_KELVIN_TO_RGB_ENTRY(509, 1964, 255, 135, 8),
    MIRED_KELVIN_TO_RGB_ENTRY(510, 1960, 255, 134, 8),    MIRED_KELVIN_TO_RGB_ENTRY(511, 1956, 255, 134, 7),
    MIRED_KELVIN_TO_RGB_ENTRY(512, 1953, 255, 134, 7),    MIRED_KELVIN_TO_RGB_ENTRY(513, 1949, 255, 134, 6),
    MIRED_KELVIN_TO_RGB_ENTRY(514, 1945, 255, 134, 6),    MIRED_KELVIN_TO_RGB_ENTRY(515, 1941, 255, 133, 5),
    MIRED_KELVIN_TO_RGB_ENTRY(516, 1937, 255, 133, 4),    MIRED_KELVIN_TO_RGB_ENTRY(517, 1934, 255, 133, 4),
    MIRED_KELVIN_TO_RGB_ENTRY(518, 1930, 255, 133, 3),    MIRED_KELVIN_TO_RGB_ENTRY(519, 1926, 255, 133, 3),
    MIRED_KELVIN_TO_RGB_ENTRY(520, 1923, 255, 132, 2),    MIRED_KELVIN_TO_RGB_ENTRY(521, 1919, 255, 132, 2),
    MIRED_KELVIN_TO_RGB_ENTRY(522, 1915, 255, 132, 1),    MIRED_KELVIN_TO_RGB_ENTRY(523, 1912, 255, 132, 1),
    MIRED_KELVIN_TO_RGB_ENTRY(524, 1908, 255, 132, 0),    MIRED_KELVIN_TO_RGB_ENTRY(525, 1904, 255, 131, 0),
    MIRED_KELVIN_TO_RGB_ENTRY(526, 1901, 255, 131, 0),    MIRED_KELVIN_TO_RGB_ENTRY(527, 1897, 255, 131, 0),
    MIRED_KELVIN_TO_RGB_ENTRY(528, 1893, 255, 131, 0),    MIRED_KELVIN_TO_RGB_ENTRY(529, 1890, 255, 131, 0),
    MIRED_KELVIN_TO_RGB_ENTRY(530, 1886, 255, 131, 0),    MIRED_KELVIN_TO_RGB_ENTRY(531, 1883, 255, 130, 0),
    MIRED_KELVIN_TO_RGB_ENTRY(532, 1879, 255, 130, 0),    MIRED_KELVIN_TO_RGB_ENTRY(533, 1876, 255, 130, 0),
    MIRED_KELVIN_TO_RGB_ENTRY(534, 1872, 255, 130, 0),    MIRED_KELVIN_TO_RGB_ENTRY(535, 1869, 255, 130, 0),
    MIRED_KELVIN_TO_RGB_ENTRY(536, 1865, 255, 129, 0),    MIRED_KELVIN_TO_RGB_ENTRY(537, 1862, 255, 129, 0),
    MIRED_KELVIN_TO_RGB_ENTRY(538, 1858, 255, 129, 0),    MIRED_KELVIN_TO_RGB_ENTRY(539, 1855, 255, 129, 0),
    MIRED_KELVIN_TO_RGB_ENTRY(540, 1851, 255, 129, 0),    MIRED_KELVIN_TO_RGB_ENTRY(541, 1848, 255, 129, 0),
    MIRED_KELVIN_TO_RGB_ENTRY(542, 1845, 255, 128, 0),    MIRED_KELVIN_TO_RGB_ENTRY(543, 1841, 255, 128, 0),
    MIRED_KELVIN_TO_RGB_ENTRY(544, 1838, 255, 128, 0),    MIRED_KELVIN_TO_RGB_ENTRY(545, 1834, 255, 128, 0),
    MIRED_KELVIN_TO_RGB_ENTRY(546, 1831, 255, 128, 0),    MIRED_KELVIN_TO_RGB_ENTRY(547, 1828, 255, 127, 0),
    MIRED_KELVIN_TO_RGB_ENTRY(548, 1824, 255, 127, 0),    MIRED_KELVIN_TO_RGB_ENTRY(549, 1821, 255, 127, 0),
};

/* Argument data... */
typedef enum {
    ARG_GROUP_1 = 0x01, /*!< Must start with non-zero, which indicates "NULL" */
    ARG_GROUP_2,
    ARG_GROUP_3,
    ARG_GROUP_4,
    ARG_GROUP_5,
    ARG_GROUP_6,
} arg_groups_t;

typedef enum {
    /* The three commands we can receive */
    LOCK_STATE_LOCK = 0,
    LOCK_STATE_UNLOCK,
    LOCK_STATE_OPEN,

    /* Then states we have later */
    LOCK_STATE_OK,
    LOCK_STATE_LOCKED,
    LOCK_STATE_LOCKING,
    LOCK_STATE_UNLOCKED,
    LOCK_STATE_UNLOCKING,
    LOCK_STATE_JAMMED,
} lock_state_t;

/* List of commands in string, much match the enumeration sequence */
static const char* lock_state_strings[] = {
    /* Command from server */
    "LOCK",
    "UNLOCK",
    "OPEN",
    /* States */
    "OK",
    "LOCKED",
    "LOCKING",
    "UNLOCKED",
    "UNLOCKING",
    "JAMMED",
};

/**
 * \brief           Connection information for MQTT CONNECT packet
 */
static const lwesp_mqtt_client_info_t mqtt_client_info = {
    .keep_alive = 10,

    /* Server login data */
    //.user = "8a215f70-a644-11e8-ac49-e932ed599553",
    //.pass = "26aa943f702e5e780f015cd048a91e8fb54cca28",

    /* Device identifier address */
    .id = "123869f5a20-af9c-11e9-b01f-db5cf74e7fb7",
};

static char mqtt_topic_str[1024];  /*!< Topic string */
static char mqtt_topic_data[1024]; /*!< Data string */
static volatile uint8_t mqtt_is_connected, mqtt_connection_trial_failed, immediate_update = 0;

typedef struct user_data {
    /* Switch */
    uint8_t switch_states[4];
    uint8_t switch_states_update[4];
    /* Triac */
    uint8_t triac_states[4];
    uint8_t triac_states_update[4];
    uint8_t triac_brightness[4];
    uint8_t triac_brightness_update[4];
    /* Sensor */
    float ts_data[4];
    uint8_t ts_data_update[4];
    /* RGB */
    uint8_t rgb_states[4];
    uint8_t rgb_states_update[4];
    uint8_t rgb_colors[4][3];
    uint8_t rgb_colors_update[4];
    uint8_t rgb_brightness[4];
    uint8_t rgb_brightness_update[4];
    uint16_t rgb_temp[4];
    uint8_t rgb_temp_update[4];
    char rgb_effect[4][32];
    uint8_t rgb_effect_update[4];
    /* Lock */
    lock_state_t lock_states[4];
    uint8_t lock_states_update[4];
    uint32_t lock_states_last_updated[4];
} user_data_t;

static user_data_t app_io_data;

/**
 * \brief           Parse the number until characters are valid
 * 
 * \param           pptr: Pointer to pointer to string. It will modify the output variable after reading the data
 * \param[in]       ptr: String pointer to parse
 * \param[in]       count: Pointer to an initialized variable where function can modify number of bytes read
 * \param[in]       max_count: Total length of the string in bytes
 * \param[in]       check_comma: If set to `1`, it checks for comma and advances the string one step further
 * \return          Return the parsed number
 */
static uint32_t
prv_parse_number(const char* ptr, uint32_t* count, uint32_t max_count, uint8_t check_comma) {
    uint32_t number = 0;

    if (check_comma && *count < max_count && ptr[*count] == ',') {
        ++(*count);
    }
    for (; *count < max_count && ptr[*count] >= '0' && ptr[*count] <= '9'; ++(*count)) {
        number = 10 * number + (ptr[*count] - '0');
    }
    if (check_comma && *count < max_count && ptr[*count] == ',') {
        ++(*count);
    }
    return number;
}

/**
 * \brief           Custom publish function, a wrapper for \ref lwesp_mqtt_client_publish with print functionatlity
 * 
 * \param           client 
 * \param           topic 
 * \param           payload 
 * \param           payload_len 
 * \param           qos 
 * \param           retain 
 * \param           arg 
 * \return          lwespr_t 
 */
static lwespr_t
lwesp_mqtt_client_publish_custom(lwesp_mqtt_client_p client, const char* topic, const void* payload,
                                 uint16_t payload_len, lwesp_mqtt_qos_t qos, uint8_t retain, void* arg) {
    lwespr_t res = lwesp_mqtt_client_publish(client, topic, payload, payload_len, qos, retain, arg);
    printf("Publishing. Topic: %s, data: %.*s, res: %u\r\n", topic, (int)payload_len, (const char*)payload,
           (unsigned)res);

    return res;
}

/**
 * \brief           MQTT connection event function
 * 
 * \param           client: MQTT client instance
 * \param           evt: Event with data
 */
static void
prv_mqtt_evt_fn(lwesp_mqtt_client_p client, lwesp_mqtt_evt_t* evt) {
    switch (evt->type) {
        case LWESP_MQTT_EVT_CONNECT: {
            if (evt->evt.connect.status == LWESP_MQTT_CONN_STATUS_ACCEPTED) {
                printf("Device accepted\r\n");

                /* Subscribe to necessary topics... */
                RUN_LWESP_API(
                    lwesp_mqtt_client_subscribe(client, "homeassistant/status", LWESP_MQTT_QOS_AT_LEAST_ONCE, NULL));
                RUN_LWESP_API(lwesp_mqtt_client_subscribe(client, DEVICE_OP_TOPIC_PREFIX "/+/+/c/#",
                                                          LWESP_MQTT_QOS_AT_LEAST_ONCE, NULL));
                RUN_LWESP_API(lwesp_mqtt_client_publish_custom(client, "homeassistant/status", "ON", 2,
                                                               LWESP_MQTT_QOS_AT_LEAST_ONCE, 0, (void*)ARG_GROUP_1));
            } else {
                mqtt_connection_trial_failed = 1;
                printf("MQTT device not connected\r\n");
            }
            break;
        }
        case LWESP_MQTT_EVT_DISCONNECT: {
            printf("MQTT disconnect event\r\n");
            mqtt_is_connected = 0;
            RUN_LWESP_API(lwesp_mqtt_client_connect(client, "192.168.1.16", 1883, prv_mqtt_evt_fn, &mqtt_client_info));
            break;
        }
        case LWESP_MQTT_EVT_PUBLISH_RECV: {
            const char* topic_name = (const void*)evt->evt.publish_recv.topic;
            const size_t topic_len = evt->evt.publish_recv.topic_len;
            const char* payload = (const void*)evt->evt.publish_recv.payload;
            const size_t payload_len = evt->evt.publish_recv.payload_len;
            static char command_topic_string[128];
            uint8_t found = 0;

            printf("Publish received. Topic: %.*s, payload: %.*s\r\n", (int)topic_len, topic_name, (int)payload_len,
                   payload);

            /* Check switches */
            if (!found) {
                for (size_t i = 0; i < ASZ(app_io_data.switch_states); ++i) {
                    sprintf(command_topic_string, SWITCH_DESC_TOPIC_COMMAND, (unsigned)(i + 1));
                    if (strncmp(command_topic_string, topic_name, topic_len) == 0) {
                        uint8_t state = strncmp(evt->evt.publish_recv.payload, "ON", 2) == 0;
                        if (state != app_io_data.switch_states[i]) {
                            app_io_data.switch_states[i] = state;
                            app_io_data.switch_states_update[i] = 1;
                        }
                        printf("Switch command: id=%u, value=%u\r\n", (unsigned)(i + 1),
                               (unsigned)app_io_data.switch_states[i]);
                        found = 1;
                        break;
                    }
                }
            }

            /* Check Triacs */
            if (!found) {
                for (size_t i = 0; i < ASZ(app_io_data.triac_states); ++i) {
                    /* Check set command */
                    sprintf(command_topic_string, TRIAC_DESC_TOPIC_COMMAND, (unsigned)(i + 1));
                    if (strncmp(command_topic_string, topic_name, topic_len) == 0) {
                        uint8_t state = strncmp(evt->evt.publish_recv.payload, "ON", 2) == 0;
                        if (state != app_io_data.triac_states[i]) {
                            app_io_data.triac_states[i] = state;
                            app_io_data.triac_states_update[i] = 1;
                        }
                        printf("Triac command: id=%u, value=%u\r\n", (unsigned)(i + 1),
                               (unsigned)app_io_data.triac_states[i]);
                        found = 1;
                        break;
                    }

                    /* Check brightness command */
                    sprintf(command_topic_string, TRIAC_DESC_TOPIC_BRIGHTNESS, (unsigned)(i + 1));
                    if (strncmp(command_topic_string, topic_name, topic_len) == 0) {
                        uint32_t cnt = 0, number;

                        number = prv_parse_number(payload, &cnt, payload_len, 1);
                        if (number != app_io_data.triac_brightness[i]) {
                            app_io_data.triac_brightness[i] = number;
                            app_io_data.triac_brightness_update[i] = 1;
                        }
                        printf("Triac brightness: id=%u, value=%u\r\n", (unsigned)(i + 1),
                               (unsigned)app_io_data.triac_brightness[i]);
                        found = 1;
                        break;
                    }
                }
            }

            /* Check RGB strip */
            if (!found) {
                for (size_t i = 0; i < ASZ(app_io_data.rgb_states); ++i) {
                    /* Check set command */
                    sprintf(command_topic_string, RGBSTRIP_DESC_TOPIC_COMMAND, (unsigned)(i + 1));
                    if (strncmp(command_topic_string, topic_name, topic_len) == 0) {
                        uint8_t state = strncmp(evt->evt.publish_recv.payload, "ON", 2) == 0;
                        if (state != app_io_data.rgb_states[i]) {
                            app_io_data.rgb_states[i] = state;
                            app_io_data.rgb_states_update[i] = 1;
                        }
                        printf("Light command: id=%u, value=%u\r\n", (unsigned)(i + 1),
                               (unsigned)app_io_data.rgb_states[i]);
                        found = 1;
                        break;
                    }

                    /* Check brightness command */
                    sprintf(command_topic_string, RGBSTRIP_DESC_TOPIC_BRIGHTNESS, (unsigned)(i + 1));
                    if (strncmp(command_topic_string, topic_name, topic_len) == 0) {
                        uint32_t cnt = 0, number;

                        number = prv_parse_number(payload, &cnt, payload_len, 1);
                        if (number != app_io_data.rgb_brightness[i]) {
                            app_io_data.rgb_brightness[i] = number;
                            app_io_data.rgb_brightness_update[i] = 1;
                        }
                        printf("Light brightness command: id=%u, value=%u\r\n", (unsigned)(i + 1),
                               (unsigned)app_io_data.rgb_brightness[i]);
                        found = 1;
                        break;
                    }

                    /* Check color temperature command */
                    sprintf(command_topic_string, RGBSTRIP_DESC_TOPIC_COLOR_TEMP, (unsigned)(i + 1));
                    if (strncmp(command_topic_string, topic_name, topic_len) == 0) {
                        uint32_t cnt = 0, number;

                        number = prv_parse_number(payload, &cnt, payload_len, 1);
                        if (number != app_io_data.rgb_temp[i]) {
                            app_io_data.rgb_temp[i] = number;
                            app_io_data.rgb_temp_update[i] = 1;

                            /* Save RGB color now, since it has changed */
                            for (size_t keypair_idx = 0; keypair_idx < ASZ(mired_kelvin_rgb_pairs); ++keypair_idx) {
                                if (mired_kelvin_rgb_pairs[keypair_idx].mired == number) {
                                    app_io_data.rgb_colors[i][0] = mired_kelvin_rgb_pairs[keypair_idx].r;
                                    app_io_data.rgb_colors[i][1] = mired_kelvin_rgb_pairs[keypair_idx].g;
                                    app_io_data.rgb_colors[i][2] = mired_kelvin_rgb_pairs[keypair_idx].b;
                                    printf("Light RGB set from color temp command: id=%u, value=%u,%u,%u\r\n",
                                           (unsigned)(i + 1), (unsigned)app_io_data.rgb_colors[i][0],
                                           (unsigned)app_io_data.rgb_colors[i][1],
                                           (unsigned)app_io_data.rgb_colors[i][2]);
                                    break;
                                }
                            }
                        }
                        printf("Light temperature command: id=%u, value=%u\r\n", (unsigned)(i + 1),
                               (unsigned)app_io_data.rgb_temp[i]);
                        found = 1;
                        break;
                    }

                    /* Check RGB command */
                    sprintf(command_topic_string, RGBSTRIP_DESC_TOPIC_RGB, (unsigned)(i + 1));
                    if (strncmp(command_topic_string, topic_name, topic_len) == 0) {
                        uint32_t cnt = 0, number;

                        /* We parse up to 3 characters */
                        number = prv_parse_number(payload, &cnt, payload_len, 1);
                        if (number != app_io_data.rgb_colors[i][0]) {
                            app_io_data.rgb_colors[i][0] = number;
                            app_io_data.rgb_colors_update[i] = 1;
                        }
                        number = prv_parse_number(payload, &cnt, payload_len, 1);
                        if (number != app_io_data.rgb_colors[i][1]) {
                            app_io_data.rgb_colors[i][1] = number;
                            app_io_data.rgb_colors_update[i] = 1;
                        }
                        number = prv_parse_number(payload, &cnt, payload_len, 1);
                        if (number != app_io_data.rgb_colors[i][2]) {
                            app_io_data.rgb_colors[i][2] = number;
                            app_io_data.rgb_colors_update[i] = 1;
                        }
                        found = 1;
                        printf("Light RGB command: id=%u, value=%u,%u,%u\r\n", (unsigned)(i + 1),
                               (unsigned)app_io_data.rgb_colors[i][0], (unsigned)app_io_data.rgb_colors[i][1],
                               (unsigned)app_io_data.rgb_colors[i][2]);
                        break;
                    }

                    /* Check effect command */
                    sprintf(command_topic_string, RGBSTRIP_DESC_TOPIC_EFFECT, (unsigned)(i + 1));
                    if (strncmp(command_topic_string, topic_name, topic_len) == 0) {
                        if (strncmp(payload, app_io_data.rgb_effect[i], payload_len) != 0) {
                            strncpy(app_io_data.rgb_effect[i], payload, sizeof(app_io_data.rgb_effect[0]));
                            app_io_data.rgb_effect[i][sizeof(app_io_data.rgb_effect[0]) - 1] = '\0';
                            app_io_data.rgb_effect_update[i] = 1;
                        }
                        printf("Light effect command: id=%u, value=%s\r\n", (unsigned)(i + 1),
                               app_io_data.rgb_effect[i]);
                        found = 1;
                        break;
                    }
                }
            }

            /* Check locks */
            if (!found) {
                for (size_t i = 0; i < ASZ(app_io_data.lock_states); ++i) {
                    sprintf(command_topic_string, LOCK_DESC_TOPIC_COMMAND, (unsigned)(i + 1));
                    if (strncmp(command_topic_string, topic_name, topic_len) == 0) {

                        /* Check if string in strings array match, then use the index as a state from enumeration */
                        for (size_t strings_index = 0; strings_index < ASZ(lock_state_strings); ++strings_index) {
                            if (strncmp(payload, lock_state_strings[strings_index], payload_len) == 0) {
                                /* We found a state, check if indexes match now */
                                if ((lock_state_t)strings_index != app_io_data.lock_states[i]) {
                                    app_io_data.lock_states[i] = (lock_state_t)strings_index;
                                    /* We do not update immediately - it will be handled in the processing function */
                                }
                                found = 1;
                                break;
                            }
                        }
                    }
                }
            }

            if (found) {
                immediate_update = 1;
            }
            break;
        }
        case LWESP_MQTT_EVT_SUBSCRIBE: {
            break;
        }
        case LWESP_MQTT_EVT_PUBLISH: {

            if (evt->evt.publish.arg == (void*)ARG_GROUP_1) {
                /* Update sensors */
                for (size_t i = 0; i < ASZ(app_io_data.ts_data); ++i) {
                    uint32_t id = (i + 1);

                    sprintf(mqtt_topic_str, SENSOR_DESC_TOPIC_DISCOVERY_CONFIG, (unsigned)id);
                    sprintf(mqtt_topic_data, SENSOR_DESC_CONFIG, (unsigned)id, (unsigned)id, (unsigned)id,
                            (unsigned)id);
                    RUN_LWESP_API(lwesp_mqtt_client_publish_custom(
                        client, mqtt_topic_str, mqtt_topic_data, strlen(mqtt_topic_data), LWESP_MQTT_QOS_AT_LEAST_ONCE,
                        0, i == 3 ? (void*)ARG_GROUP_2 : NULL));
                }
            } else if (evt->evt.publish.arg == (void*)ARG_GROUP_2) {
                /* Send RGB strips */
                for (size_t i = 0; i < ASZ(app_io_data.rgb_states); ++i) {
                    uint32_t id = (i + 1);

                    sprintf(mqtt_topic_str, RGBSTRIP_DESC_TOPIC_DISCOVERY_CONFIG, (unsigned)id);
                    sprintf(mqtt_topic_data, RGBSTRIP_DESC_CONFIG, (unsigned)id, (unsigned)id, (unsigned)id,
                            (unsigned)id);
                    RUN_LWESP_API(lwesp_mqtt_client_publish_custom(
                        client, mqtt_topic_str, mqtt_topic_data, strlen(mqtt_topic_data), LWESP_MQTT_QOS_AT_LEAST_ONCE,
                        0, i == 3 ? (void*)ARG_GROUP_3 : NULL));
                }
            } else if (evt->evt.publish.arg == (void*)ARG_GROUP_3) {
                /* Send switch entities */
                for (size_t i = 0; i < ASZ(app_io_data.switch_states); ++i) {
                    uint32_t id = (i + 1);

                    sprintf(mqtt_topic_str, SWITCH_DESC_TOPIC_DISCOVERY_CONFIG, (unsigned)id);
                    sprintf(mqtt_topic_data, SWITCH_DESC_CONFIG, (unsigned)id, (unsigned)id, (unsigned)id,
                            (unsigned)id);
                    RUN_LWESP_API(lwesp_mqtt_client_publish_custom(
                        client, mqtt_topic_str, mqtt_topic_data, strlen(mqtt_topic_data), LWESP_MQTT_QOS_AT_LEAST_ONCE,
                        0, i == 3 ? (void*)ARG_GROUP_4 : NULL));
                }
            } else if (evt->evt.publish.arg == (void*)ARG_GROUP_4) {
                /* Send triac entities */
                for (size_t i = 0; i < ASZ(app_io_data.triac_states); ++i) {
                    uint32_t id = (i + 1);

                    sprintf(mqtt_topic_str, TRIAC_DESC_TOPIC_DISCOVERY_CONFIG, (unsigned)id);
                    sprintf(mqtt_topic_data, TRIAC_DESC_CONFIG, (unsigned)id, (unsigned)id, (unsigned)id, (unsigned)id);
                    RUN_LWESP_API(lwesp_mqtt_client_publish_custom(
                        client, mqtt_topic_str, mqtt_topic_data, strlen(mqtt_topic_data), LWESP_MQTT_QOS_AT_LEAST_ONCE,
                        0, i == 3 ? (void*)ARG_GROUP_5 : NULL));
                }
            } else if (evt->evt.publish.arg == (void*)ARG_GROUP_5) {
                /* Send lock entities */
                for (size_t i = 0; i < ASZ(app_io_data.lock_states); ++i) {
                    uint32_t id = (i + 1);

                    sprintf(mqtt_topic_str, LOCK_DESC_TOPIC_DISCOVERY_CONFIG, (unsigned)id);
                    sprintf(mqtt_topic_data, LOCK_DESC_CONFIG, (unsigned)id, (unsigned)id, (unsigned)id, (unsigned)id);
                    RUN_LWESP_API(lwesp_mqtt_client_publish_custom(
                        client, mqtt_topic_str, mqtt_topic_data, strlen(mqtt_topic_data), LWESP_MQTT_QOS_AT_LEAST_ONCE,
                        0, i == 3 ? (void*)ARG_GROUP_6 : NULL));
                }
            } else if (evt->evt.publish.arg == (void*)ARG_GROUP_6) {
                printf("All groups published...\r\n");
                mqtt_is_connected = 1;
            }
            break;
        }
        case LWESP_MQTT_EVT_CONN_POLL: {
            static uint32_t sin_counter = 0;
            static uint32_t poll_counter;

            if ((++poll_counter & 0x07) != 0) {
                break;
            }

#if 1
            /* Publish data temperature */
            for (size_t i = 0; i < ASZ(app_io_data.ts_data); ++i) {
                uint32_t id = i + 1;

                /* Calculate data */
                app_io_data.ts_data[i] = 21.0f + (float)2.0f * sinf(2.0f * 3.14f * (float)++sin_counter * 100.0f);

                sprintf(mqtt_topic_str, SENSOR_DESC_TOPIC_STATE, (unsigned)id);
                sprintf(mqtt_topic_data, "{\"temperature\":%.3f}", app_io_data.ts_data[i]);
                RUN_LWESP_API(lwesp_mqtt_client_publish(client, mqtt_topic_str, mqtt_topic_data,
                                                        strlen(mqtt_topic_data), LWESP_MQTT_QOS_AT_MOST_ONCE, 0, NULL));
            }
#endif
            break;
        }
        default: break;
    }
}

/**
 * \brief           MQTT client API thread
 * \param[in]       arg: User argument
 */
void
lwesp_mqtt_client_api_ha_thread(void const* arg) {
    lwesp_mqtt_client_p client;

    LWESP_UNUSED(arg);

    if ((client = lwesp_mqtt_client_new(4096, 2048)) == NULL) {
        goto terminate;
    }

    while (1) {
        lwespr_t res;

        /* Make a connection */
        printf("Joining MQTT server\r\n");

        /* Connect to client */
        mqtt_is_connected = 0;
    try_again:
        mqtt_connection_trial_failed = 0;

        res = lwesp_mqtt_client_connect(client, "192.168.1.16", 1883, prv_mqtt_evt_fn, &mqtt_client_info);
        if (res == lwespOK) {
            printf("Connecting...\r\n");
            while (!mqtt_is_connected) {
                Sleep(1000);
                if (mqtt_connection_trial_failed) {
                    goto try_again;
                }
            }
        } else {
            printf("Failed to connect: %u. Trying again\r\n", (unsigned)res);
            Sleep(1000);

            goto try_again;
        }
        printf("Mqtt is now connected\r\n");

        /* Initialize default values before we connect -> simulate data already ON */
        for (size_t i = 0; i < ASZ(app_io_data.rgb_states); ++i) {
            app_io_data.rgb_states[i] = i & 1;
            app_io_data.rgb_brightness[i] = 0x07 << i;
            app_io_data.rgb_colors[i][0] = i << 5;
            app_io_data.rgb_colors[i][1] = i << 0;
            app_io_data.rgb_colors[i][2] = i << 2;
            app_io_data.rgb_temp[i] = 1300;
        }
        for (size_t i = 0; i < ASZ(app_io_data.triac_states); ++i) {
            app_io_data.triac_states[i] = i & 1;
            app_io_data.triac_brightness[i] = 0x03 << i;
        }
        for (size_t i = 0; i < ASZ(app_io_data.switch_states); ++i) {
            app_io_data.switch_states[i] = i & 1;
        }

        uint8_t first_time = 1;
        uint32_t time_last_states_update = lwesp_sys_now();
        while (mqtt_is_connected) {
            uint32_t time_now = lwesp_sys_now();

            if (immediate_update || (time_now - time_last_states_update) >= 5000) {
                immediate_update = 0;
                time_last_states_update = time_now;

                /* Publish switch states */
                for (size_t i = 0; i < ASZ(app_io_data.switch_states); ++i) {
                    uint32_t id = i + 1;

                    if (app_io_data.switch_states_update[i] || first_time) {
                        app_io_data.switch_states_update[i] = 0;

                        sprintf(mqtt_topic_str, SWITCH_DESC_TOPIC_STATE, (unsigned)id);
                        sprintf(mqtt_topic_data, "%s", app_io_data.switch_states[i] ? "ON" : "OFF");
                        RUN_LWESP_API(lwesp_mqtt_client_publish_custom(client, mqtt_topic_str, mqtt_topic_data,
                                                                       strlen(mqtt_topic_data),
                                                                       LWESP_MQTT_QOS_AT_LEAST_ONCE, 0, NULL));
                    }
                }

                /* Publish RGB strip states */
                for (size_t i = 0; i < ASZ(app_io_data.rgb_states); ++i) {
                    uint32_t id = i + 1;

                    if (app_io_data.rgb_states_update[i] || first_time) {
                        app_io_data.rgb_states_update[i] = 0;

                        /* Publish state */
                        sprintf(mqtt_topic_str, RGBSTRIP_DESC_TOPIC_STATE, (unsigned)id);
                        sprintf(mqtt_topic_data, "%s", app_io_data.rgb_states[i] ? "ON" : "OFF");
                        RUN_LWESP_API(lwesp_mqtt_client_publish_custom(client, mqtt_topic_str, mqtt_topic_data,
                                                                       strlen(mqtt_topic_data),
                                                                       LWESP_MQTT_QOS_AT_LEAST_ONCE, 0, NULL));
                    }

                    if (app_io_data.rgb_brightness_update[i] || first_time) {
                        app_io_data.rgb_brightness_update[i] = 0;

                        /* Publish brightness */
                        sprintf(mqtt_topic_str, RGBSTRIP_DESC_TOPIC_BRIGHTNESS_STATE, (unsigned)id);
                        sprintf(mqtt_topic_data, "{\"brightness\":%u}", (unsigned)app_io_data.rgb_brightness[i]);
                        RUN_LWESP_API(lwesp_mqtt_client_publish_custom(client, mqtt_topic_str, mqtt_topic_data,
                                                                       strlen(mqtt_topic_data),
                                                                       LWESP_MQTT_QOS_AT_LEAST_ONCE, 0, NULL));
                    }

                    if (app_io_data.rgb_temp_update[i] || first_time) {
                        app_io_data.rgb_temp_update[i] = 0;

                        /* Publish brightness */
                        sprintf(mqtt_topic_str, RGBSTRIP_DESC_TOPIC_COLOR_TEMP_STATE, (unsigned)id);
                        sprintf(mqtt_topic_data, "%u", (unsigned)app_io_data.rgb_temp[i]);
                        RUN_LWESP_API(lwesp_mqtt_client_publish_custom(client, mqtt_topic_str, mqtt_topic_data,
                                                                       strlen(mqtt_topic_data),
                                                                       LWESP_MQTT_QOS_AT_LEAST_ONCE, 0, NULL));
                    }

                    if (app_io_data.rgb_colors_update[i] || first_time) {
                        app_io_data.rgb_colors_update[i] = 0;

                        /* Publish RGB color */
                        sprintf(mqtt_topic_str, RGBSTRIP_DESC_TOPIC_RGB_STATE, (unsigned)id);
                        sprintf(mqtt_topic_data, "%u,%u,%u", (unsigned)app_io_data.rgb_colors[i][0],
                                (unsigned)app_io_data.rgb_colors[i][1], (unsigned)app_io_data.rgb_colors[i][2]);
                        RUN_LWESP_API(lwesp_mqtt_client_publish_custom(client, mqtt_topic_str, mqtt_topic_data,
                                                                       strlen(mqtt_topic_data),
                                                                       LWESP_MQTT_QOS_AT_LEAST_ONCE, 0, NULL));
                    }

                    if (app_io_data.rgb_effect_update[i] || first_time) {
                        app_io_data.rgb_effect_update[i] = 0;

                        /* Publish RGB color */
                        sprintf(mqtt_topic_str, RGBSTRIP_DESC_TOPIC_EFFECT_STATE, (unsigned)id);
                        sprintf(mqtt_topic_data, "%s", app_io_data.rgb_effect[i]);
                        RUN_LWESP_API(lwesp_mqtt_client_publish_custom(client, mqtt_topic_str, mqtt_topic_data,
                                                                       strlen(mqtt_topic_data),
                                                                       LWESP_MQTT_QOS_AT_LEAST_ONCE, 0, NULL));
                    }
                }

                /* Publish triac states */
                for (size_t i = 0; i < ASZ(app_io_data.triac_states); ++i) {
                    uint32_t id = i + 1;

                    if (app_io_data.triac_states_update[i] || first_time) {
                        app_io_data.triac_states_update[i] = 0;

                        sprintf(mqtt_topic_str, TRIAC_DESC_TOPIC_STATE, (unsigned)id);
                        sprintf(mqtt_topic_data, "%s", app_io_data.triac_states[i] ? "ON" : "OFF");
                        RUN_LWESP_API(lwesp_mqtt_client_publish_custom(client, mqtt_topic_str, mqtt_topic_data,
                                                                       strlen(mqtt_topic_data),
                                                                       LWESP_MQTT_QOS_AT_LEAST_ONCE, 0, NULL));
                    }

                    if (app_io_data.triac_brightness_update[i] || first_time) {
                        app_io_data.triac_brightness_update[i] = 0;

                        sprintf(mqtt_topic_str, TRIAC_DESC_TOPIC_BRIGHTNESS_STATE, (unsigned)id);
                        sprintf(mqtt_topic_data, "{\"brightness\":%u}", (unsigned)app_io_data.triac_brightness[i]);
                        RUN_LWESP_API(lwesp_mqtt_client_publish_custom(client, mqtt_topic_str, mqtt_topic_data,
                                                                       strlen(mqtt_topic_data),
                                                                       LWESP_MQTT_QOS_AT_LEAST_ONCE, 0, NULL));
                    }
                }

                /* Publish lock states */
                for (size_t i = 0; i < ASZ(app_io_data.lock_states); ++i) {
                    uint32_t id = i + 1;

                    /* Handle states... */

                    if (app_io_data.lock_states_update[i] || first_time) {
                        app_io_data.lock_states_update[i] = 0;

                        sprintf(mqtt_topic_str, LOCK_DESC_TOPIC_STATE, (unsigned)id);
                        sprintf(mqtt_topic_data, "%s", lock_state_strings[app_io_data.lock_states[i]]);
                        RUN_LWESP_API(lwesp_mqtt_client_publish_custom(client, mqtt_topic_str, mqtt_topic_data,
                                                                       strlen(mqtt_topic_data),
                                                                       LWESP_MQTT_QOS_AT_LEAST_ONCE, 0, NULL));
                    }
                }
            }

            /* Handle lock states here */
            lwesp_sys_protect();
            for (size_t i = 0; i < ASZ(app_io_data.lock_states); ++i) {
                switch (app_io_data.lock_states[i]) {
                    /* Commands we can receive from the cloud */
                    case LOCK_STATE_LOCK: {
                        /* Let's start the locking procedure */
                        app_io_data.lock_states[i] = LOCK_STATE_LOCKING;
                        app_io_data.lock_states_update[i] = 1;
                        app_io_data.lock_states_last_updated[i] = time_now;
                        immediate_update = 1;
                        printf("Locking starting\r\n");
                        break;
                    }
                    case LOCK_STATE_UNLOCK:
                    case LOCK_STATE_OPEN: {
                        /* Let's start the locking procedure */
                        app_io_data.lock_states[i] = LOCK_STATE_UNLOCKING;
                        app_io_data.lock_states_update[i] = 1;
                        app_io_data.lock_states_last_updated[i] = time_now;
                        immediate_update = 1;
                        printf("Unlocking starting\r\n");
                        break;
                    }

                    /* States we can operate in... */
                    case LOCK_STATE_LOCKING: {
                        /* Stay in locking for some time */
                        if (time_now - app_io_data.lock_states_last_updated[i] >= (i + 1) * 1000) {
                            app_io_data.lock_states[i] = LOCK_STATE_LOCKED;
                            app_io_data.lock_states_update[i] = 1;
                            app_io_data.lock_states_last_updated[i] = time_now;
                            immediate_update = 1;
                            printf("Device is now locked\r\n");
                        }
                        break;
                    }
                    case LOCK_STATE_LOCKED: {
                        /* Nothing to do here, wait for a new command to start unlocking or opening... */
                        break;
                    }
                    case LOCK_STATE_UNLOCKING: {
                        /* Stay in locking for some time */
                        if (time_now - app_io_data.lock_states_last_updated[i] >= (i + 1) * 1000) {
                            app_io_data.lock_states[i] = LOCK_STATE_UNLOCKED;
                            app_io_data.lock_states_update[i] = 1;
                            app_io_data.lock_states_last_updated[i] = time_now;
                            immediate_update = 1;
                            printf("Unlocked!\r\n");
                        }
                        break;
                    }
                    case LOCK_STATE_UNLOCKED: {
                        /* Stay in unlocked for some time then go back to locking state */
                        if (time_now - app_io_data.lock_states_last_updated[i] >= (i + 1) * 1000) {
                            app_io_data.lock_states[i] = LOCK_STATE_LOCKING;
                            app_io_data.lock_states_update[i] = 1;
                            app_io_data.lock_states_last_updated[i] = time_now;
                            immediate_update = 1;
                            printf("Locking starting\r\n");
                        }
                        break;
                    }
                    default: {
                        app_io_data.lock_states[i] = LOCK_STATE_LOCK;
                        break;
                    }
                }
            }
            lwesp_sys_unprotect();

            first_time = 0;
            lwesp_delay(50);
        }
        printf("Mqtt is not connected anymore\r\n");
    }

terminate:
    lwesp_mqtt_client_delete(client);
    printf("MQTT client thread terminate\r\n");
    lwesp_sys_thread_terminate(NULL);
}
