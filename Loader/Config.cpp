#include "Config.h"
#include "Allocator.h"
#include "Common/Conversions.h"

Config::FindResult Config::find(size_t offset, StringView key, size_t constraint_max) const
{
    ASSERT(offset < m_size);
    FindResult result {};

    for (;;) {
        auto& entry = m_buffer[offset];

        if (entry.type != ConfigEntry::VALUE)
            continue;

        if (entry.key == key) {
            result.last_occurence = offset;

            if (!result.count)
                result.first_occurence = offset;

            result.count++;

            if (constraint_max && result.count == constraint_max)
                break;
        }

        if (entry.offset_to_next_within_same_scope == 0)
            break;

        offset += entry.offset_to_next_within_same_scope;
    }

    return result;
}

#define PARSE_ERROR(msg)                         \
    do {                                         \
        m_error.message = msg;                   \
        m_error.line = s.file_line;              \
        m_error.offset = s.line_offset;          \
        m_error.global_offset = s.global_offset; \
        return false;                            \
    } while (0)

[[nodiscard]] bool Config::parse(StringView config)
{
    enum class State {
        NORMAL,
        KEY,
        VALUE,
        LOADABLE_ENTRY_TITLE,
        COMMENT
    };

    static constexpr size_t depth_capacity = 255;

    struct ParseState {
        size_t file_line = 1;
        size_t line_offset = 1;
        size_t global_offset = 0;

        State state = State::NORMAL;

        // Character that is picked as whitespace in the current configuration.
        // One of '\t' or ' '. Value of 0 means we don't know yet.
        char whitespace_character = 0;

        // Current depth in picked whitespace characters.
        size_t current_whitespace_depth = 0;

        // The number of characters we treat as one indentation level.
        // 0 means we don't know yet.
        size_t characters_per_level = 0;

        // Set in case we have encountered whitespace in the current value string,
        // e.g "key=val e" in this example, the 'e' after whitespace is considered invalid.
        bool expecting_end_of_value = false;

        // Set for KEY/VALUE in case at least one character has been consumed
        bool consumed_at_least_one = false;

        // set for loadable entries where first key starts at an offset
        Optional<bool> base_depth_is_nonzero;

        // Character used by the current value for quoting, either ' or ".
        // 0 means none.
        char open_quote_character = 0;

        StringView current_value_view;
        ConfigEntry current{};

        bool within_loadable_entry = false;
        bool expecting_depth_plus_one = false;

        // Empty loadable entries are not allowed
        bool consumed_at_least_one_kv = false;

        size_t current_depth = 0;

        // Depth -> offset pairs, used to link together values of the same scope.
        Optional<u32> depth_to_offset[depth_capacity];
    };

    allocator::ScopedObjectAllocation<ParseState> parse_state;

    if (parse_state.failed()) {
        m_error.message = "out of memory";
        return false;
    }

    auto& s = *parse_state.value();

    auto is = [&s](State state) -> bool { return state == s.state; };

    auto set = [&](State state)
    {
        switch (state)
        {
        case State::NORMAL:
            if (is(State::LOADABLE_ENTRY_TITLE)) {
                s.within_loadable_entry = true;

                for (size_t i = 1; i <= s.current_depth + !s.base_depth_is_nonzero.value_or(false); ++i)
                    s.depth_to_offset[i].reset();
                s.current_depth = 0;
            }
            s.expecting_end_of_value = false;
            s.consumed_at_least_one = false;
            s.open_quote_character = 0;

            break;
        case State::KEY:
            s.consumed_at_least_one = false;
            s.expecting_depth_plus_one = true;
            break;
        case State::VALUE:
            s.expecting_depth_plus_one = false;
            s.consumed_at_least_one = false;
            s.expecting_end_of_value = false;
            s.open_quote_character = 0;
            break;
        case State::LOADABLE_ENTRY_TITLE:
            s.consumed_at_least_one = false;
            s.consumed_at_least_one_kv = false;
            break;
        default:
            break;
        }

        s.state = state;
    };

    auto deduce_object_type = [&]() -> Value {
        // Value is stored inside "" or '', force a string type.
        if (s.open_quote_character)
            return s.current_value_view;

        if (s.current_value_view == "null")
            return {};
        if (s.current_value_view == "true")
            return true;
        if (s.current_value_view == "false")
            return false;

        Value value_as_number {};
        if (try_parse_as_number(s.current_value_view, value_as_number))
            return value_as_number;

        // Nothing else worked, assume string.
        return s.current_value_view;
    };

    auto finalize_key_value = [&](bool is_object = false) -> bool {
        Value value;

        s.current.type = ConfigEntry::VALUE;

        if (is_object) {
            value = { this };
        } else {
            value = deduce_object_type();
        }

        s.current.data.as_value = value;

        bool ok = false;
        auto offset = emplace_entry(s.current, ok);
        if (!ok)
            PARSE_ERROR("out of memory");

        auto depth = s.current_depth;
        depth += s.within_loadable_entry;
        depth -= s.base_depth_is_nonzero.value_or(false);

        auto previous_offset = s.depth_to_offset[depth];
        if (previous_offset) {
            auto& entry = m_buffer[previous_offset.value()];
            entry.offset_to_next_within_same_scope = offset - previous_offset.value();
        }

        s.depth_to_offset[depth] = static_cast<u32>(offset);
        s.consumed_at_least_one_kv = true;

        return true;
    };

    auto do_depth_transition = [&]() -> bool {
        if (!s.characters_per_level)
            return true;

        // Unaligned to whitespace per level
        if (s.current_whitespace_depth % s.characters_per_level)
            return false;

        auto base_is_nonzero = s.base_depth_is_nonzero.value_or(false) && s.within_loadable_entry;
        auto next_depth = s.current_whitespace_depth / s.characters_per_level;

        auto must_be_zero = !(s.expecting_depth_plus_one || s.current_depth || base_is_nonzero);

        // Expected zero but got something else
        if (must_be_zero && next_depth)
            return false;

        // Went too deep
        if (next_depth > s.current_depth && ((next_depth - s.current_depth) > 1))
            return false;

        // Empty object
        if (s.expecting_depth_plus_one && (next_depth != (s.current_depth + 1)))
            return false;

        // If our depth is now less than what it was before close all nested
        // objects that are still open.
        while (s.current_depth > next_depth)
            s.depth_to_offset[s.current_depth-- + !base_is_nonzero].reset();

        s.current_depth = next_depth;
        return true;
    };

    auto consume_character = [&](const char& c) {
        if (s.consumed_at_least_one)
            s.current_value_view.extend_by(1);
        else
            s.current_value_view = { &c, 1 };

        s.consumed_at_least_one = true;
    };

    for (const char& c : config) {
        s.line_offset++;
        s.global_offset++;

        if (is(State::COMMENT) && c != '\n')
            continue;

        switch (c) {
        case ' ':
        case '\t':
            if (is(State::NORMAL)) {
                if (s.whitespace_character && (s.whitespace_character != c))
                    PARSE_ERROR("mixed tabs and spaces are ambiguous");

                s.whitespace_character = c;
                s.current_whitespace_depth++;
                continue;
            }

            if (is(State::KEY)) {
                s.expecting_end_of_value = s.consumed_at_least_one;
                continue;
            }

            if (is(State::VALUE)) {
                if (!s.open_quote_character) {
                    s.expecting_end_of_value = s.consumed_at_least_one;
                    continue;
                }

                consume_character(c);
                continue;
            }

            if (s.expecting_end_of_value)
                continue;

            PARSE_ERROR("invalid character");
        case '\r':
            if (is(State::NORMAL) || is(State::VALUE))
                continue;

            PARSE_ERROR("invalid character");

        case '\n':
            s.file_line++;
            s.line_offset = 0;

            if (s.characters_per_level == 0)
                s.whitespace_character = 0;

            s.current_whitespace_depth = 0;
            s.expecting_end_of_value = false;

            if (is(State::NORMAL))
                continue;

            if (is(State::VALUE)) {
                if (!finalize_key_value())
                    return false;

                set(State::NORMAL);
                continue;
            }

            if (is(State::COMMENT)) {
                set(State::NORMAL);
                continue;
            }

            PARSE_ERROR("invalid character");

        case '=':
            if (is(State::NORMAL) || (is(State::VALUE) && !s.open_quote_character))
                PARSE_ERROR("invalid character");

            if (is(State::KEY)) {
                set(State::VALUE);
                continue;
            }

            s.current_value_view.extend_by(1);
            continue;

        case ':':
            if (is(State::NORMAL))
                PARSE_ERROR("invalid character");

            if (is(State::KEY)) {
                if (!finalize_key_value(true))
                    return false;

                set(State::NORMAL);
                s.expecting_end_of_value = true;
                continue;
            }

            if (is(State::VALUE) && !s.open_quote_character)
                PARSE_ERROR("invalid character");

            s.current_value_view.extend_by(1);
            continue;

        case '"':
        case '\'':
            if (!is(State::VALUE) || (!s.open_quote_character && s.consumed_at_least_one))
                PARSE_ERROR("invalid character");

            if (s.open_quote_character) {
                if (s.open_quote_character != c) {
                    consume_character(c);
                    continue;
                }

                if (!finalize_key_value())
                    return false;

                set(State::NORMAL);
                s.expecting_end_of_value = true;
                continue;
            }

            s.open_quote_character = c;
            continue;

        case '[':
            if (s.current_whitespace_depth)
                PARSE_ERROR("loadable entry title must start on a new line");

            if (is(State::NORMAL)) {
                if (s.expecting_depth_plus_one)
                    PARSE_ERROR("empty objects are not allowed");

                if (s.within_loadable_entry && !s.consumed_at_least_one_kv)
                    PARSE_ERROR("empty loadable entries are not allowed");

                set(State::LOADABLE_ENTRY_TITLE);
                continue;
            }

            if (is(State::VALUE) && s.open_quote_character) {
                s.current_value_view.extend_by(1);
                continue;
            }

            PARSE_ERROR("invalid character");
        case ']':
            if (is(State::LOADABLE_ENTRY_TITLE)) {
                s.current.type = ConfigEntry::LOADABLE_ENTRY;
                s.current.key = s.current_value_view;
                s.current.data.as_offset_to_next_loadable_entry = 0;

                bool ok = false;
                auto offset = emplace_entry(s.current, ok);
                if (!ok)
                    PARSE_ERROR("out of memory");

                if (!m_first_loadable_entry_offset)
                    m_first_loadable_entry_offset = offset;

                if (m_last_loadable_entry_offset) {
                    auto prev = m_last_loadable_entry_offset.value();
                    auto& entry = m_buffer[prev];
                    entry.data.as_offset_to_next_loadable_entry = offset - prev;
                }

                auto previous_offset = s.depth_to_offset[0];
                if (previous_offset) {
                    auto& entry = m_buffer[previous_offset.value()];
                    entry.offset_to_next_within_same_scope = offset - previous_offset.value();
                }
                s.depth_to_offset[0] = offset;

                m_last_loadable_entry_offset = offset;
                set(State::NORMAL);
                s.expecting_end_of_value = true;
                continue;
            }

            if (is(State::VALUE) && s.open_quote_character) {
                s.current_value_view.extend_by(1);
                continue;
            }

            PARSE_ERROR("invalid character");

        case '#':
            if (is(State::KEY) || is(State::LOADABLE_ENTRY_TITLE))
                PARSE_ERROR("invalid character");

            if (is(State::VALUE) && s.open_quote_character) {
                s.current_value_view.extend_by(1);
                continue;
            }

            s.expecting_end_of_value = false;
            set(State::COMMENT);
            continue;

        default:
            if (c <= 32 || c >= 127)
                PARSE_ERROR("invalid character");

            if (s.expecting_end_of_value)
                PARSE_ERROR("unexpected character");

            if (s.state == State::NORMAL) {
                if (s.current_whitespace_depth && !s.characters_per_level)
                    s.characters_per_level = s.current_whitespace_depth;

                if (!s.base_depth_is_nonzero.has_value() && s.within_loadable_entry)
                    s.base_depth_is_nonzero = s.current_whitespace_depth != 0;

                if (!do_depth_transition())
                    PARSE_ERROR("invalid number of whitespace");

                set(State::KEY);
                s.current.key = StringView(&c, 1);
                s.consumed_at_least_one = true;

                continue;
            }

            if (is(State::KEY)) {
                s.current.key.extend_by(1);
                continue;
            }

            if (s.expecting_end_of_value)
                PARSE_ERROR("invalid character");

            if (is(State::VALUE) || is(State::LOADABLE_ENTRY_TITLE)) {
                consume_character(c);
                continue;
            }

            PARSE_ERROR("invalid character");
        }
    }

    if (is(State::VALUE))
        return finalize_key_value();

    if (s.expecting_depth_plus_one || (s.within_loadable_entry && !s.consumed_at_least_one_kv))
        PARSE_ERROR("early EOF");

    if (is(State::COMMENT))
        return true;

    if (!is(State::NORMAL))
        PARSE_ERROR("early EOF");

    return true;
}

bool Config::try_parse_as_number(StringView string, Value& out)
{
    bool negative = false;
    bool ok = false;

    if (string.starts_with("-")) {
        string.offset_by(1);
        negative = true;
    } else if (string.starts_with("+")) {
        string.offset_by(1);
    }

    if (string.starts_with("0x")) {
        string.offset_by(2);
        if (negative) {
            out = from_hex_string<i64>(string, ok, negative);
        } else {
            out = from_hex_string<u64>(string, ok, negative);
        }
    } else if (string.starts_with("0")) {
        string.offset_by(1);
        if (negative) {
            out = from_octal_string<i64>(string, ok, negative);
        } else {
            out = from_octal_string<u64>(string, ok, negative);
        }
    } else {
        if (negative) {
            out = from_dec_string<i64>(string, ok, negative);
        } else {
            out = from_dec_string<u64>(string, ok, negative);
        }
    }

    return ok;
}

size_t Config::emplace_entry(ConfigEntry entry, bool& ok)
{
    ok = true;

    if (m_size == m_capacity) {
        auto old_capacity = m_capacity;
        m_capacity = max(old_capacity * 2, 4096 / sizeof(ConfigEntry));
        auto* new_buffer = allocator::allocate_new_array<ConfigEntry>(m_capacity);
        if (!new_buffer) {
            ok = false;
            return 0;
        }

        copy_memory(m_buffer, new_buffer, sizeof(ConfigEntry) * m_size);
        allocator::free_array(m_buffer, old_capacity);
        m_buffer = new_buffer;
    }

    size_t offset = m_size++;
    auto& buf_entry = m_buffer[offset] = entry;
    if (buf_entry.type == ConfigEntry::VALUE && buf_entry.data.as_value.type() == Value::OBJECT)
        buf_entry.data.as_value.set_object_offset(offset + 1);

    return offset;
}

LoadableEntry IterableLoadableEntries::operator*()
{
    auto& as_config_entry = m_config->loadable_entry_at_offset(m_offset);

    return { m_config, as_config_entry.key, m_offset };
}

IterableLoadableEntries& IterableLoadableEntries::operator++()
{
    auto offset_to_next = m_config->loadable_entry_at_offset(m_offset).data.as_offset_to_next_loadable_entry;

    if (offset_to_next)
        m_offset += offset_to_next;
    else
        m_offset = 0;

    return *this;
}

IterableKeyValuePairs LoadableEntry::begin()
{
    return m_config->get_all_for_loadable_entry_at(m_offset);
}

IterableKeyValuePairs LoadableEntry::end()
{
    return { m_config, 0 };
}

IterableKeyValuePairs Value::begin()
{
    ASSERT(m_type == OBJECT);
    return m_value.as_object.config->get_all_for_value(m_value.as_object.offset_within_config);
}

IterableKeyValuePairs Value::end()
{
    ASSERT(m_type == OBJECT);
    return { m_value.as_object.config, 0 };
}

KeyValue IterableKeyValueBase::operator*()
{
    auto& as_config_entry = m_config->value_at_offset(m_offset);

    return { as_config_entry.key, as_config_entry.data.as_value };
}

IterableKeyValuePairs& IterableKeyValuePairs::operator++()
{
    ASSERT(m_offset != 0);
    bool first = true;

    for (;;) {
        auto value = m_config->value_at_offset(m_offset);
        if (!first && (value.type == ConfigEntry::VALUE))
            break;

        if (value.offset_to_next_within_same_scope) {
            first = false;
            m_offset += value.offset_to_next_within_same_scope;
        } else {
            m_offset = 0;
            break;
        }
    }

    return *this;
}

IterableDuplicateKeyValuePairs& IterableDuplicateKeyValuePairs::operator++()
{
    ASSERT(m_offset != 0);
    bool first = true;

    for (;;) {
        auto value = m_config->any_at_offset(m_offset);
        if (!first && (value.type == ConfigEntry::VALUE) && (value.key == m_key))
            break;

        if (value.offset_to_next_within_same_scope) {
            first = false;
            m_offset += value.offset_to_next_within_same_scope;
        } else {
            m_offset = 0;
            break;
        }
    }

    return *this;
}

IterableDuplicateKeyValuePairs Value::get_all(StringView key)
{
    ASSERT(m_type == OBJECT);
    return m_value.as_object.config->get_all(m_value.as_object.offset_within_config, key);
}

Optional<Value> Value::get(StringView key, MustBeUnique must_be_unique)
{
    ASSERT(m_type == OBJECT);
    return m_value.as_object.config->get(m_value.as_object.offset_within_config, key, must_be_unique);
}

Optional<Value> Value::get_last(StringView key)
{
    ASSERT(m_type == OBJECT);
    return m_value.as_object.config->get_last(m_value.as_object.offset_within_config, key);
}

bool Value::contains(StringView key) const
{
    ASSERT(m_type == OBJECT);
    return m_value.as_object.config->contains(m_value.as_object.offset_within_config, key);
}

Optional<Value> LoadableEntry::get(StringView key, MustBeUnique must_be_unique)
{
    return m_config->get(m_offset, key, must_be_unique);
}

IterableDuplicateKeyValuePairs LoadableEntry::get_all(StringView key)
{
    return m_config->get_all(m_offset, key);
}

Optional<Value> LoadableEntry::get_last(StringView key)
{
    return m_config->get_last(m_offset, key);
}

bool LoadableEntry::contains(StringView key) const
{
    return m_config->contains(m_offset, key);
}
