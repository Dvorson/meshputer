#pragma once

// Minimal JSON: a parser into a small tree, and NIP-01 compliant string quoting
// (Nostr event ids hash an exact serialization, so escaping must match the spec).

#include <cstdint>
#include <string>
#include <vector>

namespace Json {

struct Value {
    enum class Type : uint8_t { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool boolean = false;
    std::string str;                  // String contents, or a Number's raw text
    std::vector<Value> items;         // Array elements, or Object values
    std::vector<std::string> keys;    // Object keys (parallel to items)

    bool isString() const { return type == Type::String; }
    bool isNumber() const { return type == Type::Number; }
    bool isArray() const { return type == Type::Array; }
    bool isObject() const { return type == Type::Object; }
    const Value* get(const std::string& key) const;  // object member or nullptr
    int64_t asInt(int64_t fallback = 0) const;
};

bool parse(const std::string& text, Value& out);

// "..." with the NIP-01 escapes: \" \\ \n \r \t \b \f; everything else verbatim.
std::string quote(const std::string& s);

// Compact serialization (no whitespace), strings quoted as above.
std::string stringify(const Value& v);

} // namespace Json
