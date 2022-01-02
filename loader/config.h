#pragma once

#include "common/string_view.h"

struct loadable_entry {
    struct string_view name;
    size_t offset_within_config;
};

enum value_type {
    VALUE_NONE,
    VALUE_BOOLEAN,
    VALUE_UNSIGNED,
    VALUE_SIGNED,
    VALUE_STRING,
    VALUE_OBJECT,
};

static inline struct string_view type_as_str(enum value_type t)
{
    switch (t) {
        case VALUE_NONE:
            return SV("None");
        case VALUE_BOOLEAN:
            return SV("Boolean");
        case VALUE_UNSIGNED:
            return SV("Unsigned integer");
        case VALUE_SIGNED:
            return SV("Signed integer");
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
    u16 offset_within_config;

    union {
        bool as_bool;
        u64 as_unsigned;
        i64 as_signed;
        struct string_view as_string;
    };
};

struct key_value {
    struct string_view key;
    struct value val;
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

    union {
        struct value as_value;
        size_t as_offset_to_next_loadable_entry; // 0 -> this is the last entry
    };

    size_t offset_to_next_within_same_scope; // 0 -> this is the last entry
};

struct config_error {
    struct string_view message;
    size_t line;
    size_t offset;
    size_t global_offset;
};

struct config {
    struct config_error last_error;

    // Offset + 1, or 0 if none
    size_t first_loadable_entry_offset;
    size_t last_loadable_entry_offset;

    struct config_entry *buffer;
    size_t capacity;
    size_t size;
};

bool config_parse(struct string_view text, struct config *cfg);
void config_pretty_print_error(const struct config_error *err, struct string_view config_as_view);

bool config_get_global(struct config *cfg, struct string_view key, bool must_be_unique, struct value *val);

bool value_get_child(struct config *cfg, struct value *val, struct string_view key,
                     bool must_be_unique, struct value *out);

bool loadable_entry_get_child(struct config *cfg, struct loadable_entry *entry, struct string_view key,
                              struct value *out, bool must_be_unique);

void value_get_first_child(struct config *cfg, struct value *val, struct key_value *out);
void loadable_entry_get_first_child(struct config *cfg, struct loadable_entry *entry, struct value *out);

bool config_contains_global(struct config *cfg, struct string_view key);
bool config_value_contains_child(struct config *cfg, struct value *val, struct string_view key);
bool loadable_entry_contains_child(struct config *cfg, struct loadable_entry *entry, struct string_view key);

bool config_first_loadable_entry(struct config* cfg, struct loadable_entry *entry);
bool config_next_loadable_entry(struct config *cfg, struct loadable_entry *entry);

bool config_next(struct config *cfg, struct key_value *val);
bool config_next_value_of_key(struct config *cfg, struct string_view key, struct value *val);
bool config_last_value_of_key(struct config *cfg, struct string_view key, struct value *val);
