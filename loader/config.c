#include "config.h"
#include "allocator.h"
#include "common/conversions.h"
#include "common/constants.h"
#include "common/minmax.h"
#include "common/log.h"
#include "common/format.h"

#define PARSE_ERROR(msg)                                  \
    do {                                                  \
        cfg->last_error.message = SV(msg);                \
        cfg->last_error.line = s->file_line;              \
        cfg->last_error.offset = s->line_offset;          \
        cfg->last_error.global_offset = s->global_offset; \
        return false;                                     \
    } while (0)

enum state {
    STATE_NORMAL,
    STATE_KEY,
    STATE_VALUE,
    STATE_LOADABLE_ENTRY_TITLE,
    STATE_COMMENT
};
#define DEPTH_CAPACITY 255

enum base_depth {
    BASE_DEPTH_UNKNOWN,
    BASE_DEPTH_ZERO,
    BASE_DEPTH_NON_ZERO
};
static inline bool base_depth_zero(enum base_depth bd)
{
    return bd == BASE_DEPTH_UNKNOWN || bd == BASE_DEPTH_ZERO;
}

struct parse_state {
    size_t file_line;
    size_t line_offset;
    size_t global_offset;

    enum state s;

    // Character that is picked as whitespace in the current configuration.
    // One of '\t' or ' '. Value of 0 means we don't know yet.
    char whitespace_character;

    // Current depth in picked whitespace characters->
    size_t current_whitespace_depth;

    // The number of characters we treat as one indentation level.
    // 0 means we don't know yet.
    size_t characters_per_level;

    // Set in case we have encountered whitespace in the current value string,
    // e.g "key=val e" in this example, the 'e' after whitespace is considered invalid.
    bool expecting_end_of_value;

    // Set for KEY/VALUE in case at least one character has been consumed
    bool consumed_at_least_one;

    // set for loadable entries where first key starts at an offset
    enum base_depth bd;

    // Character used by the current value for quoting, either ' or ".
    // 0 means none.
    char open_quote_character;

    struct string_view current_value_view;
    struct config_entry current;

    bool within_loadable_entry;
    bool expecting_depth_plus_one;

    // Empty loadable entries are not allowed
    bool consumed_at_least_one_kv;

    size_t current_depth;

    // Depth -> (offset + 1) pairs, used to link together values of the same scope.
    // 0 means none
    u32 depth_to_offset[DEPTH_CAPACITY];
};

#define IS(expected_state) (s->s == expected_state)

static void set_state(struct parse_state *s, enum state new_state)
{
    size_t i;

    switch (new_state)
    {
        case STATE_NORMAL:
            if (IS(STATE_LOADABLE_ENTRY_TITLE)) {
                s->within_loadable_entry = true;

                for (i = 1; i <= s->current_depth + !base_depth_zero(s->bd); ++i)
                    s->depth_to_offset[i] = 0;
                s->current_depth = 0;
            }
            s->expecting_end_of_value = false;
            s->consumed_at_least_one = false;
            s->open_quote_character = 0;

            break;
        case STATE_KEY:
            s->consumed_at_least_one = false;
            s->expecting_depth_plus_one = true;
            break;
        case STATE_VALUE:
            s->expecting_depth_plus_one = false;
            s->consumed_at_least_one = false;
            s->expecting_end_of_value = false;
            s->open_quote_character = 0;
            break;
        case STATE_LOADABLE_ENTRY_TITLE:
            s->consumed_at_least_one = false;
            s->consumed_at_least_one_kv = false;
            break;
        default:
            break;
    }

    s->s = new_state;
}

static bool try_parse_as_number(struct string_view str, struct value *val)
{
    if (sv_starts_with(str, SV("-"))) {
        i64 value;
        if (!str_to_i64(str, &value))
            return false;

        *val = (struct value) {
            .type = VALUE_SIGNED,
            .as_signed = value
        };
    } else {
        u64 uvalue;
        if (!str_to_u64(str, &uvalue))
            return false;

        *val = (struct value) {
            .type = VALUE_UNSIGNED,
            .as_unsigned = uvalue
        };
    }

    return true;
}

static void value_from_state(struct parse_state *s, struct value *val)
{
    int as_bool = 0;

    // Value is stored inside "" or '', force a string type.
    if (s->open_quote_character) {
        *val = (struct value) {
            .type = VALUE_STRING,
            .as_string = s->current_value_view
        };
        return;
    }

    if (sv_equals(s->current_value_view, SV("null"))) {
        *val = (struct value) {
            .type = VALUE_NONE
        };
        return;
    }

    as_bool = sv_equals(s->current_value_view, SV("true"))  ? 1 : as_bool;
    as_bool = sv_equals(s->current_value_view, SV("false")) ? 2 : as_bool;

    if (as_bool) {
        *val = (struct value) {
            .type = VALUE_BOOLEAN,
            .as_bool = as_bool != 2
        };
        return;
    }

    if (try_parse_as_number(s->current_value_view, val))
        return;

    // Nothing else worked, assume string.
    *val = (struct value) {
        .type = VALUE_STRING,
        .as_string = s->current_value_view
    };
}

bool config_emplace_entry(struct config *cfg, struct config_entry *entry, size_t *offset)
{
    if (cfg->size == cfg->capacity) {
        size_t old_capacity = cfg->capacity;
        struct config_entry *new_buffer;

        cfg->capacity = MAX(old_capacity * 2, PAGE_SIZE / sizeof(struct config_entry));
        new_buffer = allocate_bytes(sizeof(struct config_entry) * cfg->capacity);

        if (!new_buffer)
            return false;

        if (cfg->size)
            memcpy(new_buffer, cfg->buffer, sizeof(struct config_entry) * cfg->size);
        free_bytes(cfg->buffer, old_capacity * sizeof(struct config_entry));
        cfg->buffer = new_buffer;
    }

    *offset = cfg->size++;

    if (entry->t == CONFIG_ENTRY_VALUE)
        entry->as_value.cfg_off = *offset;

    cfg->buffer[*offset] = *entry;
    return true;
}

static bool finalize_key_value(struct config *cfg, struct parse_state *s, bool is_object)
{
    size_t entry_offset;
    size_t depth = s->current_depth;
    s->current.t = CONFIG_ENTRY_VALUE;

    if (is_object) {
        s->current.as_value = (struct value) {
            .type = VALUE_OBJECT,
            .cfg_off = 0
        };
    } else {
        value_from_state(s, &s->current.as_value);
    }

    if (!config_emplace_entry(cfg, &s->current, &entry_offset))
        PARSE_ERROR("out of memory");

    depth += s->within_loadable_entry;
    depth -= base_depth_zero(s->bd);

    if (s->depth_to_offset[depth]) {
        struct config_entry *ce = &cfg->buffer[s->depth_to_offset[depth] - 1];
        ce->offset_to_next_within_same_scope = entry_offset - (s->depth_to_offset[depth] - 1);
    }

    s->depth_to_offset[depth] = entry_offset + 1;
    s->consumed_at_least_one_kv = true;

    return true;
}

static bool do_depth_transition(struct parse_state *s)
{
    bool base_is_nonzero, must_be_zero;
    size_t next_depth;

    if (!s->characters_per_level)
        return true;

    // Unaligned to whitespace per level
    if (s->current_whitespace_depth % s->characters_per_level)
        return false;

    base_is_nonzero = base_depth_zero(s->bd) && s->within_loadable_entry;
    next_depth = s->current_whitespace_depth / s->characters_per_level;
    must_be_zero = !(s->expecting_depth_plus_one || s->current_depth || base_is_nonzero);

    // Expected zero but got something else
    if (must_be_zero && next_depth)
        return false;

    // Went too deep
    if (next_depth > s->current_depth && ((next_depth - s->current_depth) > 1))
        return false;

    // Empty object
    if (s->expecting_depth_plus_one && (next_depth != (s->current_depth + 1)))
        return false;

    /*
     * If our depth is now less than what it was before close
     * all nested objects that are still open.
     */
    while (s->current_depth > next_depth)
        s->depth_to_offset[s->current_depth-- + !base_is_nonzero] = 0;

    s->current_depth = next_depth;
    return true;
}

static void consume_character(struct parse_state *s, const char* c)
{
    if (s->consumed_at_least_one)
        sv_extend_by(&s->current_value_view, 1);
    else
        s->current_value_view = (struct string_view) { c, 1 };

    s->consumed_at_least_one = true;
}

bool cfg_parse(struct string_view text, struct config *cfg)
{
    size_t i;
    struct parse_state *s = allocate_bytes(sizeof(struct parse_state));
    if (!s) {
        cfg->last_error.message = SV("out of memory");
        return false;
    }
    memzero(s, sizeof(*s));
    s->file_line = 1;
    s->line_offset = 1;

    for (i = 0; i < text.size; ++i) {
        char c = text.text[i];
        s->line_offset++;
        s->global_offset++;

        if (IS(STATE_COMMENT) && c != '\n')
            continue;

        switch (c) {
        case ' ':
        case '\t':
            if (IS(STATE_NORMAL)) {
                if (s->whitespace_character && (s->whitespace_character != c))
                    PARSE_ERROR("mixed tabs and spaces are ambiguous");

                s->whitespace_character = c;
                s->current_whitespace_depth++;
                continue;
            }

            if (IS(STATE_KEY)) {
                s->expecting_end_of_value = s->consumed_at_least_one;
                continue;
            }

            if (IS(STATE_VALUE)) {
                if (!s->open_quote_character) {
                    s->expecting_end_of_value = s->consumed_at_least_one;
                    continue;
                }

                consume_character(s, &text.text[i]);
                continue;
            }

            if (s->expecting_end_of_value)
                continue;

            PARSE_ERROR("invalid character");
        case '\r':
            if (IS(STATE_NORMAL) || IS(STATE_VALUE))
                continue;

            PARSE_ERROR("invalid character");

        case '\n':
            s->file_line++;
            s->line_offset = 0;

            if (s->characters_per_level == 0)
                s->whitespace_character = 0;

            s->current_whitespace_depth = 0;
            s->expecting_end_of_value = false;

            if (IS(STATE_NORMAL))
                continue;

            if (IS(STATE_VALUE)) {
                if (!finalize_key_value(cfg, s, false))
                    return false;

                set_state(s, STATE_NORMAL);
                continue;
            }

            if (IS(STATE_COMMENT)) {
                set_state(s, STATE_NORMAL);
                continue;
            }

            PARSE_ERROR("invalid character");

        case '=':
            if (IS(STATE_NORMAL) || (IS(STATE_VALUE) && !s->open_quote_character))
                PARSE_ERROR("invalid character");

            if (IS(STATE_KEY)) {
                set_state(s, STATE_VALUE);
                continue;
            }

            sv_extend_by(&s->current_value_view, 1);
            continue;

        case ':':
            if (IS(STATE_NORMAL))
                PARSE_ERROR("invalid character");

            if (IS(STATE_KEY)) {
                if (!finalize_key_value(cfg, s, true))
                    return false;

                set_state(s, STATE_NORMAL);
                s->expecting_end_of_value = true;
                continue;
            }

            if (IS(STATE_VALUE) && !s->open_quote_character)
                PARSE_ERROR("invalid character");

            sv_extend_by(&s->current_value_view, 1);
            continue;

        case '"':
        case '\'':
            if (!IS(STATE_VALUE) || (!s->open_quote_character && s->consumed_at_least_one))
                PARSE_ERROR("invalid character");

            if (s->open_quote_character) {
                if (s->open_quote_character != c) {
                    consume_character(s, &text.text[i]);
                    continue;
                }

                if (!finalize_key_value(cfg, s, false))
                    return false;

                set_state(s, STATE_NORMAL);
                s->expecting_end_of_value = true;
                continue;
            }

            s->open_quote_character = c;
            continue;

        case '[':
            if (s->current_whitespace_depth)
                PARSE_ERROR("loadable entry title must start on a new line");

            if (IS(STATE_NORMAL)) {
                if (s->expecting_depth_plus_one)
                    PARSE_ERROR("empty objects are not allowed");

                if (s->within_loadable_entry && !s->consumed_at_least_one_kv)
                    PARSE_ERROR("empty loadable entries are not allowed");

                set_state(s, STATE_LOADABLE_ENTRY_TITLE);
                continue;
            }

            if (IS(STATE_VALUE) && s->open_quote_character) {
                sv_extend_by(&s->current_value_view, 1);
                continue;
            }

            PARSE_ERROR("invalid character");
        case ']':
            if (IS(STATE_LOADABLE_ENTRY_TITLE)) {
                size_t prev_offset, offset;
                s->current.t = CONFIG_ENTRY_LOADABLE_ENTRY;
                s->current.key = s->current_value_view;
                s->current.as_offset_to_next_loadable_entry = 0;

                if (!config_emplace_entry(cfg, &s->current, &offset))
                    PARSE_ERROR("out of memory");

                if (!cfg->first_loadable_entry_offset)
                    cfg->first_loadable_entry_offset = offset + 1;

                if (cfg->last_loadable_entry_offset) {
                    size_t prev = cfg->last_loadable_entry_offset - 1;
                    struct config_entry *entry = &cfg->buffer[prev];
                    entry->as_offset_to_next_loadable_entry = offset - prev;
                }

                prev_offset = s->depth_to_offset[0];
                if (prev_offset) {
                    struct config_entry *entry = &cfg->buffer[prev_offset - 1];
                    entry->offset_to_next_within_same_scope = offset - (prev_offset - 1);
                }
                s->depth_to_offset[0] = offset;

                cfg->last_loadable_entry_offset = offset + 1;
                set_state(s, STATE_NORMAL);
                s->expecting_end_of_value = true;
                continue;
            }

            if (IS(STATE_VALUE) && s->open_quote_character) {
                sv_extend_by(&s->current_value_view, 1);
                continue;
            }

            PARSE_ERROR("invalid character");

        case '#':
            if (IS(STATE_KEY) || IS(STATE_LOADABLE_ENTRY_TITLE))
                PARSE_ERROR("invalid character");

            if (IS(STATE_VALUE) && s->open_quote_character) {
                sv_extend_by(&s->current_value_view, 1);
                continue;
            }

            s->expecting_end_of_value = false;
            set_state(s, STATE_COMMENT);
            continue;

        default:
            if (c <= 32 || c >= 127)
                PARSE_ERROR("invalid character");

            if (s->expecting_end_of_value)
                PARSE_ERROR("unexpected character");

            if (IS(STATE_NORMAL)) {
                if (s->current_whitespace_depth && !s->characters_per_level)
                    s->characters_per_level = s->current_whitespace_depth;

                if (s->bd == BASE_DEPTH_UNKNOWN && s->within_loadable_entry)
                    s->bd = (s->current_whitespace_depth != 0) ? BASE_DEPTH_NON_ZERO : BASE_DEPTH_ZERO;

                if (!do_depth_transition(s))
                    PARSE_ERROR("invalid number of whitespace");

                set_state(s, STATE_KEY);
                s->current.key = (struct string_view) { &text.text[i], 1 };
                s->consumed_at_least_one = true;

                continue;
            }

            if (IS(STATE_KEY)) {
                sv_extend_by(&s->current.key, 1);
                continue;
            }

            if (s->expecting_end_of_value)
                PARSE_ERROR("invalid character");

            if (IS(STATE_VALUE) || IS(STATE_LOADABLE_ENTRY_TITLE)) {
                consume_character(s, &text.text[i]);
                continue;
            }

            PARSE_ERROR("invalid character");
        }
    }

    if (IS(STATE_VALUE))
        return finalize_key_value(cfg, s, false);

    if (s->expecting_depth_plus_one || (s->within_loadable_entry && !s->consumed_at_least_one_kv))
        PARSE_ERROR("early EOF");

    if (IS(STATE_COMMENT))
        return true;

    if (!IS(STATE_NORMAL))
        PARSE_ERROR("early EOF");

    return true;
}

#define LINE_DELIMETER " | "

void cfg_pretty_print_error(const struct config_error *err, struct string_view config_as_view)
{
    size_t i, first_char_of_line, len;
    ssize_t newline_loc;
    char line_as_string[32];

    if (sv_empty(err->message))
        return;

    print_err("Failed to parse config, error at line %zu\n", err->line);

    first_char_of_line = err->global_offset - err->offset;
    sv_offset_by(&config_as_view, first_char_of_line);
    newline_loc = sv_find(config_as_view, SV("\n"), 0);
    if (newline_loc >= 0)
        config_as_view = (struct string_view) { config_as_view.text, newline_loc };


    len = snprintf(line_as_string, sizeof(line_as_string), "%zu", err->line);

    print_err("%s%s%pSV\n", line_as_string, LINE_DELIMETER, &config_as_view);

    for (i = 0; i < len; ++i)
        print_err(" ");

    print_err(LINE_DELIMETER);

    for (i = 1; i < err->offset; ++i)
        print_err(" ");

    print_err("^--- %pSV\n", &err->message);
}

struct find_result {
    size_t first_occurence;
    size_t last_occurence;
    size_t count;
};

static void config_find(struct config *cfg, size_t offset, struct string_view key,
                        size_t constraint_max, struct find_result *res)
{
    BUG_ON(offset >= cfg->size);
    memzero(res, sizeof(*res));

    for (;;) {
        struct config_entry *entry = &cfg->buffer[offset];

        if (entry->t != CONFIG_ENTRY_VALUE)
            break;

        if (sv_equals(entry->key, key)) {
            res->last_occurence = offset;

            if (!res->count)
                res->first_occurence = offset;

            res->count++;

            if (constraint_max && res->count == constraint_max)
                break;
        }

        if (entry->offset_to_next_within_same_scope == 0)
            break;

        offset += entry->offset_to_next_within_same_scope;
    }
}

static void cfg_get_typed_entry_at_offset(struct config *cfg, enum config_entry_type expected_type,
                                          size_t offset, struct config_entry **out)
{
    BUG_ON(offset >= cfg->size);
    *out = &cfg->buffer[offset];
    BUG_ON((*out)->t != expected_type);
}

static void cfg_get_value_at_offset(struct config *cfg, size_t offset, struct config_entry **out)
{
    cfg_get_typed_entry_at_offset(cfg, CONFIG_ENTRY_VALUE, offset, out);
}

static void cfg_get_loadable_entry_at_offset(struct config *cfg, size_t offset, struct config_entry **out)
{
    cfg_get_typed_entry_at_offset(cfg, CONFIG_ENTRY_LOADABLE_ENTRY, offset, out);
}

bool cfg_get_loadable_entry(struct config *cfg, struct string_view key, struct loadable_entry *val)
{
    size_t offset = cfg->first_loadable_entry_offset;
    if (!offset--)
        return false;

    for (;;) {
        struct config_entry *e;
        cfg_get_loadable_entry_at_offset(cfg, offset, &e);

        if (sv_equals(e->key, key)) {
            val->cfg_off = offset;
            val->name = e->key;
            return true;
        }

        if (e->as_offset_to_next_loadable_entry == 0)
            return false;

        offset += e->as_offset_to_next_loadable_entry;
    }
}

bool cfg_first_loadable_entry(struct config* cfg, struct loadable_entry *entry)
{
    //print("first le %zu\n", cfg->first_loadable_entry_offset);
    //print("cfg dump\n");
    //for (size_t i = 0; i < cfg->size; ++i)
    //    print("off: %zu key: %pSV type: %d", i, &cfg->buffer[i].key, cfg->buffer[i].t);

    if (cfg->first_loadable_entry_offset == 0)
        return false;

    entry->cfg_off = cfg->first_loadable_entry_offset - 1;
    entry->name = cfg->buffer[entry->cfg_off].key;
    return true;
}

static bool is_type_set(enum value_type value, enum value_type type)
{
    return (value & type) == type;
}

static bool is_one_of_types(enum value_type mask, enum value_type type)
{
    return is_type_set(mask, VALUE_ANY) || is_type_set(mask, type);
}

NORETURN
static void oops_on_non_unique_key(struct string_view key)
{
    oops("expected key %pSV to be unique!", &key);
}

NORETURN
static void oops_on_unexpected_type(struct string_view key, enum value_type expected_mask, enum value_type got)
{
    struct string_view tstr_expected, tstr_got = value_type_as_str(got);

    size_t types = __builtin_popcount(expected_mask);
    size_t bit;

    if (types == 1) {
        tstr_expected = value_type_as_str(expected_mask);

        oops("unexpected type for \"%pSV\"! expected: %pSV, got: %pSV\n",
             &key, &tstr_expected, &tstr_got);
    }

    print_err("Oops!\nunexpected type for \"%pSV\"! expected one of: [", &key);

    for (bit = 1; bit < VALUE_ANY; bit <<= 1) {
        if (!is_type_set(expected_mask, bit))
            continue;

        tstr_expected = value_type_as_str(bit);
        print_err("%pSV", &tstr_expected);
        if (--types == 0)
            break;

        print_err(", ");
    }

    print_err("] got: %pSV\n", &tstr_got);
    for (;;);
}

static bool cfg_get_next_entry(struct config *cfg, size_t entry_offset, struct config_entry *this_entry,
                               struct config_entry **out_entry, struct string_view *key)
{
    for (;;) {
        if (this_entry->offset_to_next_within_same_scope == 0)
            return false;

        entry_offset += this_entry->offset_to_next_within_same_scope;
        this_entry = &cfg->buffer[entry_offset];

        if (key && !sv_equals(this_entry->key, *key))
            continue;

        *out_entry = this_entry;
        return true;
    }
}

bool cfg_get_next(struct config *cfg, struct value *val, bool oops_on_non_matching_type)
{
    struct config_entry *entry;
    enum value_type expected_type;

    cfg_get_value_at_offset(cfg, val->cfg_off, &entry);
    expected_type = entry->as_value.type;

    for (;;) {
        if (!cfg_get_next_entry(cfg, val->cfg_off, entry, &entry, &entry->key))
            return false;

        if (expected_type != entry->as_value.type) {
            if (oops_on_non_matching_type)
                oops_on_unexpected_type(entry->key, expected_type, entry->as_value.type);
            continue;
        }
    }

    *val = entry->as_value;
    return true;
}

bool cfg_get_next_one_of(struct config *cfg, enum value_type mask, struct value *val, bool oops_on_non_matching_type)
{
    struct config_entry *entry;
    cfg_get_value_at_offset(cfg, val->cfg_off, &entry);

    for (;;) {
        if (!cfg_get_next_entry(cfg, val->cfg_off, entry, &entry, &entry->key))
            return false;

        if (!is_type_set(entry->as_value.type, mask)) {
            if (oops_on_non_matching_type)
                oops_on_unexpected_type(entry->key, mask, entry->as_value.type);
            continue;
        }
    }

    *val = entry->as_value;
    return true;
}

static bool cfg_find_and_extract(struct config *cfg, size_t offset, bool must_be_unique, struct string_view key,
                                 enum value_type type_mask, struct value *val)
{
    struct find_result res;
    struct config_entry *entry;
    config_find(cfg, offset + 1, key, 2, &res);

    if (res.count > 1 && must_be_unique)
        oops_on_non_unique_key(key);

    if (res.count == 0)
        return false;

    cfg_get_value_at_offset(cfg, res.first_occurence, &entry);
    if (!is_one_of_types(type_mask, entry->as_value.type))
        oops_on_unexpected_type(key, type_mask, entry->as_value.type);

    *val = entry->as_value;
    return true;
}

bool _cfg_get_bool(struct config *cfg, size_t offset, bool must_be_unique, struct string_view key, bool *out)
{
    struct value val;
    if (!cfg_find_and_extract(cfg, offset, must_be_unique, key, VALUE_BOOLEAN, &val))
        return false;

    *out = val.as_bool;
    return true;
}

bool _cfg_get_unsigned(struct config *cfg, size_t offset, bool must_be_unique, struct string_view key, u64 *out)
{
    struct value val;
    if (!cfg_find_and_extract(cfg, offset, must_be_unique, key, VALUE_UNSIGNED, &val))
        return false;

    *out = val.as_unsigned;
    return true;
}

bool _cfg_get_signed(struct config *cfg, size_t offset, bool must_be_unique, struct string_view key, i64 *out)
{
    struct value val;
    if (!cfg_find_and_extract(cfg, offset, must_be_unique, key, VALUE_SIGNED, &val))
        return false;

    *out = val.as_signed;
    return true;
}

bool _cfg_get_string(struct config *cfg, size_t offset, bool must_be_unique, struct string_view key,
                     struct string_view *out)
{
    struct value val;
    if (!cfg_find_and_extract(cfg, offset, must_be_unique, key, VALUE_STRING, &val))
        return false;

    *out = val.as_string;
    return true;
}

bool _cfg_get_object(struct config *cfg, size_t offset, bool must_be_unique, struct string_view key, struct value *val)
{
    return cfg_find_and_extract(cfg, offset, must_be_unique, key, VALUE_OBJECT, val);
}

bool _cfg_get_value(struct config *cfg, size_t offset, bool must_be_unique, struct string_view key, struct value *val)
{
    return cfg_find_and_extract(cfg, offset, must_be_unique, key, VALUE_ANY, val);
}

bool _cfg_get_one_of(struct config *cfg, size_t offset, bool must_be_unique,
                     struct string_view key, enum value_type mask, struct value *val)
{
    return cfg_find_and_extract(cfg, offset, must_be_unique, key, mask, val);
}
