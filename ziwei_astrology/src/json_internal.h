#ifndef TAIYIN_ZIWEI_JSON_INTERNAL_H
#define TAIYIN_ZIWEI_JSON_INTERNAL_H

#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace taiyin {
namespace ziwei {
namespace detail {

class JsonError : public std::runtime_error {
public:
    explicit JsonError(const std::string& message) : std::runtime_error(message) {}
};

class JsonValue {
public:
    enum Type { Null, Boolean, Number, String, Array, Object };
    typedef std::vector<JsonValue> ArrayValue;
    typedef std::map<std::string, JsonValue> ObjectValue;

    JsonValue();
    explicit JsonValue(bool value);
    explicit JsonValue(double value);
    explicit JsonValue(const std::string& value);
    explicit JsonValue(const ArrayValue& value);
    explicit JsonValue(const ObjectValue& value);

    Type type() const noexcept;
    bool is_null() const noexcept;
    bool is_boolean() const noexcept;
    bool is_number() const noexcept;
    bool is_string() const noexcept;
    bool is_array() const noexcept;
    bool is_object() const noexcept;

    bool boolean() const;
    double number() const;
    const std::string& string() const;
    const ArrayValue& array() const;
    const ObjectValue& object() const;
    bool has(const std::string& key) const;
    const JsonValue& at(const std::string& key) const;

private:
    Type type_;
    bool boolean_;
    double number_;
    std::string string_;
    ArrayValue array_;
    ObjectValue object_;
};

JsonValue parse_json(const std::string& source);

}  // namespace detail
}  // namespace ziwei
}  // namespace taiyin

#endif  // TAIYIN_ZIWEI_JSON_INTERNAL_H
