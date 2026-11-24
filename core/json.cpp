#include "json.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace hunter {

namespace {
const Json kNull;
const Json::Array kEmptyArray;
const Json::Object kEmptyObject;

struct Parser {
    const std::string& s;
    size_t i = 0;

    [[noreturn]] void fail(const char* what) const {
        throw std::runtime_error(std::string("JSON: ") + what + " at offset " + std::to_string(i));
    }
    void ws() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
    }
    bool eat(char c) {
        ws();
        if (i < s.size() && s[i] == c) { ++i; return true; }
        return false;
    }
    static void utf8(std::string& out, unsigned cp) {
        if (cp < 0x80) out += static_cast<char>(cp);
        else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18)); out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    unsigned hex4() {
        if (i + 4 > s.size()) fail("bad \\u escape");
        unsigned v = 0;
        for (int k = 0; k < 4; ++k) {
            char c = s[i++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= c - '0';
            else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
            else fail("bad hex digit");
        }
        return v;
    }
    std::string str() {
        if (!eat('"')) fail("expected string");
        std::string out;
        while (true) {
            if (i >= s.size()) fail("unterminated string");
            char c = s[i++];
            if (c == '"') break;
            if (c != '\\') { out += c; continue; }
            if (i >= s.size()) fail("bad escape");
            char e = s[i++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    unsigned cp = hex4();
                    if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 <= s.size() && s[i] == '\\' && s[i + 1] == 'u') {
                        i += 2;
                        unsigned lo = hex4();
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    utf8(out, cp);
                    break;
                }
                default: fail("bad escape");
            }
        }
        return out;
    }
    Json value(int depth = 0) {
        if (depth > 200) fail("nested too deeply");
        ws();
        if (i >= s.size()) fail("unexpected end");
        char c = s[i];
        if (c == '{') {
            ++i;
            Json::Object o;
            if (eat('}')) return Json(std::move(o));
            do {
                ws();
                std::string k = str();
                if (!eat(':')) fail("expected ':'");
                o.emplace_back(std::move(k), value(depth + 1));
            } while (eat(','));
            if (!eat('}')) fail("expected '}'");
            return Json(std::move(o));
        }
        if (c == '[') {
            ++i;
            Json::Array a;
            if (eat(']')) return Json(std::move(a));
            do { a.push_back(value(depth + 1)); } while (eat(','));
            if (!eat(']')) fail("expected ']'");
            return Json(std::move(a));
        }
        if (c == '"') return Json(str());
        if (s.compare(i, 4, "true") == 0) { i += 4; return Json(true); }
        if (s.compare(i, 5, "false") == 0) { i += 5; return Json(false); }
        if (s.compare(i, 4, "null") == 0) { i += 4; return Json(); }
        if (c == '-' || (c >= '0' && c <= '9')) {
            size_t start = i;
            ++i;
            while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.' || s[i] == 'e' ||
                                    s[i] == 'E' || s[i] == '+' || s[i] == '-')) ++i;
            return Json(std::strtod(s.substr(start, i - start).c_str(), nullptr));
        }
        fail("unexpected character");
    }
};

void writeString(std::string& out, const std::string& s) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);  // UTF-8 passes through unchanged
                }
        }
    }
    out += '"';
}
}  // namespace

Json Json::parse(const std::string& text) {
    Parser p{text};
    Json v = p.value();
    p.ws();
    if (p.i != text.size()) p.fail("trailing characters");
    return v;
}

Json& Json::operator=(const Json& o) {
    if (this == &o) return *this;
    type_ = o.type_; b_ = o.b_; n_ = o.n_; s_ = o.s_;
    a_ = o.a_ ? std::make_shared<Array>(*o.a_) : nullptr;
    o_ = o.o_ ? std::make_shared<Object>(*o.o_) : nullptr;
    return *this;
}

bool Json::toBool(bool fallback) const {
    if (type_ == Type::Bool) return b_;
    if (type_ == Type::Number) return n_ != 0;
    if (type_ == Type::String) return s_ == "true" || s_ == "1" || s_ == "yes";
    return fallback;
}
double Json::toDouble(double fallback) const {
    if (type_ == Type::Number) return n_;
    if (type_ == Type::String && !s_.empty()) {
        char* end = nullptr;
        double v = std::strtod(s_.c_str(), &end);
        if (end && *end == '\0') return v;
    }
    if (type_ == Type::Bool) return b_ ? 1 : 0;
    return fallback;
}
long long Json::toInt(long long fallback) const {
    if (type_ == Type::Number || type_ == Type::String || type_ == Type::Bool) {
        double d = toDouble(static_cast<double>(fallback));
        return static_cast<long long>(std::llround(d));
    }
    return fallback;
}
std::string Json::toString(const std::string& fallback) const {
    if (type_ == Type::String) return s_;
    if (type_ == Type::Number) {
        if (std::floor(n_) == n_ && std::fabs(n_) < 1e15) return std::to_string(static_cast<long long>(n_));
        return dump();
    }
    if (type_ == Type::Bool) return b_ ? "true" : "false";
    return fallback;
}

const Json& Json::get(const std::string& key) const {
    if (type_ != Type::Object) return kNull;
    for (const auto& [k, v] : *o_) if (k == key) return v;
    return kNull;
}
bool Json::contains(const std::string& key) const {
    if (type_ != Type::Object) return false;
    for (const auto& kv : *o_) if (kv.first == key) return true;
    return false;
}
void Json::ensureObject() {
    if (type_ != Type::Object) { type_ = Type::Object; o_ = std::make_shared<Object>(); }
}
void Json::ensureArray() {
    if (type_ != Type::Array) { type_ = Type::Array; a_ = std::make_shared<Array>(); }
}
Json& Json::operator[](const std::string& key) {
    ensureObject();
    for (auto& [k, v] : *o_) if (k == key) return v;
    o_->emplace_back(key, Json());
    return o_->back().second;
}
void Json::erase(const std::string& key) {
    if (type_ != Type::Object) return;
    for (auto it = o_->begin(); it != o_->end(); ++it)
        if (it->first == key) { o_->erase(it); return; }
}
const Json::Object& Json::items() const { return type_ == Type::Object ? *o_ : kEmptyObject; }
Json::Object& Json::items() { ensureObject(); return *o_; }
const Json::Array& Json::elements() const { return type_ == Type::Array ? *a_ : kEmptyArray; }
Json::Array& Json::elements() { ensureArray(); return *a_; }
void Json::push(Json v) { ensureArray(); a_->push_back(std::move(v)); }
size_t Json::size() const {
    if (type_ == Type::Array) return a_->size();
    if (type_ == Type::Object) return o_->size();
    return 0;
}

bool Json::operator==(const Json& o) const {
    if (type_ != o.type_) return false;
    switch (type_) {
        case Type::Null: return true;
        case Type::Bool: return b_ == o.b_;
        case Type::Number: return n_ == o.n_;
        case Type::String: return s_ == o.s_;
        case Type::Array: return *a_ == *o.a_;
        case Type::Object: return *o_ == *o.o_;
    }
    return false;
}

std::string Json::dump(int indent) const {
    std::string out;
    write(out, indent, 0);
    return out;
}

void Json::write(std::string& out, int indent, int depth) const {
    auto nl = [&](int d) {
        if (indent < 0) return;
        out += '\n';
        out.append(static_cast<size_t>(indent * d), ' ');
    };
    switch (type_) {
        case Type::Null: out += "null"; break;
        case Type::Bool: out += b_ ? "true" : "false"; break;
        case Type::Number: {
            if (std::floor(n_) == n_ && std::fabs(n_) < 1e15) out += std::to_string(static_cast<long long>(n_));
            else { char buf[32]; std::snprintf(buf, sizeof buf, "%.15g", n_); out += buf; }
            break;
        }
        case Type::String: writeString(out, s_); break;
        case Type::Array:
            if (a_->empty()) { out += "[]"; break; }
            out += '[';
            for (size_t k = 0; k < a_->size(); ++k) {
                if (k) out += ',';
                nl(depth + 1);
                (*a_)[k].write(out, indent, depth + 1);
            }
            nl(depth);
            out += ']';
            break;
        case Type::Object:
            if (o_->empty()) { out += "{}"; break; }
            out += '{';
            for (size_t k = 0; k < o_->size(); ++k) {
                if (k) out += ',';
                nl(depth + 1);
                writeString(out, (*o_)[k].first);
                out += indent < 0 ? ":" : ": ";
                (*o_)[k].second.write(out, indent, depth + 1);
            }
            nl(depth);
            out += '}';
            break;
    }
}

}  // namespace hunter
