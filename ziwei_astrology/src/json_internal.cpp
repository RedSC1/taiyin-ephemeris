#include "json_internal.h"

#include <cmath>
#include <limits>
#include <locale>
#include <sstream>

namespace taiyin {
namespace ziwei {
namespace detail {

JsonValue::JsonValue() : type_(Null), boolean_(false), number_(0.0) {}
JsonValue::JsonValue(bool value)
    : type_(Boolean), boolean_(value), number_(0.0) {}
JsonValue::JsonValue(double value)
    : type_(Number), boolean_(false), number_(value) {}
JsonValue::JsonValue(const std::string& value)
    : type_(String), boolean_(false), number_(0.0), string_(value) {}
JsonValue::JsonValue(const ArrayValue& value)
    : type_(Array), boolean_(false), number_(0.0), array_(value) {}
JsonValue::JsonValue(const ObjectValue& value)
    : type_(Object), boolean_(false), number_(0.0), object_(value) {}

JsonValue::Type JsonValue::type() const noexcept { return type_; }
bool JsonValue::is_null() const noexcept { return type_ == Null; }
bool JsonValue::is_boolean() const noexcept { return type_ == Boolean; }
bool JsonValue::is_number() const noexcept { return type_ == Number; }
bool JsonValue::is_string() const noexcept { return type_ == String; }
bool JsonValue::is_array() const noexcept { return type_ == Array; }
bool JsonValue::is_object() const noexcept { return type_ == Object; }

bool JsonValue::boolean() const {
    if (!is_boolean()) throw JsonError("expected boolean");
    return boolean_;
}
double JsonValue::number() const {
    if (!is_number()) throw JsonError("expected number");
    return number_;
}
const std::string& JsonValue::string() const {
    if (!is_string()) throw JsonError("expected string");
    return string_;
}
const JsonValue::ArrayValue& JsonValue::array() const {
    if (!is_array()) throw JsonError("expected array");
    return array_;
}
const JsonValue::ObjectValue& JsonValue::object() const {
    if (!is_object()) throw JsonError("expected object");
    return object_;
}
bool JsonValue::has(const std::string& key) const {
    return is_object() && object_.find(key) != object_.end();
}
const JsonValue& JsonValue::at(const std::string& key) const {
    const ObjectValue::const_iterator found = object().find(key);
    if (found == object_.end()) throw JsonError("missing object key '" + key + "'");
    return found->second;
}

namespace {

const std::size_t kMaximumJsonNestingDepth = 128u;

void append_utf8(uint32_t codepoint, std::string* out) {
    if (codepoint <= 0x7fu) {
        out->push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7ffu) {
        out->push_back(static_cast<char>(0xc0u | (codepoint >> 6u)));
        out->push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
    } else if (codepoint <= 0xffffu) {
        out->push_back(static_cast<char>(0xe0u | (codepoint >> 12u)));
        out->push_back(static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3fu)));
        out->push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
    } else {
        out->push_back(static_cast<char>(0xf0u | (codepoint >> 18u)));
        out->push_back(static_cast<char>(0x80u | ((codepoint >> 12u) & 0x3fu)));
        out->push_back(static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3fu)));
        out->push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
    }
}

class Parser {
public:
    explicit Parser(const std::string& source) : source_(source), offset_(0u) {}

    JsonValue parse() {
        skip_space();
        JsonValue result = parse_value(0u);
        skip_space();
        if (offset_ != source_.size()) fail("unexpected trailing input");
        return result;
    }

private:
    void fail(const std::string& message) const {
        std::ostringstream text;
        text << "JSON byte " << offset_ << ": " << message;
        throw JsonError(text.str());
    }

    void skip_space() {
        while (offset_ < source_.size()) {
            const char value = source_[offset_];
            if (value != ' ' && value != '\t' && value != '\r' && value != '\n') break;
            ++offset_;
        }
    }

    bool consume(char value) {
        if (offset_ < source_.size() && source_[offset_] == value) {
            ++offset_;
            return true;
        }
        return false;
    }

    void expect_literal(const char* value) {
        for (const char* p = value; *p != '\0'; ++p) {
            if (offset_ >= source_.size() || source_[offset_++] != *p) {
                fail(std::string("expected '") + value + "'");
            }
        }
    }

    uint32_t hex4() {
        uint32_t result = 0u;
        for (int i = 0; i < 4; ++i) {
            if (offset_ >= source_.size()) fail("incomplete unicode escape");
            const char value = source_[offset_++];
            const int digit = value >= '0' && value <= '9' ? value - '0'
                : value >= 'a' && value <= 'f' ? value - 'a' + 10
                : value >= 'A' && value <= 'F' ? value - 'A' + 10 : -1;
            if (digit < 0) fail("invalid unicode escape");
            result = (result << 4u) | static_cast<uint32_t>(digit);
        }
        return result;
    }

    std::string parse_string() {
        if (!consume('"')) fail("expected string");
        std::string result;
        while (offset_ < source_.size()) {
            const unsigned char value = static_cast<unsigned char>(source_[offset_++]);
            if (value == '"') return result;
            if (value < 0x20u) fail("control character in string");
            if (value != '\\') {
                result.push_back(static_cast<char>(value));
                continue;
            }
            if (offset_ >= source_.size()) fail("incomplete escape");
            const char escaped = source_[offset_++];
            switch (escaped) {
            case '"': result.push_back('"'); break;
            case '\\': result.push_back('\\'); break;
            case '/': result.push_back('/'); break;
            case 'b': result.push_back('\b'); break;
            case 'f': result.push_back('\f'); break;
            case 'n': result.push_back('\n'); break;
            case 'r': result.push_back('\r'); break;
            case 't': result.push_back('\t'); break;
            case 'u': {
                uint32_t codepoint = hex4();
                if (codepoint >= 0xd800u && codepoint <= 0xdbffu) {
                    if (offset_ + 2u > source_.size()
                        || source_[offset_] != '\\' || source_[offset_ + 1u] != 'u') {
                        fail("high surrogate without low surrogate");
                    }
                    offset_ += 2u;
                    const uint32_t low = hex4();
                    if (low < 0xdc00u || low > 0xdfffu) fail("invalid low surrogate");
                    codepoint = 0x10000u + ((codepoint - 0xd800u) << 10u)
                        + (low - 0xdc00u);
                } else if (codepoint >= 0xdc00u && codepoint <= 0xdfffu) {
                    fail("unexpected low surrogate");
                }
                append_utf8(codepoint, &result);
                break;
            }
            default: fail("unknown escape");
            }
        }
        fail("unterminated string");
        return std::string();
    }

    JsonValue parse_number() {
        const std::size_t start = offset_;
        if (consume('-')) {}
        if (consume('0')) {
        } else {
            if (offset_ >= source_.size() || source_[offset_] < '1' || source_[offset_] > '9') {
                fail("invalid number");
            }
            while (offset_ < source_.size() && source_[offset_] >= '0'
                && source_[offset_] <= '9') ++offset_;
        }
        if (consume('.')) {
            const std::size_t digits = offset_;
            while (offset_ < source_.size() && source_[offset_] >= '0'
                && source_[offset_] <= '9') ++offset_;
            if (digits == offset_) fail("invalid fraction");
        }
        if (offset_ < source_.size()
            && (source_[offset_] == 'e' || source_[offset_] == 'E')) {
            ++offset_;
            if (offset_ < source_.size()
                && (source_[offset_] == '+' || source_[offset_] == '-')) ++offset_;
            const std::size_t digits = offset_;
            while (offset_ < source_.size() && source_[offset_] >= '0'
                && source_[offset_] <= '9') ++offset_;
            if (digits == offset_) fail("invalid exponent");
        }
        const std::string token = source_.substr(start, offset_ - start);
        std::istringstream input(token);
        input.imbue(std::locale::classic());
        double value = 0.0;
        input >> value;
        if (!input || input.peek() != std::char_traits<char>::eof()
            || !std::isfinite(value)) {
            fail("non-finite or invalid number");
        }
        return JsonValue(value);
    }

    JsonValue parse_array(std::size_t depth) {
        consume('[');
        skip_space();
        JsonValue::ArrayValue result;
        if (consume(']')) return JsonValue(result);
        for (;;) {
            skip_space();
            result.push_back(parse_value(depth + 1u));
            skip_space();
            if (consume(']')) return JsonValue(result);
            if (!consume(',')) fail("expected ',' or ']'");
        }
    }

    JsonValue parse_object(std::size_t depth) {
        consume('{');
        skip_space();
        JsonValue::ObjectValue result;
        if (consume('}')) return JsonValue(result);
        for (;;) {
            skip_space();
            const std::string key = parse_string();
            skip_space();
            if (!consume(':')) fail("expected ':'");
            skip_space();
            if (!result.insert(std::make_pair(
                    key, parse_value(depth + 1u))).second) {
                fail("duplicate object key '" + key + "'");
            }
            skip_space();
            if (consume('}')) return JsonValue(result);
            if (!consume(',')) fail("expected ',' or '}'");
        }
    }

    JsonValue parse_value(std::size_t depth) {
        if (depth > kMaximumJsonNestingDepth) {
            fail("maximum nesting depth exceeded");
        }
        if (offset_ >= source_.size()) fail("expected value");
        const char value = source_[offset_];
        if (value == '"') return JsonValue(parse_string());
        if (value == '{') return parse_object(depth);
        if (value == '[') return parse_array(depth);
        if (value == 't') { expect_literal("true"); return JsonValue(true); }
        if (value == 'f') { expect_literal("false"); return JsonValue(false); }
        if (value == 'n') { expect_literal("null"); return JsonValue(); }
        if (value == '-' || (value >= '0' && value <= '9')) return parse_number();
        fail("unexpected token");
        return JsonValue();
    }

    const std::string& source_;
    std::size_t offset_;
};

}  // namespace

JsonValue parse_json(const std::string& source) {
    return Parser(source).parse();
}

}  // namespace detail
}  // namespace ziwei
}  // namespace taiyin
