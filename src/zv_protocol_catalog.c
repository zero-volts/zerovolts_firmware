#include "zv_protocol_catalog.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "zv_com_protocol.h"
#include "zv_uart.h"

/* EMBED_TXTFILES stores this text in flash, with a terminating NUL. */
extern const char catalog_start[] __asm__("_binary_catalog_json_start");
extern const char catalog_end[] __asm__("_binary_catalog_json_end");

#define HELP_LINE_SIZE 512

static cJSON *load_catalog(void)
{
    return cJSON_ParseWithLengthOpts(catalog_start,
                                     (size_t)(catalog_end - catalog_start), NULL, true);
}

static const cJSON *member(const cJSON *object, const char *key)
{
    return cJSON_GetObjectItemCaseSensitive(object, key);
}

/* Descriptive values must fit a single unescaped UART field. */
static const char *text(const cJSON *value)
{
    if (!cJSON_IsString(value) || strpbrk(value->valuestring, "|\r\n"))
        return NULL;

    return value->valuestring;
}

static bool append(char *line, size_t *used, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int written = vsnprintf(line + *used, HELP_LINE_SIZE - *used, format, args);
    va_end(args);

    if (written < 0 || (size_t)written >= HELP_LINE_SIZE - *used)
        return false;

    *used += (size_t)written;
    return true;
}

static bool render_fields(const cJSON *root, const cJSON *entry, const char *key, const char *token, bool emit)
{
    const cJSON *reference = member(entry, key);
    if (!reference)
        return true;

    const char *set_name = text(reference);
    const cJSON *fields = set_name ? member(member(root, "field_sets"), set_name) : NULL;
    if (!cJSON_IsArray(fields))
        return false;

    const cJSON *field;
    cJSON_ArrayForEach(field, fields)
    {
        const char *name = text(member(field, "name"));
        const char *type = text(member(field, "type"));
        const char *description = text(member(field, "description"));
        const cJSON *attributes = member(field, "attributes");

        char line[HELP_LINE_SIZE];
        size_t used = 0;
        if (!name || !type || !description || !cJSON_IsObject(attributes) ||
            !append(line, &used, "%s|name=%s|type=%s", token, name, type))
            return false;

        const cJSON *attribute;
        cJSON_ArrayForEach(attribute, attributes)
        {
            if (!attribute->string || strpbrk(attribute->string, "|=\r\n"))
                return false;

            bool valid;
            if (cJSON_IsBool(attribute))
            {
                valid = append(line, &used, "|%s=%d", attribute->string, cJSON_IsTrue(attribute));
            }
            else if (cJSON_IsNumber(attribute) && isfinite(attribute->valuedouble) &&
                     floor(attribute->valuedouble) == attribute->valuedouble)
            {
                valid = append(line, &used, "|%s=%.17g", attribute->string, attribute->valuedouble);
            }
            else
            {
                const char *value = text(attribute);
                valid = value && append(line, &used, "|%s=%s", attribute->string, value);
            }

            if (!valid)
                return false;
        }

        if (!append(line, &used, "|description=%s", description))
            return false;

        if (emit)
            zv_uart_send_line(line);

    }

    return true;
}

static bool render_help(const cJSON *root, const cJSON *entry, const char *name, bool is_command, bool emit)
{
    const char *description = text(member(entry, "description"));
    char line[HELP_LINE_SIZE];
    size_t used = 0;
    if (!description || !append(line, &used, "%s|name=%s|description=%s",
                               is_command ? ZV_COM_RES_HELP_COMMAND : ZV_COM_RES_HELP_MESSAGE,
                               name, description))
        return false;

    if (emit)
        zv_uart_send_line(line);

    if (!render_fields(root, entry, is_command ? "params" : "fields",
                       is_command ? ZV_COM_RES_HELP_PARAM : ZV_COM_RES_HELP_FIELD, emit))
        return false;


    if (!is_command)
        return true;

    const cJSON *responses = member(entry, "responses");
    if (!cJSON_IsArray(responses))
        return false;

    const cJSON *response;
    cJSON_ArrayForEach(response, responses)
    {
        const char *response_name = text(member(response, "name"));
        const cJSON *ends = member(response, "ends_operation");

        used = 0;
        if (!response_name || !cJSON_IsBool(ends) ||
            !append(line, &used, "%s|name=%s|ends_operation=%d", ZV_COM_RES_HELP_RESPONSE,
                    response_name, cJSON_IsTrue(ends)))
            return false;

        if (emit)
            zv_uart_send_line(line);
    }

    const cJSON *note = member(entry, "notes");
    if (note)
    {
        used = 0;
        const char *value = text(note);
        if (!value || !append(line, &used, "%s|description=%s", ZV_COM_RES_HELP_NOTE, value))
            return false;

        if (emit && value[0])
            zv_uart_send_line(line);
    }

    return true;
}

void zv_protocol_catalog_info(void)
{
    cJSON *root = load_catalog();
    const char *error = root ? "catalog_invalid" : "catalog_unavailable";
    const char *version = text(member(root, "firmware_version"));
    char line[HELP_LINE_SIZE];
    size_t used = 0;

    bool valid = cJSON_IsObject(root) && version && version[0] &&
                 append(line, &used, "%s|firmware=%s", ZV_COM_RES_INFO_OK, version);

    cJSON_Delete(root);
    if (valid)
        zv_uart_send_line(line);
    else
        zv_uart_send_formatted_line("%s|reason=%s", ZV_COM_RES_INFO_ERROR, error);
}

void zv_protocol_catalog_help(const char *name, bool is_command)
{
    cJSON *root = load_catalog();
    const char *error = NULL;
    const cJSON *entries = member(root, is_command ? "commands" : "messages");
    const cJSON *entry = member(entries, name);
    if (!root)
        error = "catalog_unavailable";
    else if (!cJSON_IsObject(root) || !cJSON_IsObject(entries) || (entry && !cJSON_IsObject(entry)))
        error = "catalog_invalid";
    else if (!entry)
        error = is_command ? "catalog_invalid" : "unknown_message";
    else if (!render_help(root, entry, name, is_command, false)) /* Validate every line before emitting anything, without another JSON tree. */
        error = "catalog_invalid";
    else
        render_help(root, entry, name, is_command, true);

    cJSON_Delete(root);
    if (error)
        zv_uart_send_formatted_line("%s|reason=%s", ZV_COM_RES_HELP_ERROR, error);
    else
        zv_uart_send_line(ZV_COM_RES_HELP_DONE);
}
