#include "jsonparser.h"

#include "stdlib.h"

uint8_t jsonparser_parse(JsonParser_t* jsonParser, const char* jsonString, uint16_t numToken) {
    if(jsonParser->is_initialized == 1) {
        jsonparser_free(jsonParser);
        
        jsonParser->is_initialized = 0;
    }

    jsonParser->lwjson_tokens = malloc(sizeof(lwjson_token_t) * numToken);

    if(jsonParser->lwjson_tokens == NULL) return -2;

    jsonParser->is_initialized = 1;

    lwjson_init(&jsonParser->lwjson, jsonParser->lwjson_tokens, numToken);
    
    if(lwjson_parse(&jsonParser->lwjson, jsonString) == lwjsonOK) {
        return 1;
    }

    jsonparser_free(jsonParser);

    jsonParser->is_initialized = 0;

    return -1;
}

uint8_t jsonparser_getfloat(JsonParser_t* jsonParser, const char* jsonKey, float* dst) {
    const lwjson_token_t* t;

    if((t = lwjson_find(&jsonParser->lwjson, jsonKey)) != NULL) {
        *dst = lwjson_get_val_real(t);

        return 1;
    }

    return -1;
}

uint8_t jsonparser_getinteger(JsonParser_t* jsonParser, const char* jsonKey, int32_t* dst) {
    const lwjson_token_t* t;

    if((t = lwjson_find(&jsonParser->lwjson, jsonKey)) != NULL) {
        *dst = lwjson_get_val_int(t);

        return 1;
    }

    return -1;
}

uint8_t jsonparser_getstring(JsonParser_t* jsonParser, const char* jsonKey, char* dst) {
    const lwjson_token_t* t;

    if((t = lwjson_find(&jsonParser->lwjson, jsonKey)) != NULL) {
        memcpy(dst, lwjson_get_val_string(t, NULL), lwjson_get_val_string_length(t));

        return 1;
    }

    return -1;    
}

void jsonparser_free(JsonParser_t* jsonParser) {
    if(jsonParser->is_initialized) {
        free(jsonParser->lwjson_tokens);
        lwjson_free(&jsonParser->lwjson);
    }

    jsonParser->is_initialized = 0;
    
    return;
}