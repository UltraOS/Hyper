#pragma once

#include "Common/Logger.h"
#include "Common/StringView.h"
#include "Common/Optional.h"
#include "Common/Panic.h"

class Config;
class Value;

struct KeyValue {
    StringView key;
    Value& value;
};

class IterableKeyValueBase {
public:
    IterableKeyValueBase(Config* config, size_t offset)
        : m_config(config)
        , m_offset(offset)
    {
    }

    KeyValue operator*();

protected:
    Config* m_config { nullptr };
    size_t m_offset { 0 };
};

class IterableKeyValuePairs : public IterableKeyValueBase {
public:
    IterableKeyValuePairs(Config* config, size_t offset)
        : IterableKeyValueBase(config, offset)
    {
    }

    IterableKeyValuePairs begin() { return { m_config, m_offset }; }
    IterableKeyValuePairs end() { return { m_config, 0 }; }

    bool operator==(const IterableKeyValuePairs& other)
    {
        ASSERT(m_config == other.m_config);
        return m_offset == other.m_offset;
    }

    bool operator!=(const IterableKeyValuePairs& other)
    {
        return !operator==(other);
    }

    IterableKeyValuePairs& operator++();
};

class IterableDuplicateKeyValuePairs : public IterableKeyValueBase {
public:
    IterableDuplicateKeyValuePairs(Config* config, size_t offset, StringView key)
        : IterableKeyValueBase(config, offset)
        , m_key(key)
    {
    }

    IterableDuplicateKeyValuePairs begin() { return { m_config, m_offset, m_key }; }
    IterableDuplicateKeyValuePairs end() { return { m_config, 0, m_key }; }

    bool operator==(const IterableDuplicateKeyValuePairs& other)
    {
        ASSERT(m_config == other.m_config && m_key == other.m_key);
        return m_offset == other.m_offset;
    }

    bool operator!=(const IterableDuplicateKeyValuePairs& other)
    {
        return !operator==(other);
    }

    IterableDuplicateKeyValuePairs& operator++();

private:
    StringView m_key;
};

enum class MustBeUnique {
    YES,
    NO
};

class LoadableEntry {
public:
    LoadableEntry() = default;

    IterableKeyValuePairs begin();
    IterableKeyValuePairs end();

    [[nodiscard]] Optional<Value> get(StringView key, MustBeUnique = MustBeUnique::YES);
    [[nodiscard]] IterableDuplicateKeyValuePairs get_all(StringView key);
    [[nodiscard]] Optional<Value> get_last(StringView key);
    [[nodiscard]] bool contains(StringView key) const;

    StringView name() { return m_name; }

private:
    friend class IterableLoadableEntries;

    LoadableEntry(Config* config, StringView name, size_t offset)
        : m_config(config)
        , m_name(name)
        , m_offset(offset)
    {
    }

private:
    Config* m_config { nullptr };
    StringView m_name;
    size_t m_offset { 0 };
};


class IterableLoadableEntries {
public:
    IterableLoadableEntries begin() { return { m_config, m_offset }; }
    IterableLoadableEntries end() { return { m_config, 0 }; }

    LoadableEntry operator*();

    bool operator==(const IterableLoadableEntries& other)
    {
        ASSERT(m_config == other.m_config);
        return m_offset == other.m_offset;
    }

    bool operator!=(const IterableLoadableEntries& other)
    {
        return !operator==(other);
    }

    IterableLoadableEntries& operator++();

private:
    friend class Config;

    IterableLoadableEntries(Config* config, size_t offset)
        : m_config(config)
        , m_offset(offset)
    {
    }

private:
    Config* m_config { nullptr };
    size_t m_offset { 0 };
};

class Value {
public:
    enum Type : uint8_t {
        NONE,
        BOOLEAN,
        UNSIGNED,
        SIGNED,
        STRING,
        OBJECT,
    };

    static StringView type_as_string(Type t)
    {
        switch (t) {
        case NONE:
            return "None";
        case BOOLEAN:
            return "Boolean";
        case UNSIGNED:
            return "Unsigned integer";
        case SIGNED:
            return "Signed integer";
        case STRING:
            return "String";
        case OBJECT:
            return "Object";
        }

        return "<Invalid>";
    }

    Value()
        : m_type(NONE)
    {
    }

    Value(u64 value)
        : m_type(UNSIGNED)
    {
        m_value.as_unsigned = value;
    }

    Value(i64 value)
        : m_type(SIGNED)
    {
        m_value.as_signed = value;
    }

    Value(StringView value)
        : m_type(STRING)
    {
        m_value.as_string = value;
    }

    Value(bool value)
        : m_type(BOOLEAN)
    {
        m_value.as_bool = value;
    }

    [[nodiscard]] bool is_null() const { return m_type == NONE; }
    operator bool() const { return !is_null(); }

    [[nodiscard]] Type type() const { return m_type; }
    [[nodiscard]] bool is_bool() const { return m_type == BOOLEAN; }
    [[nodiscard]] bool is_unsigned() const { return m_type == UNSIGNED; }
    [[nodiscard]] bool is_signed() const { return m_type == SIGNED; }
    [[nodiscard]] bool is_string() const { return m_type == STRING; }
    [[nodiscard]] bool is_object() const { return m_type == OBJECT; }

    [[nodiscard]] u64 as_unsigned() const
    {
        ASSERT(type() == UNSIGNED);
        return m_value.as_unsigned;
    }

    [[nodiscard]] i64 as_signed() const
    {
        ASSERT(type() == SIGNED);
        return m_value.as_signed;
    }

    [[nodiscard]] StringView as_string() const
    {
        ASSERT(type() == STRING);
        return m_value.as_string;
    }

    [[nodiscard]] bool as_bool() const
    {
        ASSERT(type() == BOOLEAN);
        return m_value.as_bool;
    }

    [[nodiscard]] Optional<Value> get(StringView key, MustBeUnique = MustBeUnique::YES);
    [[nodiscard]] IterableDuplicateKeyValuePairs get_all(StringView key);
    [[nodiscard]] Optional<Value> get_last(StringView key);
    [[nodiscard]] bool contains(StringView key) const;

    IterableKeyValuePairs begin();
    IterableKeyValuePairs end();

private:
    friend class Config;

    Value(Config* config)
        : m_type(OBJECT)
    {
        m_value.as_object = { config, 0 };
    }
    void set_object_offset(size_t offset) { m_value.as_object.offset_within_config = offset; }

private:
    Type m_type { NONE };

    union {
        bool as_bool;
        u64 as_unsigned;
        i64 as_signed;
        StringView as_string;
        struct {
            Config* config;
            size_t offset_within_config;
        } as_object;
    } m_value {};
};

struct ConfigEntry {
    StringView key;

    enum Type {
        NONE,
        VALUE,
        LOADABLE_ENTRY
    } type { NONE };

    union {
        Value as_value;
        size_t as_offset_to_next_loadable_entry; // 0 -> this is the last entry
    } data {};

    size_t offset_to_next_within_same_scope; // 0 -> this is the last entry
};

class Config {
public:
    [[nodiscard]] bool parse(StringView config);

    [[nodiscard]] Optional<LoadableEntry> get_loadable_entry(StringView name)
    {
        for (auto entry : loadable_entries()) {
            if (entry.name() == name)
                return entry;
        }

        return {};
    }

    // Getters for globals
    [[nodiscard]] Optional<Value> get(StringView key, MustBeUnique must_be_unique = MustBeUnique::YES)
    {
        return get(0, key, must_be_unique);
    }

    [[nodiscard]] Optional<Value> get_last(StringView key)
    {
        return get_last(0, key);
    }

    [[nodiscard]] IterableDuplicateKeyValuePairs get_all(StringView key)
    {
        return get_all(0, key);
    }

    [[nodiscard]] IterableLoadableEntries loadable_entries()
    {
        if (m_first_loadable_entry_offset.has_value())
            return { this, m_first_loadable_entry_offset.value() + 1 };

        return { this, 0 };
    }

    struct Error {
        StringView message;
        size_t line;
        size_t offset;
        size_t global_offset;
    };
    Error last_error() { return m_error; }

private:
    friend class IterableLoadableEntries;
    friend class IterableKeyValueBase;
    friend class IterableKeyValuePairs;
    friend class IterableDuplicateKeyValuePairs;
    friend class LoadableEntry;
    friend class Value;

    struct FindResult {
        size_t first_occurence;
        size_t last_occurence;
        size_t count;
    };

    FindResult find(size_t offset, StringView key, size_t constraint_max = 0) const;

    [[nodiscard]] bool contains(size_t offset_to_scope, StringView key) const
    {
        return find(offset_to_scope, key, 1).count > 0;
    }

    [[nodiscard]] Optional<Value> get(size_t offset, StringView key, MustBeUnique must_be_unqiue = MustBeUnique::YES)
    {
        auto result = find(offset, key, 2);

        if (!result.count)
            return {};

        if (result.count > 1 && must_be_unqiue == MustBeUnique::YES)
            unrecoverable_error("Key {} must be unique", key);

        return m_buffer[result.first_occurence].data.as_value;
    }

    [[nodiscard]] Optional<Value> get_last(size_t offset, StringView key)
    {
        auto result = find(offset, key);

        if (!result.count)
            return {};

        return m_buffer[result.last_occurence].data.as_value;
    }

    [[nodiscard]] IterableDuplicateKeyValuePairs get_all(size_t offset, StringView key)
    {
        auto result = find(offset, key, 1);

        if (!result.count)
            return { this, 0, key };

       return { this, result.first_occurence + 1, key };
    }

    [[nodiscard]] ConfigEntry& safe_entry(size_t offset, ConfigEntry::Type expected_type)
    {
        ASSERT(offset && offset <= m_size);
        auto& entry = m_buffer[offset - 1];
        ASSERT(entry.type == expected_type);

        return entry;
    }

    [[nodiscard]] IterableKeyValuePairs get_all_for_loadable_entry_at(size_t offset)
    {
        // TODO: make sure there are no empty loadable entries
        return { this, offset + 1 };
    }

    [[nodiscard]] IterableKeyValuePairs get_all_for_value(size_t offset)
    {
        return { this, offset + 1 };
    }

    [[nodiscard]] ConfigEntry& any_at_offset(size_t offset)
    {
        ASSERT(offset && offset <= m_size);
        return m_buffer[offset - 1];
    }

    [[nodiscard]] ConfigEntry& loadable_entry_at_offset(size_t offset)
    {
        return safe_entry(offset, ConfigEntry::LOADABLE_ENTRY);
    }

    [[nodiscard]] ConfigEntry& value_at_offset(size_t offset)
    {
        return safe_entry(offset, ConfigEntry::VALUE);
    }

    size_t emplace_entry(ConfigEntry, bool& ok);
    bool try_parse_as_number(StringView, Value& out);

public:
    Error m_error;
    Optional<size_t> m_first_loadable_entry_offset;
    Optional<size_t> m_last_loadable_entry_offset;

    ConfigEntry* m_buffer { nullptr };
    size_t m_capacity { 0 };
    size_t m_size { 0 };
};

void pretty_print_error(const Config::Error&, StringView config);

[[noreturn]] inline void panic_on_unexpected_type(StringView key, Value::Type expected, Value::Type got)
{
    panic("Unexpected type of \"{}\", expected {} got {}", key,
          Value::type_as_string(expected), Value::type_as_string(got));
}

inline bool extract_boolean(KeyValue kv)
{
    if (!kv.value.is_bool())
        panic_on_unexpected_type(kv.key, Value::BOOLEAN, kv.value.type());

    return kv.value.as_bool();
}

inline StringView extract_string(KeyValue kv)
{
    if (!kv.value.is_string())
        panic_on_unexpected_type(kv.key, Value::STRING, kv.value.type());

    return kv.value.as_string();
}

inline u64 extract_unsigned(KeyValue kv)
{
    if (!kv.value.is_unsigned())
        panic_on_unexpected_type(kv.key, Value::UNSIGNED, kv.value.type());

    return kv.value.as_unsigned();
}

inline i64 extract_signed(KeyValue kv)
{
    if (!kv.value.is_signed())
        panic_on_unexpected_type(kv.key, Value::SIGNED, kv.value.type());

    return kv.value.as_signed();
}

inline void ensure_is_object(KeyValue kv)
{
    if (!kv.value.is_object())
        panic_on_unexpected_type(kv.key, Value::OBJECT, kv.value.type());
}
