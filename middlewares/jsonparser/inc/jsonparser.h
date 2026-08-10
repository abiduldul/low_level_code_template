#ifndef __JSON_PARSER_H__
#define __JSON_PARSER_H__

#include <stdint.h>
#include "lwjson/lwjson.h"

typedef struct{
    lwjson_t lwjson;
    lwjson_token_t* lwjson_tokens;
    uint8_t is_initialized;
} JsonParser_t;

uint8_t jsonparser_parse(JsonParser_t* jsonParser, const char* jsonString, uint16_t numToken);
uint8_t jsonparser_getinteger(JsonParser_t* jsonParser, const char* jsonKey, int32_t* dst);
uint8_t jsonparser_getstring(JsonParser_t* jsonParser, const char* jsonKey, char* dst);
void jsonparser_free(JsonParser_t* jsonParser);
#endif