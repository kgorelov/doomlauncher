#ifndef TEMPLATE_ENGINE_H
#define TEMPLATE_ENGINE_H

#include <string>
#include <unordered_map>
#include <vector>

namespace doomlauncher {

struct Value;
using Array = std::vector<Value>;

struct Value {
    enum class Type { Null, String, Bool, Number, Array } type = Type::Null;

    std::string str_val;
    bool bool_val = false;
    double num_val = 0.0;
    Array arr_val;

    Value() : type(Type::Null) {}
    Value(const std::string& s) : type(Type::String), str_val(s) {}
    Value(const char* s) : type(Type::String), str_val(s) {}
    Value(bool b) : type(Type::Bool), bool_val(b) {}
    Value(int n) : type(Type::Number), num_val(n) {}
    Value(double n) : type(Type::Number), num_val(n) {}
    Value(Array a) : type(Type::Array), arr_val(std::move(a)) {}

    bool is_truthy() const {
        switch (type) {
            case Type::Null: return false;
            case Type::Bool: return bool_val;
            case Type::String: return !str_val.empty() && str_val != "false" && str_val != "0";
            case Type::Number: return num_val != 0.0;
            case Type::Array: return !arr_val.empty();
        }
        return false;
    }

    std::string to_string() const {
        switch (type) {
            case Type::Null: return "";
            case Type::String: return str_val;
            case Type::Bool: return bool_val ? "true" : "false";
            case Type::Number: {
                if (num_val == static_cast<long long>(num_val)) {
                    return std::to_string(static_cast<long long>(num_val));
                }
                return std::to_string(num_val);
            }
            case Type::Array: {
                std::string res;
                for (size_t i = 0; i < arr_val.size(); ++i) {
                    if (i > 0) res += " ";
                    res += arr_val[i].to_string();
                }
                return res;
            }
        }
        return "";
    }
};

using Scope = std::unordered_map<std::string, Value>;

class TemplateEngine {
public:
    static std::string render(const std::string& template_str, const Scope& scope);
};

} // namespace doomlauncher

#endif // TEMPLATE_ENGINE_H
