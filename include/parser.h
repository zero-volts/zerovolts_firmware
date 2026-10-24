#ifndef PARSER_H
#define PARSER_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Reads one key=value field separated by '|', without changing the input.
 * Returns false for longer input, missing/duplicate keys, invalid arguments or insufficient output space.
 * On failure, out is unchanged. Empty values are allowed. */
bool get_string_field_value(const char *kv_buffer, const char *key, char *out, size_t out_size);

/* Reads an unsigned decimal integer in the range 0..UINT32_MAX.
 * Rejects signs, whitespace and nonnumeric characters. On failure, out is unchanged. */
bool get_uint32_field_value(const char *kv_buffer, const char *key, uint32_t *out);

#ifdef __cplusplus
}
#endif

#endif /* PARSER_H */
