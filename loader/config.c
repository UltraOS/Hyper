#include "config.h"
#include "allocator.h"
#include "common/conversions.h"
#include "common/constants.h"
#include "common/minmax.h"
#include "common/log.h"
#include "common/format.h"
#include "common/ctype.h"


// #define CFG_DEBUG


enum token_type
{
    TK_IDENT,  // Identifier / string. Can be quoted ('hello', "hello") or unquoted (hello)
    TK_INDENT, // Indentation
    TK_INT,    // Integer
    TK_BOOL,   // Boolean
    TK_NULL,   // Null value
    TK_ENTRY,  // Loadable entry
    TK_EQU,    // Equal sign '='
    TK_COLON,  // Colon sign ':'
    TK_EOF     // End of config
};

struct config_pos {
    size_t line;
    size_t column;
    size_t pos;
};

struct token
{
    enum token_type type;

    union {
        struct string_view ident;
        bool is_true;

        struct {
            bool is_signed;
            union {
                i64 num_i64;
                u64 num_u64;
            };
        };
    };

    struct config_pos pos;
};

struct config_parser {
    // These change each time we fetch a token
    struct config_pos pos;

    // These are set once
    struct config *cfg;
    const char *source;
    size_t src_size;

    size_t ind_count; // Indentation character count
    char ind_char;    // Indentation character

    // These may change depending on context
    bool preserve_line;
};


static inline struct config_entry *cfg_last_entry(struct config_parser *parser)
{
    return parser->cfg->size == 0 ? NULL : &parser->cfg->buffer[parser->cfg->size - 1];
}

static inline struct config_entry *cfg_last_entry_to(struct config_parser *parser, size_t offset)
{
    BUG_ON(offset > parser->cfg->size);
    BUG_ON(offset == 0);

    struct config_entry *ent = &parser->cfg->buffer[--offset];
    size_t rel = 1;


    while (ent->next != rel && offset > 0) {
        ent = &parser->cfg->buffer[--offset];
        rel++;
    }

    return (offset == 0 && ent->next != rel) ? NULL : ent;
}

static inline u16 cfg_ent_offset(struct config *cfg, struct config_entry *ent)
{
    BUG_ON(ent < cfg->buffer);

    return ent - cfg->buffer;
}

static inline struct config_entry *cfg_next_ent(struct config *cfg, struct config_entry *ent)
{
    BUG_ON(!ent);
    return ent->next == 0 ? NULL : &cfg->buffer[cfg_ent_offset(cfg, ent) + ent->next];
}

static inline bool cfg_eof(struct config_parser *parser)
{
    return parser->pos.pos >= parser->src_size;
}

static inline char cfg_getch(struct config_parser *parser)
{
    return cfg_eof(parser) ? '\0' : ({ parser->pos.column++; parser->source[parser->pos.pos++]; });
}

static inline void cfg_ungetch(struct config_parser *parser)
{
    if (parser->pos.pos != 0)
        parser->pos.pos--;

    if (parser->pos.column != 0)
        parser->pos.column--;
}

static bool cfg_raise(struct config_parser *parser, struct config_pos *pos, const char *m)
{
    struct config_error *error = &parser->cfg->last_error;

    error->message = SV(m);
    error->line    = pos->line + 1;
    error->column  = pos->column == 0 ? 0 : pos->column - 1;
    error->pos     = pos->pos == 0 ? 0 : pos->pos - 1;

    return false;
}

static bool cfg_skip_line(struct config_parser *parser)
{
    char ch;

    if (parser->preserve_line)
        return cfg_raise(parser, &parser->pos, "Tried skipping line while trying to preserve it");

    ch = cfg_getch(parser);

    while (ch == ' ' || ch == '\t' || ch == '\r')
        ch = cfg_getch(parser);

    if (ch == '#') {
        while (ch != '\n')
            ch = cfg_getch(parser);

        parser->pos.column = 0;
        parser->pos.line++;

        return true;
    } else if (ch == '\n') {
        parser->pos.column = 0;
        parser->pos.line++;

        return true;
    }

    return cfg_raise(parser, &parser->pos, "Invalid character while skipping line");
}

static void cfg_unfetch_token(struct config_parser *parser, struct token *token)
{
    parser->pos = token->pos;
}

static inline bool isnumerical(char ch)
{
    return (ch >= '0' && ch <= '9') ||
           (ch >= 'a' && ch <= 'f') ||
           (ch >= 'A' && ch <= 'F') ||
           (ch == '-' || ch == '+') ||
           (ch == 'x' || ch == 'X');
}

static bool cfg_fetch_token(struct config_parser *parser, struct token *token)
{
    if (cfg_eof(parser)) {
        token->pos  = parser->pos;
        token->type = TK_EOF;
        return true;
    }

    char ch = cfg_getch(parser);

    if (ch == '\r') {
        if (parser->preserve_line)
            return cfg_raise(parser, &parser->pos, "Unexpected new line");

        ch = cfg_getch(parser);
    }

    if (ch == '\n') {
        cfg_ungetch(parser);

        while ((ch = cfg_getch(parser)) == '\n')
            parser->pos.line++;

        cfg_ungetch(parser);

        parser->pos.column = 0;

        if (parser->preserve_line)
            return cfg_raise(parser, &parser->pos, "Unexpected new line");
    } else if (ch == '#') {
        if (parser->preserve_line)
            return cfg_raise(parser, &parser->pos, "Unexpected comment");

        while (ch != '\n')
            ch = cfg_getch(parser);

        parser->pos.line++;
        parser->pos.column = 0;
    } else {
        cfg_ungetch(parser);
    }

    token->pos = parser->pos;

    if (parser->pos.column == 0 && (ch == ' ' || ch == '\t')) {
        // Try fetch indentation
        size_t i = 0;

        if (parser->ind_count == 0) {
            parser->ind_count = 1;
            parser->ind_char  = cfg_getch(parser);

            if (parser->ind_char != '\t' && parser->ind_char != ' ')
                return cfg_raise(parser, &parser->pos, "Invalid indentation character");

            while ((ch = cfg_getch(parser)) == parser->ind_char)
                parser->ind_count++;

            if (ch == '\t' || ch == ' ')
                return cfg_raise(parser, &parser->pos, "Invalid indentation character");

            i = 1;
        } else {
            while ((ch = cfg_getch(parser)) == parser->ind_char)
                i++;

            if (ch == '\t' || ch == ' ')
                return cfg_raise(parser, &parser->pos, "Invalid indentation character");

            if ((i % parser->ind_count) != 0)
                return cfg_raise(parser, &token->pos, "Invalid count of spaces/tabs");

            i = i / parser->ind_count;
        }

        token->type    = TK_INDENT;
        token->num_u64 = i;

        cfg_ungetch(parser);
        return true;
    } else {
        if (ch == ' ' || ch == '\t') {
            while (ch == ' ' || ch == '\t')
                ch = cfg_getch(parser);

            cfg_ungetch(parser);
        }
    }

    if (cfg_eof(parser)) {
        token->pos  = parser->pos;
        token->type = TK_EOF;
        return true;
    }

    token->pos = parser->pos;

    switch (ch = cfg_getch(parser)) {
    case '\0':
        token->type = TK_EOF;
        return true;
    case '=':
        token->type = TK_EQU;
        return true;
    case ':':
        token->type = TK_COLON;
        return true;
    case '[':
        token->type = TK_ENTRY;
        token->ident.text = &parser->source[parser->pos.pos];
        token->ident.size = 0;

        while (isalnum(ch = cfg_getch(parser)) || ch == '_')
            token->ident.size++;

        if (ch != ']')
            return cfg_raise(parser, &parser->pos, "Invalid character in loadable entry");

        return true;
    case '+':
    case '-':
    case '0'...'9':
        token->type = TK_INT;

        cfg_ungetch(parser);

        token->ident.text = &parser->source[parser->pos.pos];
        token->ident.size = 0;

        while (isnumerical(ch = cfg_getch(parser)))
            token->ident.size++;

        cfg_ungetch(parser);

        if (token->ident.text[0] == '-') {
            if (!str_to_i64(token->ident, &token->num_i64))
                return cfg_raise(parser, &token->pos, "Invalid signed number");
            token->is_signed = true;
        } else {
            if (!str_to_u64(token->ident, &token->num_u64))
                return cfg_raise(parser, &token->pos, "Invalid unsigned number");
            token->is_signed = false;
        }

        return true;
    case '\'':
    case '"':
    case '_':
    case 'a'...'z':
    case 'A'...'Z': {
        char quote = ch;

        token->type = TK_IDENT;

        if (quote == '\'' || quote == '"') {
            token->ident.text = &parser->source[parser->pos.pos];
            token->ident.size = 0;

            while ((ch = cfg_getch(parser)) != quote && ch >= ' ' && ch < '\x7F' /* ASCII DEL */)
                token->ident.size++;

            if (ch != quote)
                return cfg_raise(parser, &parser->pos, !ch ? "Open-ended quote" : "Invalid quote");

            return true;
        }

        cfg_ungetch(parser);

        token->ident.text = &parser->source[parser->pos.pos];
        token->ident.size = 0;

        while (isalnum(ch) || ch == '_' || ch == '-') {
            ch = cfg_getch(parser);
            token->ident.size++;
        }

        token->ident.size--;
        cfg_ungetch(parser);

        if (sv_equals(token->ident, SV("null"))) {
            token->type = TK_NULL;
        } else if (sv_equals(token->ident, SV("true"))) {
            token->type    = TK_BOOL;
            token->is_true = true;
        } else if (sv_equals(token->ident, SV("false"))) {
            token->type    = TK_BOOL;
            token->is_true = false;
        }

        return true;
      }
    case '\r':
    case '\n':
        return cfg_fetch_token(parser, token);
    default:
        return cfg_raise(parser, &token->pos, "Unknown character");
    }
}

static struct config_entry *cfg_fetch_entry(struct config_parser *parser)
{
    struct config_entry *ent = parser->cfg->size >= parser->cfg->capacity ? NULL :
                              &parser->cfg->buffer[parser->cfg->size++];

    if (ent == NULL) {
        size_t cap = MAX(parser->cfg->capacity * 2, PAGE_SIZE / sizeof(struct config_entry));

        if (!(ent = allocate_bytes(sizeof(struct config_entry) * cap)))
            return NULL;

        memcpy(ent, parser->cfg->buffer, sizeof(struct config_entry) * parser->cfg->size);

        parser->cfg->capacity = cap;
        parser->cfg->buffer   = ent;

        return cfg_fetch_entry(parser);
    }

    return ent;
}

/*
 *  Returns -1 in case there is an error
 *  Returns the number of children + 1 for success
 *  Returns (-2 - new indentation level) when the level changes
*/
static ssize_t cfg_parse_object(struct config_parser *parser, size_t lvl)
{
    if (lvl > 256)
        return cfg_raise(parser, &parser->pos, "Indentation overflow (>256)") - 1;

    ssize_t children, total = 1;
    size_t indent;
    struct token tok;
    struct config_entry *ent;

    if (!cfg_fetch_token(parser, &tok))
        return -1;

    indent = tok.type == TK_INDENT ? tok.num_u64 : 0;

    if (indent != lvl) {
        if (indent > lvl)
            return cfg_raise(parser, &tok.pos, "Invalid indentation") - 1;

        cfg_unfetch_token(parser, &tok);
        parser->preserve_line = false;
        return -2 - indent;
    }

    if (tok.type != TK_INDENT)
        cfg_unfetch_token(parser, &tok);

    parser->preserve_line = true;

    if (!cfg_fetch_token(parser, &tok))
        return -1;

    if (tok.type != TK_IDENT)
        return cfg_raise(parser, &tok.pos, "Expected identifier") - 1;

    ent = cfg_fetch_entry(parser);

    if (!ent)
        return cfg_raise(parser, &tok.pos, "Out of memory") - 1;

    ent->key = tok.ident;
    ent->as_value.cfg_off = cfg_ent_offset(parser->cfg, ent);

    if (!cfg_fetch_token(parser, &tok))
        return -1;

    if (tok.type != TK_COLON && tok.type != TK_EQU)
        return cfg_raise(parser, &tok.pos, "Expected ':' or '='") - 1;

    ent->t = CONFIG_ENTRY_VALUE;

    if (tok.type == TK_EQU) {
        ent->next = 1;

        if (!cfg_fetch_token(parser, &tok))
            return -1;

        parser->preserve_line = false;
        cfg_skip_line(parser);

        switch (tok.type)
        {
        case TK_IDENT:
            ent->as_value.type = VALUE_STRING;
            ent->as_value.as_string = tok.ident;
            return 1;
        case TK_INT:
            if (tok.is_signed) {
                ent->as_value.type = VALUE_SIGNED;
                ent->as_value.as_signed = tok.num_i64;
            } else {
                ent->as_value.type = VALUE_UNSIGNED;
                ent->as_value.as_signed = tok.num_u64;
            }
            return 1;
        case TK_BOOL:
            ent->as_value.type = VALUE_BOOLEAN;
            ent->as_value.as_bool = tok.is_true;
            return 1;
        case TK_NULL:
            ent->as_value.type = VALUE_NONE;
            return 1;
        default:
            return cfg_raise(parser, &tok.pos, "Expected value") - 1;
        }
    }

    ent->as_value.type = VALUE_OBJECT;

    parser->preserve_line = false;
    cfg_skip_line(parser);

    while ((children = cfg_parse_object(parser, lvl + 1)) > -2) {
        if (children == -1)
            return -1;

        total += children;
    }

    indent = -children - 2;

    ent->next = indent == lvl ? total : 0;
    cfg_last_entry(parser)->next = 0;

    return total;
}

#ifdef CFG_DEBUG
static void cfg_print_entries(struct config *cfg, u16 offset, int depth)
{
    BUG_ON(offset >= cfg->size);

    struct config_entry *ent = &cfg->buffer[offset];

    while (ent) {
        for (int i = 0; i < depth; i++)
            print_info("    ");

        if (ent->t == CONFIG_ENTRY_LOADABLE_ENTRY) {
            print_info("[%pSV] %zu\n", &ent->key, ent->next);

            cfg_print_entries(cfg, cfg_ent_offset(cfg, ent) + 1, depth + 1);
        } else if (ent->t == CONFIG_ENTRY_VALUE) {
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

        ent = cfg_next_ent(cfg, ent);
    }
}
#endif

bool cfg_parse(struct string_view text, struct config *cfg)
{
    struct config_parser parser = {
        .pos           = { 0, 0, 0 },
        .cfg           = cfg,
        .source        = text.text,
        .src_size      = text.size,
        .ind_count     = 0,
        .ind_char      = 0,
        .preserve_line = false
    };

    struct config_entry *ent = NULL;

    for (;;) {
        struct token tok;

        if (!cfg_fetch_token(&parser, &tok))
            return false;

        if (tok.type == TK_ENTRY) {
            if (ent != NULL) {
                if (cfg_last_entry(&parser)->t == CONFIG_ENTRY_LOADABLE_ENTRY)
                    return cfg_raise(&parser, &parser.pos, "Empty loadable entry isn't allowed");

                cfg_last_entry_to(&parser, cfg->size)->next = 0;
                ent->next = cfg->size - cfg_ent_offset(cfg, ent);
            } else {
                cfg->first_loadable_entry_offset = cfg->size + 1;
            }

            if (!(ent = cfg_fetch_entry(&parser)))
                return cfg_raise(&parser, &parser.pos, "Out of config entries");

            ent->key  = tok.ident;
            ent->t    = CONFIG_ENTRY_LOADABLE_ENTRY;

            parser.preserve_line = false;
            cfg_skip_line(&parser);
        } else if (tok.type == TK_EOF) {
            if (ent != NULL) {
                if (cfg_last_entry(&parser)->t == CONFIG_ENTRY_LOADABLE_ENTRY)
                    return cfg_raise(&parser, &parser.pos, "Empty loadable entry isn't allowed");

                cfg_last_entry_to(&parser, cfg->size)->next = 0;
                ent->next = 0;

                cfg->last_loadable_entry_offset = cfg_ent_offset(cfg, ent);
            }

            if (cfg_last_entry(&parser) != NULL)
                cfg_last_entry(&parser)->next = 0;

            break;
        } else {
            cfg_unfetch_token(&parser, &tok);
            if (cfg_parse_object(&parser, 0) == -1)
                return false;
        }
    }

#ifdef CFG_DEBUG
    cfg_print_entries(cfg, 0, 0);
#endif

    return true;
}

void cfg_pretty_print_error(const struct config_error *err, struct string_view cfg)
{
    struct string_view line_sv;
    size_t i;

    print_err("Config:%zu:%zu: error: %pSV\n", err->line, err->column, &err->message);

    for (i = 0; i < err->line - 1; i++) {
        char ch = 0;

        while (ch != '\n') {
            if (!sv_pop_one(&cfg, &ch))
                break;
        }

        BUG_ON(ch != '\n');
    }

    line_sv.text = cfg.text;
    line_sv.size = 0;

    for (i = 0; cfg.text[i] != '\n' && i < cfg.size; i++)
        line_sv.size++;

    print_err("%4zu | ", err->line);
    print_err("%pSV\n     | ", &line_sv);

    for (i = 0; i < err->column; i++)
        print_err(" ");

    print_err("^\n");
}

bool cfg_get_loadable_entry(struct config *cfg, struct string_view key, struct loadable_entry *val)
{
    if (cfg->first_loadable_entry_offset == 0)
        return false;

    struct config_entry *ent = &cfg->buffer[cfg->first_loadable_entry_offset - 1];

    while (ent) {
        if (sv_equals(ent->key, key)) {
            *val = (struct loadable_entry) { .name = key, .cfg_off = cfg_ent_offset(cfg, ent) };
            return true;
        }

        ent = cfg_next_ent(cfg, ent);
    }

    return false;
}

bool cfg_first_loadable_entry(struct config *cfg, struct loadable_entry *val)
{
    if (cfg->first_loadable_entry_offset == 0)
        return false;

    struct config_entry *ent = &cfg->buffer[cfg->first_loadable_entry_offset - 1];

    *val = (struct loadable_entry) { .name = ent->key, .cfg_off = cfg_ent_offset(cfg, ent) };
    return true;
}

struct find_result {
    size_t first;
    size_t last;
    size_t count;
};

static void cfg_find(struct config *cfg, size_t offset, struct string_view key, size_t max, struct find_result *res)
{
    BUG_ON(offset >= cfg->size);
    memzero(res, sizeof(struct find_result));

    for (;;) {
        struct config_entry *ent = &cfg->buffer[offset];

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
    BUG_ON(expected == VALUE_ANY);
    struct string_view type_str = value_type_as_str(type);
    bool f = false;
    int i;


    print_err("Oops: %pSV has an unexpected type of %pSV. Expected ", &key, &type_str);


    for (i = 0; i < 6; i++) {
        enum value_type t = type & (1 << i);

        if (!t)
            continue;

        type_str = value_type_as_str(t);

        if (f)
            print_err(" or ");

        print_err("%pSV", &type_str);
        f = true;
    }

    for (;;);
}

static bool cfg_find_ext(struct config *cfg, size_t offset, bool unique, struct string_view key, 
                     enum value_type mask, struct value *val)
{
    BUG_ON(!mask);
    BUG_ON(offset >= cfg->size);

    struct find_result res;
    struct config_entry *ent;

    cfg_find(cfg, offset == 0 ? 0 : offset + 1, key, 2, &res);

    if (res.count > 1 && unique)
        oops("%pSV must be unique", &key);

    if (!res.count)
        return false;

    ent = &cfg->buffer[res.first];

    if (!is_of_type(ent->as_value.type, mask))
        oops_on_unexpected_type(key, ent->as_value.type, mask);

    *val = ent->as_value;
    return true;
}

bool cfg_get_next(struct config *cfg, struct value *val, bool type_oops)
{
    struct config_entry *entry;
    enum value_type type;
    struct string_view key;

    entry = &cfg->buffer[val->cfg_off];
    type  = entry->as_value.type;
    key   = entry->key;

    for (;;) {
        if (!cfg_next_ent(cfg, entry))
            return false;

        if (!sv_equals(entry->key, key))
            continue;

        if (entry->as_value.type != type) {
            if (type_oops)
                oops_on_unexpected_type(key, entry->as_value.type, type);

            continue;
        }

        break;
    }

    *val = entry->as_value;
    return true;
}

bool cfg_get_next_one_of(struct config *cfg, enum value_type mask, struct value *val, bool type_oops)
{
    struct config_entry *entry;
    struct string_view key;

    entry = &cfg->buffer[val->cfg_off];
    key   = entry->key;

    for (;;) {
        if (!cfg_next_ent(cfg, entry))
            return false;

        if (!sv_equals(entry->key, key))
            continue;

        if (!is_of_type(entry->as_value.type, mask)) {
            if (type_oops)
                oops_on_unexpected_type(key, entry->as_value.type, mask);

            continue;
        }

        break;
    }

    *val = entry->as_value;
    return true;
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
