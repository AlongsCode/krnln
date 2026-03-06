// ejson.hpp
#ifndef KRLN_EJSON_HPP
#define KRLN_EJSON_HPP
#include <iostream>
#include <variant>
#include <vector>
#include <unordered_map>
#include <string>
#include <string_view>
#include <stdexcept>
#include <sstream>
#include <cctype>
#include <initializer_list>
#include <cmath>

namespace krnln {

    class json_exception : public std::runtime_error {
    public:
        explicit json_exception(const std::string& msg) : std::runtime_error(msg) {}
    };

    class json {
    public:
        using array = std::vector<json>;
        using object = std::unordered_map<std::string, json>;
        using variant_t = std::variant<std::monostate, bool, int64_t, double, std::string, array, object>;

    private:
        variant_t value_;

        // --- Tokenizer ---
        enum class TokenType { LBrace, RBrace, LBracket, RBracket, Colon, Comma, String, Number, True, False, Null, End };
        struct Token { TokenType type; std::string text; };

        class Lexer {
            std::string_view src_;
            size_t idx_ = 0;
        public:
            explicit Lexer(std::string_view s) : src_(s) {}
            std::vector<Token> tokenize() {
                std::vector<Token> tokens;
                while (true) {
                    skipWS();
                    if (idx_ >= src_.size()) { tokens.push_back({ TokenType::End, {} }); break; }
                    char c = src_[idx_++];
                    switch (c) {
                    case '{': tokens.push_back({ TokenType::LBrace, {} }); break;
                    case '}': tokens.push_back({ TokenType::RBrace, {} }); break;
                    case '[': tokens.push_back({ TokenType::LBracket, {} }); break;
                    case ']': tokens.push_back({ TokenType::RBracket, {} }); break;
                    case ':': tokens.push_back({ TokenType::Colon, {} }); break;
                    case ',': tokens.push_back({ TokenType::Comma, {} }); break;
                    case '"':
                        tokens.push_back({ TokenType::String, parseString() });
                        break;
                    default:
                        if (isDigit(c) || c == '-') tokens.push_back({ TokenType::Number, parseNumber(c) });
                        else if (isAlpha(c)) tokens.push_back(parseLiteral(c));
                        else throw json_exception("Invalid character in JSON");
                    }
                }
                return tokens;
            }
        private:
            void skipWS() { while (idx_ < src_.size() && std::isspace((unsigned char)src_[idx_])) ++idx_; }
            static bool isDigit(char c) { return std::isdigit((unsigned char)c); }
            static bool isAlpha(char c) { return std::isalpha((unsigned char)c); }
            std::string parseString() {
                std::string out;
                while (idx_ < src_.size()) {
                    char c = src_[idx_++];
                    if (c == '"') break;
                    if (c == '\\' && idx_ < src_.size()) {
                        char e = src_[idx_++];
                        switch (e) {
                        case '"': out.push_back('"'); break;
                        case '\\': out.push_back('\\'); break;
                        case '/': out.push_back('/'); break;
                        case 'b': out.push_back('\b'); break;
                        case 'f': out.push_back('\f'); break;
                        case 'n': out.push_back('\n'); break;
                        case 'r': out.push_back('\r'); break;
                        case 't': out.push_back('\t'); break;
                        default: out.push_back(e);
                        }
                    }
                    else out.push_back(c);
                }
                return out;
            }
            std::string parseNumber(char first) {
                size_t start = idx_ - 1;
                while (idx_ < src_.size() && (isDigit(src_[idx_]) || src_[idx_] == '.' || src_[idx_] == 'e' || src_[idx_] == 'E' || src_[idx_] == '+' || src_[idx_] == '-')) ++idx_;
                return std::string(src_.substr(start, idx_ - start));
            }
            Token parseLiteral(char first) {
                size_t start = idx_ - 1;
                while (idx_ < src_.size() && isAlpha(src_[idx_])) ++idx_;
                std::string lit = std::string(src_.substr(start, idx_ - start));
                if (lit == "true") return { TokenType::True, {} };
                if (lit == "false") return { TokenType::False, {} };
                if (lit == "null") return { TokenType::Null, {} };
                throw json_exception("Unknown literal: " + lit);
            }
        };


        // --- Parser ---
        class Parser {
            const std::vector<Token>& tks_;
            size_t pos_ = 0;
        public:
            explicit Parser(const std::vector<Token>& toks) : tks_(toks) {}
            json parseValue() {
                const auto& tk = peek();
                switch (tk.type) {
                case TokenType::LBrace: return parseObject();
                case TokenType::LBracket: return parseArray();
                case TokenType::String: { auto s = tk.text; consume(); return json(std::move(s)); }
                case TokenType::Number: {
                    std::string txt = tk.text; consume();
                    if (txt.find_first_of(".eE") != std::string::npos) {
                        return json(std::stod(txt));
                    }
                    else {
                        return json(static_cast<int64_t>(std::stoll(txt)));
                    }
                }
                case TokenType::True:    consume(); return json(true);
                case TokenType::False:   consume(); return json(false);
                case TokenType::Null:    consume(); return json(nullptr);
                default: throw json_exception("Unexpected token in JSON");
                }
            }
        private:
            const Token& peek() const { return tks_[pos_]; }
            void consume() { ++pos_; }
            json parseObject() {
                consume(); object obj;
                while (peek().type != TokenType::RBrace) {
                    if (peek().type != TokenType::String) throw json_exception("Expected string key");
                    std::string key = peek().text; consume();
                    if (peek().type != TokenType::Colon) throw json_exception("Expected ':'"); consume();
                    json val = parseValue();
                    obj.emplace(std::move(key), std::move(val));
                    if (peek().type == TokenType::Comma) consume(); else break;
                }
                if (peek().type != TokenType::RBrace) throw json_exception("Expected '}'");
                consume();
                return json(std::move(obj));
            }
            json parseArray() {
                consume(); array arr;
                while (peek().type != TokenType::RBracket) {
                    arr.push_back(parseValue());
                    if (peek().type == TokenType::Comma) consume(); else break;
                }
                if (peek().type != TokenType::RBracket) throw json_exception("Expected ']'");
                consume();
                return json(std::move(arr));
            }
        };

        // --- Serializer ---
        class Serializer {
            std::ostringstream oss_;
            bool pretty_;
            int indent_;
        public:
            Serializer(bool pretty) : pretty_(pretty), indent_(0) {}
            std::string str() const { return oss_.str(); }
            void serialize(const json& j) { visit(j); }
        private:
            void writeIndent() { if (pretty_) oss_ << std::string(indent_, ' '); }
            void newline() { if (pretty_) oss_ << '\n'; }
            void visit(const json& j) {
                if (j.is_null()) { oss_ << "null"; }
                else if (j.is_bool()) { oss_ << (j.as_bool() ? "true" : "false"); }
                else if (j.is_double()) { oss_ << j.as_double(); }
                else if (j.is_int()) { oss_ << j.as_int(); }
                else if (j.is_string()) { oss_ << '"' << j.as_string() << '"'; }
                else if (j.is_array()) {
                    oss_ << '['; newline(); indent_ += 2;
                    const auto& a = j.as_array();
                    for (size_t i = 0; i < a.size(); ++i) {
                        writeIndent(); visit(a[i]);
                        if (i + 1 < a.size()) oss_ << ',';
                        newline();
                    }
                    indent_ -= 2; writeIndent(); oss_ << ']';
                }
                else {
                    oss_ << '{'; newline(); indent_ += 2;
                    const auto& o = j.as_object();
                    size_t count = 0;
                    for (auto& [k, v] : o) {
                        writeIndent(); oss_ << '"' << k << "\":"; visit(v);
                        if (++count < o.size()) oss_ << ',';
                        newline();
                    }
                    indent_ -= 2; writeIndent(); oss_ << '}';
                }
            }
        };

    public:
        // Constructors
        json() noexcept = default;
        json(std::nullptr_t) noexcept : value_(std::monostate{}) {}
        json(bool b) noexcept : value_(b) {}
        json(double d) noexcept : value_(d) {}
        json(int64_t d) noexcept : value_(d) {}
        json(int i) noexcept : value_(static_cast<double>(i)) {}
        json(const std::string& s) : value_(s) {}
        json(const char* s) : value_(std::move(std::string(s))) {}
        json(std::string&& s) : value_(std::move(s)) {}
        json(const array& a) : value_(a) {}
        json(array&& a) : value_(std::move(a)) {}
        json(const object& o) : value_(o) {}
        json(object&& o) : value_(std::move(o)) {}
        //Json(std::initializer_list<Json> init) : value_(array{ init }) {}
        json(std::initializer_list<object::value_type> init) : value_(object{ init }) {}

        // Parse and dump
        static json parse(std::string_view s) {
            Lexer lx(s);
            auto tokens = lx.tokenize();
            Parser ps(tokens);
            return ps.parseValue();
        }
        std::string dump(bool pretty = false) const {
            Serializer sz(pretty);
            sz.serialize(*this);
            return sz.str();
        }

        // Type checks
        bool is_int() const noexcept { return std::holds_alternative<int64_t>(value_); }
        bool is_double() const noexcept { return std::holds_alternative<double>(value_); }
        bool is_null() const noexcept { return std::holds_alternative<std::monostate>(value_); }
        bool is_bool() const noexcept { return std::holds_alternative<bool>(value_); }
        bool is_number() const noexcept { return is_int() || is_double(); }
        bool is_string() const noexcept { return std::holds_alternative<std::string>(value_); }
        bool is_array() const noexcept { return std::holds_alternative<array>(value_); }
        bool is_object() const noexcept { return std::holds_alternative<object>(value_); }

        // Accessors
        bool as_bool() const { return std::get<bool>(value_); }
        int64_t as_int() const {
            if (!is_int()) throw json_exception("Not an int64");
            return std::get<int64_t>(value_);
        }
        double as_double() const {
            if (!is_double()) throw json_exception("Not a double");
            return std::get<double>(value_);
        }
        double as_number() const {
            if (is_int()) return static_cast<double>(as_int());
            return as_double();
        }
        const std::string& as_string() const { return std::get<std::string>(value_); }
        const array& as_array() const { return std::get<array>(value_); }
        const object& as_object() const { return std::get<object>(value_); }
        array& as_array() { return std::get<array>(value_); }
        object& as_object() { return std::get<object>(value_); }

        // Safe indexing
        json& operator[](size_t i) { return as_array().at(i); }
        json& operator[](const std::string& k) { return as_object().at(k); }
        const json& operator[](size_t i) const { return const_cast<json*>(this)->operator[](i); }
        const json& operator[](const std::string& k) const { return const_cast<json*>(this)->operator[](k); }
    };

} // namespace krnln::ejson
#endif // KRLN_EJSON_HPP






#ifdef  TEST_KRNLN_JSON



#include <iostream>

using namespace krnln;

int main() {
    try {
        // 构造一个超级复杂的 JSON 对象
        json complex = {
            { "name", "OpenAI" },
            { "founded", 2015 },
            { "is_ai_company", true },
            { "null_test", nullptr },
            { "products", json::array{
                "ChatGPT", "Codex", "DALL*E", json{
                    { "name", "Whisper" },
                    { "type", "speech-to-text" },
                    { "languages", json::array{"English", "Spanish", "French"} }
                }
            }},
            { "financials", json{
                { "revenue", 1000000000.50 },
                { "employees", 1000 },
                { "profit_margin", 0.35 }
            }},
            { "meta", json{
                { "created_at", "2025-04-24T10:00:00Z" },
                { "tags", json::array{"中文测试", "ML", "NLP"} },
                { "nested", json{
                    { "level_1", json{
                        { "level_2", json{
                            { "level_3", "deep value" }
                        }}
                    }}
                }}
            }}
        };

        // 输出美化后的 JSON
        std::cout << "Pretty JSON:\n" << complex.dump(true) << "\n\n";

        // 原始 JSON 串
        std::string raw = complex.dump(false);
        std::cout << "Compact JSON:\n" << raw << "\n\n";

        // 再解析回对象
        json parsed = json::parse(raw);
        std::cout << "Re-parsed JSON pretty:\n" << parsed.dump(true) << "\n\n";

        // 访问一些字段
        std::cout << "Founded: " << parsed["founded"].as_number() << "\n";
        std::cout << "First product: " << parsed["products"][0].as_string() << "\n";
        std::cout << "Third language in Whisper: " << parsed["products"][3]["languages"][2].as_string() << "\n";
        std::cout << "Deep nested value: " << parsed["meta"]["nested"]["level_1"]["level_2"]["level_3"].as_string() << "\n";

        // 异常测试：访问不存在的 key（捕获异常）
        try {
            std::cout << parsed["nonexistent"].as_string() << "\n";
        }
        catch (const std::exception& e) {
            std::cerr << "[Handled Exception] " << e.what() << "\n";
        }

        // 异常测试：类型错误访问（捕获异常）
        try {
            std::cout << parsed["founded"].as_string() << "\n";  // 是 double，不是 string
        }
        catch (const std::exception& e) {
            std::cerr << "[Handled Exception] " << e.what() << "\n";
        }

    }
    catch (const std::exception& ex) {
        std::cerr << "[Fatal Error] " << ex.what() << "\n";
    }

    return 0;
}

#endif //  TEST_KRNLN_JSON
