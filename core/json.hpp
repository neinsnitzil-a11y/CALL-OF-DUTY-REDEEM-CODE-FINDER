// Small JSON value + parser + writer. Objects keep their key order so config.json stays readable
// when the app writes it back.
#pragma once
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace hunter {

class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };
    using Array = std::vector<Json>;
    using Object = std::vector<std::pair<std::string, Json>>;

    Json() = default;
    Json(std::nullptr_t) {}
    Json(bool b) : type_(Type::Bool), b_(b) {}
    Json(int v) : type_(Type::Number), n_(v) {}
    Json(long v) : type_(Type::Number), n_(static_cast<double>(v)) {}
    Json(long long v) : type_(Type::Number), n_(static_cast<double>(v)) {}
    Json(double v) : type_(Type::Number), n_(v) {}
    Json(const char* s) : type_(Type::String), s_(s) {}
    Json(std::string s) : type_(Type::String), s_(std::move(s)) {}
    Json(Array a) : type_(Type::Array), a_(std::make_shared<Array>(std::move(a))) {}
    Json(Object o) : type_(Type::Object), o_(std::make_shared<Object>(std::move(o))) {}

    static Json array() { return Json(Array{}); }
    static Json object() { return Json(Object{}); }
    static Json parse(const std::string& text);  // throws std::runtime_error

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isBool() const { return type_ == Type::Bool; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    // Lenient getters: wrong type → fallback (config files are edited by people).
    bool toBool(bool fallback = false) const;
    double toDouble(double fallback = 0) const;
    long long toInt(long long fallback = 0) const;
    std::string toString(const std::string& fallback = "") const;

    // Object access. get() never throws; operator[] creates the key (turning null into an object).
    const Json& get(const std::string& key) const;
    bool contains(const std::string& key) const;
    Json& operator[](const std::string& key);
    void erase(const std::string& key);
    const Object& items() const;
    Object& items();

    // Array access.
    const Array& elements() const;
    Array& elements();
    void push(Json v);
    size_t size() const;

    std::string dump(int indent = -1) const;  // -1 = compact
    bool operator==(const Json& other) const;

private:
    void write(std::string& out, int indent, int depth) const;
    void ensureObject();
    void ensureArray();
    Type type_ = Type::Null;
    bool b_ = false;
    double n_ = 0;
    std::string s_;
    std::shared_ptr<Array> a_;   // shared + copy-on-write would be overkill: copies deep-clone below
    std::shared_ptr<Object> o_;

public:
    Json(const Json& o) { *this = o; }
    Json& operator=(const Json& o);
    Json(Json&&) noexcept = default;
    Json& operator=(Json&&) noexcept = default;
};

// Shorthands used all over the core.
inline std::string jstr(const Json& j, const std::string& k, const std::string& d = "") { return j.get(k).toString(d); }
inline long long jinteger(const Json& j, const std::string& k, long long d = 0) { return j.get(k).toInt(d); }
inline double jnum(const Json& j, const std::string& k, double d = 0) { return j.get(k).toDouble(d); }
inline bool jbool(const Json& j, const std::string& k, bool d = false) { return j.get(k).toBool(d); }

}  // namespace hunter
