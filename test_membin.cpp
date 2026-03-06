


#include "krnln/membin.hpp"
#include "krnln/protobuf.hpp"
#include "krnln/json.hpp"




namespace test_framework {

    // ==================== 测试结果结构 ====================
    struct TestResult {
        std::string name;
        bool passed;
        std::string message;
        double duration_ms;

        TestResult(std::string n, bool p, std::string m = "", double d = 0.0)
            : name(std::move(n)), passed(p), message(std::move(m)), duration_ms(d) {
        }
    };

    // ==================== 测试用例基类 ====================
    class TestCase {
    protected:
        std::vector<TestResult> results;
        std::string suite_name;

        void record_result(const std::string& name, bool passed,
            const std::string& message = "") {
            auto duration = results.empty() ? 0.0 : 0.0; // 简化的时间记录
            results.emplace_back(name, passed, message, duration);
        }

    public:
        TestCase(std::string name) : suite_name(std::move(name)) {}
        virtual ~TestCase() = default;

        virtual void run() = 0;

        const std::vector<TestResult>& get_results() const { return results; }
        const std::string& get_name() const { return suite_name; }

        int count_passed() const {
            return std::count_if(results.begin(), results.end(),
                [](const TestResult& r) { return r.passed; });
        }

        int count_failed() const {
            return static_cast<int>(results.size()) - count_passed();
        }
    };

    // ==================== 测试宏 ====================
#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::ostringstream oss; \
            oss << msg << " (at " << __FILE__ << ":" << __LINE__ << ")"; \
            record_result(__func__, false, oss.str()); \
            return; \
        } \
    } while(0)

#define TEST_EXPECT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::ostringstream oss; \
            oss << msg << " (at " << __FILE__ << ":" << __LINE__ << ")"; \
            record_result(__func__, false, oss.str()); \
        } else { \
            record_result(__func__, true, ""); \
        } \
    } while(0)

#define TEST_SUITE(name) class TestSuite_##name : public test_framework::TestCase

#define TEST_METHOD(name) void test_##name()

#define RUN_TEST(method) \
    do { \
        try { \
            test_##method(); \
        } catch (const std::exception& e) { \
            record_result(#method, false, e.what()); \
        } catch (...) { \
            record_result(#method, false, "Unknown exception"); \
        } \
    } while(0)

// ==================== 测试运行器 ====================
    class TestRunner {
    private:
        std::vector<std::unique_ptr<TestCase>> test_suites;

    public:
        template<typename T>
        void register_test() {
            test_suites.push_back(std::make_unique<T>());
        }

        int run_all() {
            int total_passed = 0;
            int total_failed = 0;

            std::cout << "\n" << std::string(60, '=') << "\n";
            std::cout << "RUNNING ALL TESTS\n";
            std::cout << std::string(60, '=') << "\n\n";

            for (auto& suite : test_suites) {
                auto start = std::chrono::high_resolution_clock::now();

                std::cout << "[" << suite->get_name() << "]\n";
                std::cout << std::string(suite->get_name().length() + 2, '-') << "\n";

                try {
                    suite->run();
                }
                catch (const std::exception& e) {
                    std::cerr << "  ERROR: Test suite crashed: " << e.what() << "\n";
                    continue;
                }

                auto end = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration<double>(end - start).count();

                int passed = suite->count_passed();
                int failed = suite->count_failed();
                total_passed += passed;
                total_failed += failed;

                std::cout << "  √ " << passed << " passed, ";
                if (failed > 0) {
                    std::cout << "× " << failed << " failed";
                }
                else {
                    std::cout << "√ All passed";
                }
                std::cout << " (" << std::fixed << std::setprecision(3)
                    << duration * 1000 << " ms)\n";

                // 显示失败的详细信息
                for (const auto& result : suite->get_results()) {
                    if (!result.passed) {
                        std::cout << "    × " << result.name << ": "
                            << result.message << "\n";
                    }
                }

                std::cout << "\n";
            }

            // 汇总结果
            std::cout << std::string(60, '=') << "\n";
            std::cout << "TEST SUMMARY\n";
            std::cout << std::string(60, '=') << "\n";
            std::cout << "Total: " << (total_passed + total_failed)
                << " tests\n";
            std::cout << "Passed: √ " << total_passed << "\n";
            std::cout << "Failed: ";

            if (total_failed > 0) {
                std::cout << "× " << total_failed << "\n";
            }
            else {
                std::cout << "√ None\n";
            }

            return total_failed == 0 ? 0 : 1;
        }
    };

} // namespace test_framework





using namespace krnln;

// ==================== JSON 测试套件 ====================
TEST_SUITE(JsonTests) {
public:
    TestSuite_JsonTests() : TestCase("JSON") {}

    TEST_METHOD(basic_parsing) {
        auto json = json::parse(R"({"name": "test", "value": 123})");
        TEST_EXPECT(json.is_object(), "Should be object");
        TEST_EXPECT(json["name"].as_string() == "test", "Name should be 'test'");
        TEST_EXPECT(json["value"].as_int() == 123, "Value should be 123");
    }

    TEST_METHOD(array_parsing) {
        auto json = json::parse(R"([1, 2, "three", true, null])");
        TEST_EXPECT(json.is_array(), "Should be array");
        TEST_EXPECT(json[0].as_int() == 1, "First element should be 1");
        TEST_EXPECT(json[2].as_string() == "three", "Third element should be 'three'");
        TEST_EXPECT(json[3].as_bool() == true, "Fourth element should be true");
        TEST_EXPECT(json[4].is_null(), "Fifth element should be null");
    }

    TEST_METHOD(serialization) {
        json j = {
            {"string", "hello"},
            {"number", 42},
            {"array", json::array{1, 2, 3}},
            {"object", json{{"nested", "value"}}}
        };

        std::string compact = j.dump(false);
        std::string pretty = j.dump(true);

        TEST_EXPECT(!compact.empty(), "Compact dump should not be empty");
        TEST_EXPECT(!pretty.empty(), "Pretty dump should not be empty");
        TEST_EXPECT(compact.find('\n') == std::string::npos,
            "Compact should have no newlines");
        TEST_EXPECT(pretty.find('\n') != std::string::npos,
            "Pretty should have newlines");

        // 验证往返
        auto parsed = json::parse(compact);
        TEST_EXPECT(parsed["string"].as_string() == "hello",
            "String should survive round-trip");
    }

    TEST_METHOD(error_handling) {
        bool caught = false;
        try {
            auto json = json::parse("{invalid json}");
        }
        catch (const json_exception&) {
            caught = true;
        }
        TEST_EXPECT(caught, "Should throw on invalid JSON");
    }

    void run() override {
        RUN_TEST(basic_parsing);
        RUN_TEST(array_parsing);
        RUN_TEST(serialization);
        RUN_TEST(error_handling);
    }
};

// ==================== Membin 测试套件 ====================
TEST_SUITE(MembinTests) {
private:
    template<typename T>
    void test_with_type() {
        membin b = { 0x01, 0x02, 0x03, 0x04 };
        T value = b.extract_data<T>(0);
        membin b2 = to_membin(value);
        TEST_EXPECT(b2.size() == sizeof(T), "Size should match");
    }

public:
    TestSuite_MembinTests() : TestCase("Membin") {}

    TEST_METHOD(constructors) {
        membin empty;
        TEST_EXPECT(empty.empty(), "Default constructor should be empty");

        membin from_list = { 1, 2, 3, 4, 5 };
        TEST_EXPECT(from_list.size() == 5, "Initializer list size should be 5");
        TEST_EXPECT(from_list[0] == 1 && from_list[4] == 5,
            "Elements should match initializer list");

        uint8_t data[] = { 10, 20, 30 };
        membin from_ptr(data, sizeof(data));
        TEST_EXPECT(from_ptr.size() == 3, "Pointer constructor size should be 3");

        membin copy(from_ptr);
        TEST_EXPECT(copy == from_ptr, "Copy should be equal");

        membin moved(std::move(copy));
        TEST_EXPECT(moved == from_ptr, "Move should preserve data");
        TEST_EXPECT(copy.empty(), "Moved-from should be empty");
    }

    TEST_METHOD(capacity_operations) {
        membin b;
        TEST_EXPECT(b.capacity() >= 0, "Capacity should be non-negative");

        b.reserve(100);
        TEST_EXPECT(b.capacity() >= 100, "Reserve should increase capacity");
        TEST_EXPECT(b.empty(), "Reserve should not change size");

        b.resize(50, 0xFF);
        TEST_EXPECT(b.size() == 50, "Resize should change size");
        TEST_EXPECT(b[25] == 0xFF, "Resize fill should work");

        b.clear();
        TEST_EXPECT(b.empty(), "Clear should make empty");
    }

    TEST_METHOD(modifiers) {
        membin b;
        b.push_back(1);
        b.push_back(2);
        TEST_EXPECT(b.size() == 2, "Push_back should increase size");
        TEST_EXPECT(b.back() == 2, "Back should return last element");

        membin other = { 3, 4 };
        b.append(other);
        TEST_EXPECT(b.size() == 4, "Append should add elements");
        TEST_EXPECT(b[2] == 3, "Appended elements should be correct");

        b.insert(1, { 9, 9 });
        TEST_EXPECT(b.size() == 6, "Insert should increase size");
        TEST_EXPECT(b[1] == 9, "Inserted elements should be correct");

        b.replace(1, 2, { 8, 8, 8 });
        TEST_EXPECT(b.size() == 7, "Replace should adjust size");
        TEST_EXPECT(b[1] == 8, "Replaced elements should be correct");
    }

    TEST_METHOD(search_operations) {
        membin b = { 1, 2, 3, 4, 2, 3, 5 };
        membin pattern = { 2, 3 };

        size_t pos = b.find(pattern);
        TEST_EXPECT(pos == 1, "Find should return first occurrence");

        pos = b.find(pattern, 2);
        TEST_EXPECT(pos == 4, "Find with offset should work");

        pos = b.rfind(pattern);
        TEST_EXPECT(pos == 4, "Rfind should return last occurrence");

        TEST_EXPECT(b.find({ 9, 9 }) == membin::npos,
            "Find should return npos for missing pattern");
    }

    TEST_METHOD(conversions) {
        membin b = { 0x41, 0x42, 0x43 }; // "ABC"

        auto hex_str = b.hex();
        TEST_EXPECT(hex_str == "414243", "Hex conversion should work");

        auto b64_str = b.base64();
        TEST_EXPECT(b64_str == "QUJD", "Base64 conversion should work");

        auto from_hex = membin::from_hex("414243");
        TEST_EXPECT(from_hex == b, "From hex should work");

        auto from_b64 = membin::from_base64("QUJD");
        TEST_EXPECT(from_b64 == b, "From base64 should work");

        auto str = b.to_string();
        TEST_EXPECT(str == "ABC", "To string should work");
    }

    TEST_METHOD(file_operations) {
        membin original = { 1, 2, 3, 4, 5 };

        bool written = original.write_to_file("test_temp.bin");
        TEST_EXPECT(written, "Write to file should succeed");

        auto loaded = membin::from_file("test_temp.bin");
        TEST_EXPECT(loaded == original, "File round-trip should preserve data");

        std::filesystem::remove("test_temp.bin");
    }

    TEST_METHOD(performance) {
        // 性能基准测试
        const size_t size = 100000;

        auto start = std::chrono::high_resolution_clock::now();
        membin b;
        b.reserve(size);
        for (size_t i = 0; i < size; ++i) {
            b.push_back(static_cast<uint8_t>(i % 256));
        }
        auto end = std::chrono::high_resolution_clock::now();

        auto duration = std::chrono::duration<double>(end - start).count();
        TEST_EXPECT(b.size() == size, "Should create correct size");

        std::cout << "    Performance: " << size << " push_back in "
            << std::fixed << std::setprecision(3)
            << duration * 1000 << " ms\n";
    }

    void run() override {
        RUN_TEST(constructors);
        RUN_TEST(capacity_operations);
        RUN_TEST(modifiers);
        RUN_TEST(search_operations);
        RUN_TEST(conversions);
        RUN_TEST(file_operations);
        RUN_TEST(performance);
    }
};

// ==================== Protobuf 测试套件 ====================
TEST_SUITE(ProtobufTests) {
public:
    TestSuite_ProtobufTests() : TestCase("Protobuf") {}

    TEST_METHOD(basic_encoding_decoding) {
        protobuf::writer writer;
        writer.write(1, 42u);
        writer.write(2, std::string("test"));
        writer.write(3, 3.14);

        auto buf = writer.to_buffer();
        protobuf::reader reader(buf);

        auto field1 = reader.get(1);
        TEST_EXPECT(field1.has_value(), "Field 1 should exist");
        TEST_EXPECT(field1->as_uint() == 42, "Field 1 value should be 42");

        auto field2 = reader.get(2);
        TEST_EXPECT(field2.has_value(), "Field 2 should exist");
        TEST_EXPECT(field2->as_string() == "test", "Field 2 value should be 'test'");
    }

    TEST_METHOD(nested_messages) {
        protobuf::writer inner;
        inner.write(1, std::string("inner"));

        protobuf::writer outer;
        outer.write(1, std::string("outer"));
        outer.write(2, inner);

        auto buf = outer.to_buffer();
        protobuf::reader reader(buf);

        auto outer_field = reader.get(1);
        TEST_EXPECT(outer_field->as_string() == "outer",
            "Outer field should be correct");

        auto inner_reader = reader.get(2)->as_reader();
        auto inner_field = inner_reader.get(1);
        TEST_EXPECT(inner_field->as_string() == "inner",
            "Inner field should be correct");
    }

    TEST_METHOD(packed_fields) {
        protobuf::writer writer;
        std::vector<int32_t> values = { 1, 2, 3, 4, 5 };
        writer.write_packed(1, values);

        auto buf = writer.to_buffer();
        protobuf::reader reader(buf);

        auto field = reader.get(1);
        TEST_EXPECT(field.has_value(), "Packed field should exist");

        auto unpacked = reader.unpack_packed<int32_t>(*field);
        TEST_EXPECT(unpacked == values, "Unpacked values should match original");
    }

    TEST_METHOD(json_conversion) {
        protobuf::writer writer;
        writer.write(1, 42u);
        writer.write(2, std::string("hello"));

        protobuf::writer inner;
        inner.write(1, std::string("world"));
        writer.write(3, inner);

        auto buf = writer.to_buffer();
        protobuf::reader reader(buf);

        auto json = reader.to_json();
        TEST_EXPECT(json.is_object(), "Result should be JSON object");
        TEST_EXPECT(json["1"].as_number() == 42, "Field 1 in JSON should be 42");
        TEST_EXPECT(json["2"].as_string() == "hello",
            "Field 2 in JSON should be 'hello'");

        std::string json_str = json.dump(false);
        TEST_EXPECT(!json_str.empty(), "JSON dump should not be empty");
    }

    TEST_METHOD(stream_iteration) {
        protobuf::writer writer;
        writer.write(1, 100);
        writer.write(2, 200);
        writer.write(3, 300);

        auto buf = writer.to_buffer();
        protobuf::reader reader(buf);

        int count = 0;
        for (auto field : reader) {
            ++count;
            TEST_EXPECT(field.field >= 1 && field.field <= 3,
                "Field number should be in range");
        }
        TEST_EXPECT(count == 3, "Should iterate over all fields");
    }

    TEST_METHOD(error_handling) {
        // 测试无效数据
        membin invalid_data = { 0xFF, 0xFF, 0xFF }; // 无效的varint

        bool caught = false;
        try {
            protobuf::reader reader(invalid_data, true); // 严格模式
        }
        catch (const protobuf::DecodeError&) {
            caught = true;
        }
        TEST_EXPECT(caught, "Should throw on invalid protobuf data");
    }

    void run() override {
        RUN_TEST(basic_encoding_decoding);
        RUN_TEST(nested_messages);
        RUN_TEST(packed_fields);
        RUN_TEST(json_conversion);
        RUN_TEST(stream_iteration);
        RUN_TEST(error_handling);
    }
};

// ==================== 集成测试套件 ====================
TEST_SUITE(IntegrationTests) {
public:
    TestSuite_IntegrationTests() : TestCase("Integration") {}

    TEST_METHOD(json_to_protobuf_roundtrip) {
        // 创建复杂JSON
        json j = {
            {"id", 123},
            {"name", "test object"},
            {"values", json::array{1.1, 2.2, 3.3}},
            {"metadata", json{
                {"version", "1.0"},
                {"enabled", true}
            }}
        };

        // JSON -> 字符串 -> Membin
        std::string json_str = j.dump(false);
        membin json_bin = to_membin(json_str);

        // Membin -> Protobuf
        protobuf::writer writer;
        writer.write(1, 123u);
        writer.write(2, std::string("test object"));
        writer.write(3, json_bin); // 嵌入JSON作为二进制数据

        auto protobuf_bin = writer.to_buffer();

        // Protobuf -> JSON
        protobuf::reader reader(protobuf_bin);
        auto decoded_json = reader.to_json();

        // 验证
        TEST_EXPECT(decoded_json["1"].as_int() == 123,
            "ID should survive round-trip");
        TEST_EXPECT(decoded_json["2"].as_string() == "test object",
            "Name should survive round-trip");
    }

    TEST_METHOD(performance_comparison) {
        // 比较三种库的性能
        const int iterations = 1000;

        // Membin性能
        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < iterations; ++i) {
            membin b(1000, static_cast<uint8_t>(i % 256));
            b.reverse();
        }
        auto membin_time = std::chrono::duration<double>(
            std::chrono::high_resolution_clock::now() - start).count();

        // JSON性能
        start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < iterations; ++i) {
            json j = {
                {"id", i},
                {"data", std::string(100, 'x')}
            };
            auto str = j.dump(false);
            auto parsed = json::parse(str);
        }
        auto json_time = std::chrono::duration<double>(
            std::chrono::high_resolution_clock::now() - start).count();

        // Protobuf性能
        start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < iterations; ++i) {
            protobuf::writer writer;
            writer.write(1, i);
            writer.write(2, std::string(100, 'x'));
            auto buf = writer.to_buffer();
            protobuf::reader reader(buf);
        }
        auto protobuf_time = std::chrono::duration<double>(
            std::chrono::high_resolution_clock::now() - start).count();

        std::cout << "    Performance Comparison (1000 iterations):\n";
        std::cout << "      Membin: " << std::fixed << std::setprecision(3)
            << membin_time * 1000 << " ms\n";
        std::cout << "      JSON:   " << json_time * 1000 << " ms\n";
        std::cout << "      Protobuf: " << protobuf_time * 1000 << " ms\n";

        TEST_EXPECT(true, "Performance test completed");
    }

    TEST_METHOD(memory_usage) {
        // 测试内存使用模式
        const size_t large_size = 1000000; // 1MB

        membin large(large_size, 0xAA);
        TEST_EXPECT(large.size() == large_size,
            "Should allocate large membin");

        // 测试COW（写时复制）
        membin copy1 = large;
        membin copy2 = large;

        // 修改copy1，应该触发COW
        copy1[0] = 0xBB;
        TEST_EXPECT(large[0] == 0xAA, "Original should remain unchanged");
        TEST_EXPECT(copy1[0] == 0xBB, "Copy should be modified");

        TEST_EXPECT(true, "Memory usage test completed");
    }

    void run() override {
        RUN_TEST(json_to_protobuf_roundtrip);
        RUN_TEST(performance_comparison);
        RUN_TEST(memory_usage);
    }
};

// ==================== 主函数 ====================
int main() {
    test_framework::TestRunner runner;

    // 注册所有测试套件
    runner.register_test<TestSuite_JsonTests>();
    runner.register_test<TestSuite_MembinTests>();
    runner.register_test<TestSuite_ProtobufTests>();
    runner.register_test<TestSuite_IntegrationTests>();

    // 运行所有测试
    return runner.run_all();
}


