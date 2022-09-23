#pragma once

#include "common/string_view.h"
#include "common/dynamic_buffer.h"

struct loadable_entry {
    struct string_view name;
    size_t cfg_off;
};

enum value_type {
    VALUE_NONE     = 1 << 0,
    VALUE_BOOLEAN  = 1 << 1,
    VALUE_UNSIGNED = 1 << 2,
    VALUE_SIGNED   = 1 << 3,
    VALUE_STRING   = 1 << 4,
    VALUE_OBJECT   = 1 << 5,
    VALUE_ANY      = 0xFF
};

static inline struct string_view value_type_as_str(enum value_type t)
{
    switch (t) {
    case VALUE_NONE:
        return SV("None");
    case VALUE_BOOLEAN:
        return SV("Boolean");
    case VALUE_UNSIGNED:
        return SV("Unsigned Integer");
    case VALUE_SIGNED:
        return SV("Signed Integer");
    case VALUE_STRING:
        return SV("String");
    case VALUE_OBJECT:
        return SV("Object");
    default:
        return SV("<Invalid>");
    }
}

struct value {
    u16 type;
    u16 cfg_off;

    union {
        bool as_bool;
        u64 as_unsigned;
        i64 as_signed;
        struct string_view as_string;
    };
};

static inline bool value_is_null(struct value *val)
{
    return val->type == VALUE_NONE;
}

static inline bool value_is_bool(struct value *val)
{
    return val->type == VALUE_BOOLEAN;
}

static inline bool value_is_unsigned(struct value *val)
{
    return val->type == VALUE_UNSIGNED;
}

static inline bool value_is_signed(struct value *val)
{
    return val->type == VALUE_SIGNED;
}

static inline bool value_is_string(struct value *val)
{
    return val->type == VALUE_STRING;
}

static inline bool value_is_object(struct value *val)
{
    return val->type == VALUE_OBJECT;
}

enum config_entry_type {
    CONFIG_ENTRY_NONE,
    CONFIG_ENTRY_VALUE,
    CONFIG_ENTRY_LOADABLE_ENTRY
};

struct config_entry {
    struct string_view key;
    enum config_entry_type t;
    struct value as_value;

    size_t next; // 0 -> this is the last entry
};

struct config_error {
    struct string_view message;
    size_t line;
    size_t column;
    size_t line_start_pos;
};

struct config_source {
    char *text;
    size_t size;
};

struct config {
    struct config_source src;
    struct config_error last_error;

    // Offset + 1, or 0 if none
    size_t first_loadable_entry_offset;
    size_t last_loadable_entry_offset;

    struct dynamic_buffer entries_buf;
};

static inline bool cfg_empty(struct config *cfg)
{
    return cfg->entries_buf.size == 0;
}

bool cfg_parse(struct config_source src, struct config *cfg);
void cfg_pretty_print_error(const struct config *cfg);

void cfg_release(struct config *cfg);

bool cfg_get_loadable_entry(struct config *cfg, struct string_view key, struct loadable_entry *val);
bool cfg_first_loadable_entry(struct config *cfg, struct loadable_entry *entry);

bool _cfg_get_bool(struct config *cfg, size_t offset, bool must_be_unique, struct string_view key, bool *val);
bool _cfg_get_unsigned(struct config *cfg, size_t offset, bool must_be_unique, struct string_view key, u64 *val);
bool _cfg_get_signed(struct config *cfg, size_t offset, bool must_be_unique, struct string_view key, i64 *val);
bool _cfg_get_string(struct config *cfg, size_t offset, bool must_be_unique,
                     struct string_view key, struct string_view *val);
bool _cfg_get_object(struct config *cfg, size_t offset, bool must_be_unique, struct string_view key, struct value *val);
bool _cfg_get_value(struct config *cfg, size_t offset, bool must_be_unique, struct string_view key, struct value *val);
bool _cfg_get_one_of(struct config *cfg, size_t offset, bool must_be_unique,
                     struct string_view key, enum value_type mask, struct value *val);

bool cfg_get_next(struct config *cfg, struct value *val, bool oops_on_non_matching_type);
bool cfg_get_next_one_of(struct config *cfg, enum value_type mask, struct value *val, bool oops_on_non_matching_type);

#define cfg_get_bool(cfg, obj, key, out_ptr) _cfg_get_bool(cfg, (obj)->cfg_off, true, key, out_ptr)
#define cfg_get_signed(cfg, obj, key, out_ptr) _cfg_get_signed(cfg, (obj)->cfg_off, true, key, out_ptr)
#define cfg_get_unsigned(cfg, obj, key, out_ptr) _cfg_get_unsigned(cfg, (obj)->cfg_off, true, key, out_ptr)
#define cfg_get_string(cfg, obj, key, out_ptr) _cfg_get_string(cfg, (obj)->cfg_off, true, key, out_ptr)
#define cfg_get_object(cfg, obj, key, out_ptr) _cfg_get_object(cfg, (obj)->cfg_off, true, key, out_ptr)
#define cfg_get_one_of(cfg, obj, key, type_mask, out_ptr) _cfg_get_one_of(cfg, (obj)->cfg_off, true, key, (type_mask), out_ptr)

#define cfg_get_first_bool(cfg, obj, key, out_ptr) _cfg_get_bool(cfg, (obj)->cfg_off, false, key, out_ptr)
#define cfg_get_first_signed(cfg, obj, key, out_ptr) _cfg_get_signed(cfg, (obj)->cfg_off, false, key, out_ptr)
#define cfg_get_first_unsigned(cfg, obj, key, out_ptr) _cfg_get_unsigned(cfg, (obj)->cfg_off, false, key, out_ptr)
#define cfg_get_first_string(cfg, obj, key, out_ptr) _cfg_get_string(cfg, (obj)->cfg_off, false, key, out_ptr)
#define cfg_get_first_object(cfg, obj, key, out_ptr) _cfg_get_object(cfg, (obj)->cfg_off, false, key, out_ptr)
#define cfg_get_first_one_of(cfg, obj, key, type_mask, out_ptr) _cfg_get_one_of(cfg, (obj)->cfg_off, false, key, (type_mask), out_ptr)

#define cfg_get_global_bool(cfg, key, out_ptr) _cfg_get_bool(cfg, -1, true, key, out_ptr)
#define cfg_get_global_signed(cfg, key, out_ptr) _cfg_get_unsigned(cfg, -1, true, key, out_ptr)
#define cfg_get_global_unsigned(cfg, key, out_ptr) _cfg_get_signed(cfg, -1, true, key, out_ptr)
#define cfg_get_global_string(cfg, key, out_ptr) _cfg_get_string(cfg, -1, true, key, out_ptr)
#define cfg_get_global_object(cfg, key, out_ptr) _cfg_get_object(cfg, -1, true, key, out_ptr)

NORETURN
static inline void cfg_oops_no_mandatory_key(struct string_view key)
{
    oops("couldn't find mandatory key \"%pSV\" in the config file!\n", &key);
}

#define CFG_MANDATORY_GET(type, cfg, obj, key, out_ptr)                              \
    do {                                                                             \
        if (!_cfg_get_##type(cfg, (obj)->cfg_off, true, (key), out_ptr))             \
            cfg_oops_no_mandatory_key((key));                                        \
    } while (0)

#define CFG_MANDATORY_GET_ONE_OF(type_mask, cfg, obj, key, out_ptr)                     \
    do {                                                                                \
        if (!_cfg_get_one_of(cfg, (obj)->cfg_off, true, (key), (type_mask), out_ptr))   \
            cfg_oops_no_mandatory_key((key));                                           \
    } while (0)
