#include "krnln/membin.hpp"
#include "krnln/protobuf.hpp"
#include "krnln/json.hpp"

#include <iostream>

using namespace krnln;

int main() {
    try {
        // 构造一个超级复杂的 JSON 对象 (Test Data)
        json complex = {
            { "name", "test_company" },
            { "founded", 2026 },
            { "is_test_entity", true },
            { "null_test", nullptr },
            { "products", json::array{
                "test_product_1", "test_product_2", "test_product_3", json{
                    { "name", "test_module" },
                    { "type", "test-process" },
                    { "languages", json::array{"English", "Spanish", "French"} }
                }
            }},
            { "financials", json{
                { "revenue", 500000000.50 },
                { "employees", 500 },
                { "profit_margin", 0.20 }
            }},
            { "meta", json{
                { "created_at", "2026-03-06T10:00:00Z" },
                { "tags", json::array{"中文测试", "Test_Suite", "QA"} },
                { "nested", json{
                    { "level_1", json{
                        { "level_2", json{
                            { "level_3", "deep_test_value" }
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
        std::cout << "Third language in test_module: " << parsed["products"][3]["languages"][2].as_string() << "\n";
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
//g++ -std=c++20 -I. test_json.cpp -o test_suite
