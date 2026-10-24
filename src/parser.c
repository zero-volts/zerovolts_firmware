#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "parser.h"

#include <string.h>
#include <stdlib.h>
#include <errno.h>

#define FIELD_TEXT_BUFFER_SIZE 512
#define UINT32_FIELD_TEXT_BUFFER_SIZE 32

bool get_string_field_value(const char *kv_buffer, const char *key, char *out, size_t out_size)
{
    if (!kv_buffer || !key || !out || out_size == 0)
        return false;

    if (key[0] == '\0')
        return false;

    /* Work on a copy so tokenizing does not modify the received command. */
    char copy[FIELD_TEXT_BUFFER_SIZE];
    if (strlen(kv_buffer) >= sizeof(copy))
        return false;

    strcpy(copy, kv_buffer);

    char *matched_value = NULL;
    char *saveptr = NULL;
    char *token = strtok_r(copy, "|", &saveptr);
    while (token != NULL)
    {
        char *equals = strchr(token, '=');
        if (equals != NULL)
        {
            *equals = '\0';
            const char *field_key = token;
            char *field_value = equals + 1;

            if (strcmp(field_key, key) == 0)
            {
                if (matched_value != NULL)
                    return false;

                if (strlen(field_value) >= out_size)
                    return false;

                matched_value = field_value;
            }
        }

        token = strtok_r(NULL, "|", &saveptr);
    }

    if (matched_value == NULL)
        return false;

    /* Copy only after checking the complete command for duplicate keys. */
    strcpy(out, matched_value);
    return true;
}

bool get_uint32_field_value(const char *kv_buffer, const char *key, uint32_t *out)
{
    if (out == NULL)
        return false;

    char field[UINT32_FIELD_TEXT_BUFFER_SIZE];
    if (!get_string_field_value(kv_buffer, key, field, sizeof(field)))
        return false;

    if (field[0] == '\0')
        return false;

    for (const char *cursor = field; *cursor; cursor++)
    {
        if (*cursor < '0' || *cursor > '9')
            return false;
    }

    char *end;
    errno = 0;
    uint64_t number = strtoull(field, &end, 10);
    if (errno == ERANGE)
        return false;

    if (*end != '\0')
        return false;

    if (number > UINT32_MAX)
        return false;

    *out = (uint32_t)number;
    return true;
}
