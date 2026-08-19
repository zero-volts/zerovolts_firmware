#include "parser.h"
#include <string.h>

/*
 * Checks a buffer with key=value fields separated by '|' and copies the value
 * for `key` into `out`. The input buffer is copied internally so callers can
 * safely keep using it after parsing.
 */
bool get_field_value(const char *kv_buffer, const char *key, char *out, size_t out_size)
{
    if (!kv_buffer || !key || !out || out_size == 0)
        return false;

    char copy[512];
    snprintf(copy, sizeof(copy), "%s", kv_buffer);

    char *saveptr;
    char *token = strtok_r(copy, "|", &saveptr);
    while (token != NULL)
    {
        char *eq = strchr(token, '=');
        if (eq)
        {
            *eq = '\0';
            const char *k = token;
            const char *v = eq + 1;
            if (strcmp(k, key) == 0)
            {
                snprintf(out, out_size, "%s", v);
                return true;
            }
        }

        token = strtok_r(NULL, "|", &saveptr);
    }

    return false;
}