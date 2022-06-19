#include "common/bug.h"
#include "common/log.h"
#include "common/ctype.h"
#include "common/conversions.h"
#include "config.h"

// #define CFG_DEBUG

static u32 cfg_entry_offset(struct config* cfg, struct config_entry* e)
{
    struct config_entry *b = cfg->entries_buf.buf;

    BUG_ON(e < b);
    return e - b;
}

static struct config_entry* cfg_next_entry(struct config* cfg, struct config_entry* e)
{
    BUG_ON(!e);

    if (e->next == 0)
        return NULL;

    return dynamic_buffer_get_slot(&cfg->entries_buf, cfg_entry_offset(cfg, e) + e->next);
}

#ifdef CFG_DEBUG
static void cfg_print_entries(struct config* cfg, u16 offset, int depth)
{
    struct config_entry* ent;

    if (!cfg->size)
        return;

    BUG_ON(offset >= cfg->size);
    ent = &cfg->buffer[offset];

    while (ent) {
        for (int i = 0; i < depth; i++)
            print_info("    ");

        if (ent->t == CONFIG_ENTRY_LOADABLE_ENTRY) {
            print_info("[%pSV] %zu\n", &ent->key, ent->next);

            cfg_print_entries(cfg, cfg_entry_offset(cfg, ent) + 1, depth + 1);
        }
        else if (ent->t == CONFIG_ENTRY_VALUE) {
            print_info("%pSV ", &ent->key);

            switch (ent->as_value.type) {
            case VALUE_BOOLEAN:
                print_info("= %s %zu\n", ent->as_value.as_bool ? "true" : "false", ent->next);
                break;
            case VALUE_UNSIGNED:
                print_info("= %llu %zu\n", ent->as_value.as_unsigned, ent->next);
                break;
            case VALUE_SIGNED:
                print_info("= %lld %zu\n", ent->as_value.as_signed, ent->next);
                break;
            case VALUE_STRING:
                print_info("= %pSV %zu\n", &ent->as_value.as_string, ent->next);
                break;
            case VALUE_OBJECT:
                print_info(": %zu\n", ent->next);

                cfg_print_entries(cfg, ent->as_value.cfg_off + 1, depth + 1);
                break;
            default:
                print_info("<invalid>\n");
                break;
            }
        } else {
            print_info("<none>\n");
        }

        ent = cfg_next_entry(cfg, ent);
    }
}
#endif

enum token_type
{
    TOKEN_STRING, // String, can be quoted: "hello"/'hello' or unquoted: hello
    TOKEN_INDENT, // Indentation
    TOKEN_LENTRY, // Loadable entry
    TOKEN_EQU,    // Equal sign '='
    TOKEN_COLON,  // Colon sign ':'
    TOKEN_EOF     // End of config
};

struct config_pos {
    size_t line;
    size_t column;
    size_t idx;
    size_t line_start_idx;
};

struct token
{
    enum token_type type;
    union {
        struct string_view as_str;
        u32 as_uint;
    };
    struct config_pos pos;
};

#define MAX_DEPTH 16

struct config_parser {
    struct config_pos pos;

    struct config *cfg;
    struct string_view src;

    size_t ind_count;
    char   ind_char;

#define PARSER_IN_VALUE  (1 << 0)
#define PARSER_IN_LENTRY (1 << 1)
    u8 flags;

    /*
     * Table of nesting level depth to offset within config.
     * 0 means none, any other value is index + 1
     */
    u32 depth_to_offset[MAX_DEPTH];
};

bool cfg_emplace(struct config_parser *p, struct config_entry *e, u32 depth)
{
    struct config *cfg = p->cfg;
    struct dynamic_buffer *buf = &cfg->entries_buf;
    struct config_entry *new_entry;
    size_t i;

    new_entry = dynamic_buffer_slot_alloc(buf);
    *new_entry = *e;

    if (e->t == CONFIG_ENTRY_LOADABLE_ENTRY) {
        if (!cfg->first_loadable_entry_offset)
            cfg->first_loadable_entry_offset = buf->size;
        cfg->last_loadable_entry_offset = buf->size;
    } else if (e->t == CONFIG_ENTRY_VALUE) {
        new_entry->as_value.cfg_off = buf->size - 1;
    } else {
        BUG();
    }

    if (p->depth_to_offset[depth]) {
        u32 old_idx = p->depth_to_offset[depth] - 1;
        struct config_entry *old_entry = dynamic_buffer_get_slot(&cfg->entries_buf, old_idx);
        old_entry->next = (buf->size - 1) - old_idx;
    }

    p->depth_to_offset[depth] = buf->size;
    for (i = depth + 1; i < MAX_DEPTH && p->depth_to_offset[i]; ++i)
        p->depth_to_offset[i] = 0;

    return true;
}

bool cfg_emplace_loadable_entry(struct config_parser *p, struct string_view name)
{
    struct config_entry e = {
        .t = CONFIG_ENTRY_LOADABLE_ENTRY,
        .key = name,
    };

    return cfg_emplace(p, &e, 0);
}

static inline bool cfg_eof(struct config_parser *p)
{
    return p->pos.idx >= p->src.size;
}

#define CFG_EOF_CH '\0'

static inline char cfg_getch(struct config_parser *p)
{
    char c;

    if (cfg_eof(p))
        return CFG_EOF_CH;

    c = p->src.text[p->pos.idx++];
    p->pos.column++;

    if (c == '\n') {
        p->pos.line_start_idx = p->pos.idx;
        p->pos.line++;
        p->pos.column = 0;
    }

    return c;
}

static inline void cfg_ungetch(struct config_parser *p)
{
    if (p->pos.idx == 0)
        return;

    p->pos.idx--;

    if (p->src.text[p->pos.idx] == '\n') {
        p->pos.line--;
    } else if (p->pos.column != 0) {
        p->pos.column--;
    }
}

#define ERR_INVALID_CHAR       SV("invalid character")
#define ERR_AMBIGUOUS_INDENT   SV("ambiguous indentation")
#define ERR_EXPECTED_NEWLINE   SV("expected a newline")
#define ERR_UNEXPECTED_NEWLINE SV("unexpected newline")
#define ERR_EXPECTED_IDENT     SV("expected an identifier")
#define ERR_EXPECTED_STR       SV("expected a string")
#define ERR_MAX_DEPTH          SV("exceeded maximum object depth")
#define ERR_EMPTY_OBJ          SV("an empty object")
#define ERR_EMPTY_LE           SV("an empty loadable entry")

static bool cfg_raise(struct config_parser *p, struct string_view err_message)
{
    struct config_error *error = &p->cfg->last_error;
    struct config_pos *pos = &p->pos;

    error->message = err_message;
    error->line = pos->line + 1;
    error->column = pos->column;
    error->line_start_pos = pos->line_start_idx;

    return false;
}

static bool parser_is_set(struct config_parser *p, u8 value)
{
    return (p->flags & value) == value;
}

static void parser_set(struct config_parser *p, u8 value)
{
    p->flags |= value;
}

static void parser_clear(struct config_parser *p, u8 value)
{
    p->flags &= ~value;
}

static bool cfg_verify_valid_char(struct config_parser *p, char c)
{
    if (likely(c > 31 && c < 127))
        return true;

    cfg_ungetch(p);
    return cfg_raise(p, ERR_INVALID_CHAR);
}

static bool is_reserved_char(char c)
{
    switch (c)
    {
    case ' ':
    case '\t':
    case '\r':
    case '\n':
    case ':':
    case '=':
    case '#':
    case '[':
    case ']':
        return true;
    default:
        return false;
    }
}

static bool cfg_consume_terminated_string(struct config_parser *p, char eos_char, struct string_view *out)
{
    static char err_msg[] = "expected a X";
#define ERR_CHAR_POS (sizeof(err_msg) - 2)

    out->text = &p->src.text[p->pos.idx];
    out->size = 0;

    for (;;) {
        char c = cfg_getch(p);

        if (c == eos_char)
            return true;
        if (unlikely(c == CFG_EOF_CH || c == '\r' || c == '\n')) {
            err_msg[ERR_CHAR_POS] = eos_char;
            return cfg_raise(p, SV(err_msg));
        }

        // We allow any characters inside a terminated string, don't verify
        sv_extend_by(out, 1);
    }
}

static bool cfg_consume_unterminated_string(struct config_parser *p, struct string_view* out)
{
    out->text = &p->src.text[p->pos.idx];
    out->size = 0;

    for (;;) {
        char c = cfg_getch(p);

        if (c == CFG_EOF_CH)
            return true;

        if (is_reserved_char(c)) {
            cfg_ungetch(p);
            return true;
        }

        if (unlikely(!cfg_verify_valid_char(p, c)))
            return false;

        sv_extend_by(out, 1);
    }
}

static bool cfg_skip_empty_lines(struct config_parser *p, bool allow_chars_on_first_line)
{
    bool allow_chars = allow_chars_on_first_line;
    bool in_comment = false;
    bool expect_newline = false;

    for (;;) {
        char c = cfg_getch(p);

        if (c == CFG_EOF_CH)
            return true;

        if (c == '\n') {
            in_comment = false;
            expect_newline = false;
            allow_chars = true;
            continue;
        } else if (unlikely(expect_newline)) {
            return cfg_raise(p, ERR_EXPECTED_NEWLINE);
        }

        if (in_comment)
            continue;

        if (c == '\r') {
            expect_newline = true;
            continue;
        }

        if (c == '#') {
            in_comment = true;
            continue;
        }

        if (c == ' ' || c == '\t')
            continue;

        if (!unlikely(cfg_verify_valid_char(p, c)))
            return false;

        /*
         * Random garbage after expression, e.g:
         * -------------------------------------
         * hello = world   G
         *                 ^- garbage here
         * -------------------------------------
         * hello:   G
         *          ^- garbage here
         * -------------------------------------
         */
        if (!allow_chars) {
            cfg_ungetch(p);
            return cfg_raise(p, ERR_EXPECTED_NEWLINE);
        }

        p->pos.idx -= p->pos.column;
        p->pos.column = 0;

        return true;
    }
}

static bool cfg_skip_whitespace(struct config_parser *p)
{
    for (;;) {
        char c = cfg_getch(p);

        if (c == CFG_EOF_CH || c == '\n' || c == '\r')
            return cfg_raise(p, ERR_UNEXPECTED_NEWLINE);

        if (c == ' ' || c == '\t')
            continue;

        if (unlikely(!cfg_verify_valid_char(p, c)))
            return false;

        cfg_ungetch(p);
        return true;
    }
}

static bool cfg_fetch_indentation(struct config_parser *p, struct token *tok)
{
    size_t characters = 1;
    char indent_char = cfg_getch(p);
    BUG_ON(indent_char != ' ' && indent_char != '\t');

    if (unlikely(p->ind_char && p->ind_char != indent_char))
        return cfg_raise(p, ERR_AMBIGUOUS_INDENT);

    for (;;) {
        char c = cfg_getch(p);

        if (c != ' ' && c != '\t') {
            cfg_ungetch(p);
            break;
        }

        if (unlikely(c != indent_char))
            return cfg_raise(p, ERR_AMBIGUOUS_INDENT);

        characters++;
    }

    if (!p->ind_char) {
        p->ind_char = indent_char;
        p->ind_count = characters;
    }

    if (unlikely(characters % p->ind_count))
        return cfg_raise(p, ERR_AMBIGUOUS_INDENT);

    tok->as_uint = characters / p->ind_count;
    tok->as_uint += parser_is_set(p, PARSER_IN_LENTRY);

    return true;
}

static bool cfg_fetch_token(struct config_parser *p, struct token *tok)
{
    tok->pos = p->pos;
    char c = cfg_getch(p);
    bool skip_multiline = true;

    if (c == CFG_EOF_CH) {
        tok->type = TOKEN_EOF;
        return true;
    }

    switch (c) {
    // indentation
    case ' ':
    case '\t':
        tok->type = TOKEN_INDENT;
        cfg_ungetch(p);
        return cfg_fetch_indentation(p, tok);

        // object
    case ':':
        tok->type = TOKEN_COLON;
        parser_clear(p, PARSER_IN_VALUE);
        break;

        // assignment
    case '=':
        tok->type = TOKEN_EQU;
        skip_multiline = false;
        break;

        // loadable entry
    case '[':
        tok->type = TOKEN_LENTRY;
        if (unlikely(!cfg_consume_terminated_string(p, ']', &tok->as_str)))
            return false;
        break;

        // quoted string
    case '\'':
    case '"':
        tok->type = TOKEN_STRING;
        if (unlikely(!parser_is_set(p, PARSER_IN_VALUE)))
            return cfg_raise(p, ERR_EXPECTED_IDENT);

        if (unlikely(!cfg_consume_terminated_string(p, c, &tok->as_str)))
            return false;

        parser_clear(p, PARSER_IN_VALUE);
        break;

        // unquoted string/identifier depending on context
    default:
        tok->type = TOKEN_STRING;
        cfg_ungetch(p);

        if (unlikely(!cfg_consume_unterminated_string(p, &tok->as_str)))
            return false;

        if (parser_is_set(p, PARSER_IN_VALUE)) {
            parser_clear(p, PARSER_IN_VALUE);
        } else {
            parser_set(p, PARSER_IN_VALUE);
            skip_multiline = false;
        }

        break;
    }

    return skip_multiline ? cfg_skip_empty_lines(p, false) :
           cfg_skip_whitespace(p);
}

void cfg_unfetch_token(struct config_parser *p, struct token *tok)
{
    p->pos = tok->pos;
}

void cfg_object_from_string(struct string_view str, struct value *val)
{
    if (str_to_u64(str, &val->as_unsigned)) {
        val->type = VALUE_UNSIGNED;
        return;
    }

    if (str_to_i64(str, &val->as_signed)) {
        val->type = VALUE_SIGNED;
        return;
    }

    if (sv_equals_caseless(str, SV("true"))) {
        val->type = VALUE_BOOLEAN;
        val->as_bool = true;
        return;
    }

    if (sv_equals_caseless(str, SV("false"))) {
        val->type = VALUE_BOOLEAN;
        val->as_bool = false;
        return;
    }

    if (sv_equals_caseless(str, SV("null"))) {
        val->type = VALUE_NONE;
        return;
    }

    val->type = VALUE_STRING;
    val->as_string = str;
}

bool cfg_parse_objects(struct config_parser *p)
{
    struct token tok = { 0 };
    size_t base_depth = parser_is_set(p, PARSER_IN_LENTRY) ? 1 : 0;
    size_t current_depth = base_depth;
    enum token_type prev_tok = TOKEN_EOF;

    for (;;) {
        struct config_entry ce = { 0 };

        if (!cfg_fetch_token(p, &tok))
            return false;

        /*
         * Attempted to do something like:
         * ----------------------------------------------------------------
         * foobar:
         *     x <----------- expected a value here
         * val = 123 # 'foobar' is empty because 'val' is at the same level
         * ----------------------------------------------------------------
         */
        if (prev_tok == TOKEN_COLON && (tok.type != TOKEN_INDENT || tok.as_uint != current_depth)) {
            cfg_unfetch_token(p, &tok);
            return cfg_raise(p, ERR_EMPTY_OBJ);
        }

        if (tok.type == TOKEN_EOF || tok.type == TOKEN_LENTRY) {
            cfg_unfetch_token(p, &tok);
            break;
        }

        if (tok.type == TOKEN_INDENT) {
            if (tok.as_uint > current_depth)
                return cfg_raise(p, ERR_AMBIGUOUS_INDENT);

            current_depth = tok.as_uint;
            prev_tok = tok.type;
            continue;
        } else if (prev_tok != TOKEN_INDENT) {
            current_depth = base_depth;
        }

        if (tok.type != TOKEN_STRING)
            return cfg_raise(p, ERR_EXPECTED_IDENT);

        ce.key = tok.as_str;
        ce.t = CONFIG_ENTRY_VALUE;

        if (!cfg_fetch_token(p, &tok))
            return false;
        prev_tok = tok.type;

        if (tok.type == TOKEN_COLON) {
            ce.as_value.type = VALUE_OBJECT;

            if (unlikely(current_depth >= MAX_DEPTH))
                return cfg_raise(p, ERR_MAX_DEPTH);

            if (!cfg_emplace(p, &ce, current_depth++))
                return false;
        } else if (tok.type == TOKEN_EQU) {
            if (!cfg_fetch_token(p, &tok))
                return false;

            if (tok.type != TOKEN_STRING)
                return cfg_raise(p, ERR_EXPECTED_STR);

            cfg_object_from_string(tok.as_str, &ce.as_value);

            if (!cfg_emplace(p, &ce, current_depth))
                return false;
        } else {
            return cfg_raise(p, SV("expected one of ':' or '='"));
        }
    }

    return true;
}

bool cfg_parse(struct string_view src, struct config *cfg)
{
    struct config_parser p = {
        .cfg = cfg,
        .src = src,
    };
    bool must_be_ident = false;
    bool ret = true;

    memzero(p.cfg, sizeof(struct config));
    dynamic_buffer_init(&cfg->entries_buf, sizeof(struct config_entry), true);

    // Skip whitespace/comments at the beginning of config
    if (unlikely(!cfg_skip_empty_lines(&p, true)))
        goto err_out;

    for (;;) {
        struct token tok = { 0 };

        if (unlikely(!cfg_fetch_token(&p, &tok)))
            goto err_out;

        switch (tok.type) {
        case TOKEN_STRING:
            must_be_ident = false;
            cfg_unfetch_token(&p, &tok);
            parser_clear(&p, PARSER_IN_VALUE);

            if (unlikely(!cfg_parse_objects(&p)))
                goto err_out;

            continue;

        case TOKEN_LENTRY:
            if (unlikely(must_be_ident)) {
                cfg_raise(&p, ERR_EMPTY_LE);
                goto err_out;
            }

            if (unlikely(!cfg_emplace_loadable_entry(&p, tok.as_str)))
                goto err_out;

            must_be_ident = true;
            parser_set(&p, PARSER_IN_LENTRY);
            continue;

        case TOKEN_INDENT:
            cfg_raise(&p, ERR_AMBIGUOUS_INDENT);
            goto err_out;

        case TOKEN_EQU:
        case TOKEN_COLON:
            cfg_raise(&p, ERR_EXPECTED_IDENT);
            goto err_out;

        case TOKEN_EOF:
            if (unlikely(must_be_ident)) {
                cfg_raise(&p, ERR_EMPTY_LE);
                goto err_out;
            }

            goto out;

        default:
            BUG();
        }
    }

err_out:
    ret = false;
    dynamic_buffer_release(&cfg->entries_buf);

out:
#ifdef CFG_DEBUG
    cfg_print_entries(cfg, 0, 0);
#endif

    return ret;
}

void cfg_pretty_print_error(const struct config_error *err, struct string_view src)
{
    size_t i = err->line_start_pos;
    struct string_view line_view = {
        .text = &src.text[err->line_start_pos],
        .size = 0
    };

    while (i < src.size && src.text[i] != '\n') {
        ++i;
        line_view.size++;
    }

    print_err("Config:%zu:%zu parse error:\n", err->line, err->column);
    print_err("%4zu | ", err->line);
    print_err("%pSV\n     | ", &line_view);

    for (i = 0; i < err->column; i++)
        print_err(" ");

    print_err("^-- %pSV here\n", &err->message);
}

bool cfg_get_loadable_entry(struct config *cfg, struct string_view key, struct loadable_entry *val)
{
    if (cfg->first_loadable_entry_offset == 0)
        return false;

    struct config_entry *ent = dynamic_buffer_get_slot(&cfg->entries_buf, cfg->first_loadable_entry_offset - 1);

    while (ent) {
        if (sv_equals(ent->key, key)) {
            *val = (struct loadable_entry) {
                .name = key,
                .cfg_off = cfg_entry_offset(cfg, ent)
            };
            return true;
        }

        ent = cfg_next_entry(cfg, ent);
    }

    return false;
}

bool cfg_first_loadable_entry(struct config *cfg, struct loadable_entry *val)
{
    if (cfg->first_loadable_entry_offset == 0)
        return false;

    struct config_entry *ent = dynamic_buffer_get_slot(&cfg->entries_buf, cfg->first_loadable_entry_offset - 1);

    *val = (struct loadable_entry) {
        .name = ent->key,
        .cfg_off = cfg_entry_offset(cfg, ent)
    };
    return true;
}

struct find_result {
    size_t first;
    size_t last;
    size_t count;
};

static void cfg_find(struct config *cfg, size_t offset, struct string_view key, size_t max, struct find_result *res)
{
    BUG_ON(offset >= cfg->entries_buf.size);
    memzero(res, sizeof(struct find_result));

    for (;;) {
        struct config_entry *ent = dynamic_buffer_get_slot(&cfg->entries_buf, offset);

        if (ent->t != CONFIG_ENTRY_VALUE)
            break;

        if (sv_equals(ent->key, key)) {
            res->last = offset;

            if (!res->count)
                res->first = offset;

            res->count++;

            if (res->count >= max)
                break;
        }

        if (ent->next == 0)
            break;

        offset += ent->next;
    }
}

static bool is_of_type(enum value_type type, enum value_type mask)
{
    return (type & mask) == type;
}

static void oops_on_unexpected_type(struct string_view key, enum value_type type, enum value_type expected)
{
    struct string_view type_str;
    bool first_type = true;
    size_t i;

    BUG_ON(expected == VALUE_ANY);

    type_str = value_type_as_str(type);
    print_err("Oops! \"%pSV\" has an unexpected type of %pSV, expected ", &key, &type_str);

    for (i = 0; i < 6; i++) {
        enum value_type t = expected & (1 << i);

        if (!t)
            continue;

        type_str = value_type_as_str(t);

        if (!first_type)
            print_err(" or ");

        print_err("%pSV", &type_str);
        first_type = false;
    }
    print_err(".");

    for (;;);
}

static bool cfg_find_ext(struct config *cfg, size_t offset, bool unique, struct string_view key,
                         enum value_type mask, struct value *val)
{
    struct find_result res;
    struct config_entry *ent;

    BUG_ON(!mask);
    BUG_ON((ssize_t)offset != -1 && offset >= (cfg->entries_buf.size ? cfg->entries_buf.size - 1 : 0));

    if (cfg_empty(cfg))
        return false;

    cfg_find(cfg, offset + 1, key, 2, &res);

    if (res.count > 1 && unique)
        oops("%pSV must be unique\n", &key);

    if (!res.count)
        return false;

    ent = dynamic_buffer_get_slot(&cfg->entries_buf, res.first);

    if (!is_of_type(ent->as_value.type, mask))
        oops_on_unexpected_type(key, ent->as_value.type, mask);

    *val = ent->as_value;
    return true;
}

bool cfg_get_next_one_of(struct config *cfg, enum value_type mask, struct value *val, bool oops_on_non_matching_type)
{
    struct config_entry *entry = dynamic_buffer_get_slot(&cfg->entries_buf, val->cfg_off);
    struct string_view key = entry->key;

    for (;;) {
        entry = cfg_next_entry(cfg, entry);
        if (!entry)
            return false;

        if (!sv_equals(entry->key, key))
            continue;

        if (!is_of_type(entry->as_value.type, mask)) {
            if (oops_on_non_matching_type)
                oops_on_unexpected_type(key, entry->as_value.type, mask);

            continue;
        }

        break;
    }

    *val = entry->as_value;
    return true;
}

bool cfg_get_next(struct config *cfg, struct value *val, bool oops_on_non_matching_type)
{
    return cfg_get_next_one_of(cfg, val->type, val, oops_on_non_matching_type);
}

bool _cfg_get_bool(struct config *cfg, size_t offset, bool unique,
                   struct string_view key, bool *val)
{
    struct value value;
    if (!cfg_find_ext(cfg, offset, unique, key, VALUE_BOOLEAN, &value))
        return false;

    *val = value.as_bool;
    return true;
}

bool _cfg_get_unsigned(struct config *cfg, size_t offset, bool unique,
                       struct string_view key, u64 *val)
{
    struct value value;
    if (!cfg_find_ext(cfg, offset, unique, key, VALUE_UNSIGNED, &value))
        return false;

    *val = value.as_unsigned;
    return true;
}

bool _cfg_get_signed(struct config *cfg, size_t offset, bool unique,
                     struct string_view key, i64 *val)
{
    struct value value;
    if (!cfg_find_ext(cfg, offset, unique, key, VALUE_SIGNED, &value))
        return false;

    *val = value.as_signed;
    return true;
}

bool _cfg_get_string(struct config *cfg, size_t offset, bool unique,
                     struct string_view key, struct string_view *val)
{
    struct value value;
    if (!cfg_find_ext(cfg, offset, unique, key, VALUE_STRING, &value))
        return false;

    *val = value.as_string;
    return true;

}

bool _cfg_get_object(struct config *cfg, size_t offset, bool unique,
                     struct string_view key, struct value *val)
{
    return cfg_find_ext(cfg, offset, unique, key, VALUE_OBJECT, val);
}

bool _cfg_get_value(struct config *cfg, size_t offset, bool unique,
                    struct string_view key, struct value *val) {
    return cfg_find_ext(cfg, offset, unique, key, VALUE_ANY, val);
}

bool _cfg_get_one_of(struct config *cfg, size_t offset, bool unique, struct string_view key,
                     enum value_type mask, struct value *val)
{
    return cfg_find_ext(cfg, offset, unique, key, mask, val);
}
