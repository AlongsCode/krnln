#ifndef MEMBIN_HPP
#define MEMBIN_HPP

#include <iostream>
#include <string>
#include <atomic>
#include <bit>
#include <stdexcept>
#include <vector>
#include <limits>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <random>
#include <memory>
#include <algorithm>
#include <cstring>
#include <type_traits>
#include <span>
namespace krnln {
    namespace _imp {

        /**
         * @brief 分配内存并进行空指针检查
         * @param size_bytes 需要分配的字节数
         * @return 指向分配内存的指针
         * @throws std::bad_alloc 如果内存分配失败
         */
        inline void* checked_malloc(size_t size_bytes) {
            void* ptr = malloc(size_bytes);
            if (!ptr) {
                throw std::bad_alloc();
            }
            return ptr;
        }

        /**
         * @brief 重新分配内存并进行空指针检查
         * @param ptr 指向现有内存块的指针
         * @param new_size_bytes 新的字节大小
         * @return 指向重新分配内存的指针
         * @throws std::bad_alloc 如果内存重新分配失败
         */
        inline void* checked_realloc(void* ptr, size_t new_size_bytes) {
            void* new_ptr = realloc(ptr, new_size_bytes);
            if (!new_ptr) {
                throw std::bad_alloc();
            }
            return new_ptr;
        }

        /**
         * @brief 智能内存重新分配，根据空闲空间比例选择复制或原地重分配
         * @param ptr 指向现有内存块的指针
         * @param used_size 已使用字节数
         * @param current_capacity 当前总容量
         * @param new_capacity 新容量
         * @return 指向重新分配内存的指针
         * @note 当空闲空间超过已用空间的50%时，使用复制方式减少内存碎片
         */
        inline void* smart_realloc(void* ptr, size_t used_size, size_t current_capacity, size_t new_capacity) {
            size_t slack_space = current_capacity - used_size;

            if (slack_space > (used_size >> 1)) {
                // 空闲空间过多，使用复制方式优化内存使用
                void* result = checked_malloc(new_capacity);
                std::memcpy(result, ptr, used_size);
                free(ptr);
                return result;
            }
            // 空闲空间合理，直接原地重新分配
            return checked_realloc(ptr, new_capacity);
        }

        /**
         * @brief 泛型整数加法溢出检查
         * @tparam T 整数类型
         * @param result_ptr 存储结果的指针
         * @param a 第一个加数
         * @param b 第二个加数
         * @return true如果加法安全执行，false如果发生溢出
         */
        template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
        bool generic_checked_add(T* result_ptr, T a, T b) {
            if constexpr (std::is_signed_v<T>) {
                // 有符号整数溢出检查
                if (a >= 0) {
                    if (std::numeric_limits<T>::max() - a < b) {
                        *result_ptr = T{};
                        return false;
                    }
                }
                else if (b < std::numeric_limits<T>::min() - a) {
                    *result_ptr = T{};
                    return false;
                }
                *result_ptr = a + b;
                return true;
            }
            else {
                // 无符号整数溢出检查
                if (a <= std::numeric_limits<T>::max() - b) {
                    *result_ptr = a + b;
                    return true;
                }
                *result_ptr = T{};
                return false;
            }
        }

        /**
         * @brief 整数加法溢出检查
         * @tparam T 整数类型
         * @param result_ptr 存储结果的指针
         * @param a 第一个加数
         * @param b 第二个加数
         * @return true如果加法安全执行，false如果发生溢出
         */
        template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
        bool checked_add(T* result_ptr, T a, T b) {
            return generic_checked_add(result_ptr, a, b);
        }

        /**
         * @brief 无符号整数乘法溢出检查（适用于小于64位的类型）
         * @tparam T 无符号整数类型
         * @param result_ptr 存储结果的指针
         * @param a 第一个乘数
         * @param b 第二个乘数
         * @return true如果乘法安全执行，false如果发生溢出
         */
        template <typename T, typename = std::enable_if_t<std::is_unsigned_v<T>>>
        bool generic_checked_small_mul(T* result_ptr, T a, T b) {
            static_assert(sizeof(T) < sizeof(uint64_t), "类型过大");
            uint64_t result = static_cast<uint64_t>(a) * static_cast<uint64_t>(b);
            constexpr uint64_t overflow_mask = ~((1ULL << (sizeof(T) * 8)) - 1);
            if ((result & overflow_mask) != 0) {
                *result_ptr = T{};
                return false;
            }
            *result_ptr = static_cast<T>(result);
            return true;
        }

        /**
         * @brief 无符号整数乘法溢出检查（64位以下类型）
         * @tparam T 无符号整数类型
         * @param result_ptr 存储结果的指针
         * @param a 第一个乘数
         * @param b 第二个乘数
         * @return true如果乘法安全执行，false如果发生溢出
         */
        template <typename T, typename = std::enable_if_t<std::is_unsigned_v<T>>>
        std::enable_if_t < sizeof(T) < sizeof(uint64_t), bool >
            generic_checked_mul(T* result_ptr, T a, T b) {
            return generic_checked_small_mul(result_ptr, a, b);
        }

        /**
         * @brief 64位无符号整数乘法溢出检查
         * @tparam T 64位无符号整数类型
         * @param result_ptr 存储结果的指针
         * @param a 第一个乘数
         * @param b 第二个乘数
         * @return true如果乘法安全执行，false如果发生溢出
         */
        template <typename T, typename = std::enable_if_t<std::is_unsigned_v<T>>>
        std::enable_if_t<sizeof(T) == sizeof(uint64_t), bool>
            generic_checked_mul(T* result_ptr, T a, T b) {
            constexpr uint64_t half_bits = 32;
            constexpr uint64_t half_mask = (1ULL << half_bits) - 1ULL;

            uint64_t lhs_high = a >> half_bits;
            uint64_t lhs_low = a & half_mask;
            uint64_t rhs_high = b >> half_bits;
            uint64_t rhs_low = b & half_mask;

            if (lhs_high == 0 && rhs_high == 0) {
                *result_ptr = lhs_low * rhs_low;
                return true;
            }

            if (lhs_high != 0 && rhs_high != 0) {
                *result_ptr = T{};
                return false;
            }

            uint64_t mid_bits1 = lhs_low * rhs_high;
            if (mid_bits1 >> half_bits != 0) {
                *result_ptr = T{};
                return false;
            }

            uint64_t mid_bits2 = lhs_high * rhs_low;
            if (mid_bits2 >> half_bits != 0) {
                *result_ptr = T{};
                return false;
            }

            uint64_t mid_bits = mid_bits1 + mid_bits2;
            if (mid_bits >> half_bits != 0) {
                *result_ptr = T{};
                return false;
            }

            uint64_t low_bits = lhs_low * rhs_low;
            if (!generic_checked_add(result_ptr, low_bits, mid_bits << half_bits)) {
                *result_ptr = T{};
                return false;
            }
            return true;
        }

        /**
         * @brief 无符号整数乘法溢出检查
         * @tparam T 无符号整数类型
         * @param result_ptr 存储结果的指针
         * @param a 第一个乘数
         * @param b 第二个乘数
         * @return true如果乘法安全执行，false如果发生溢出
         */
        template <typename T, typename = std::enable_if_t<std::is_unsigned_v<T>>>
        bool checked_mul(T* result_ptr, T a, T b) {
            return generic_checked_mul(result_ptr, a, b);
        }

        /**
         * @brief 乘加运算溢出检查 (base * mul + add)
         * @tparam T 无符号整数类型
         * @param result_ptr 存储结果的指针
         * @param base 基数
         * @param mul 乘数
         * @param add 加数
         * @return true如果运算安全执行，false如果发生溢出
         */
        template <typename T, typename = std::enable_if_t<std::is_unsigned_v<T>>>
        bool checked_muladd(T* result_ptr, T base, T mul, T add) {
            T temp_result{};
            if (!checked_mul(&temp_result, base, mul)) {
                *result_ptr = T{};
                return false;
            }
            if (!checked_add(&temp_result, temp_result, add)) {
                *result_ptr = T{};
                return false;
            }
            *result_ptr = temp_result;
            return true;
        }

    }
    // 转换工具类 - 用于复用转换逻辑
    namespace _conversion {


        // 强制字节序转换 (例如：将主机序转为大端)
        template <typename T>
        [[nodiscard]] inline T swap_endian(T val) {
            /*原版:
            template <typename T>
        [[nodiscard]] inline T swap_endian(T val) {
            static_assert(std::is_arithmetic_v<T>, "必须为可计算类型");
            union { T val; uint8_t raw[sizeof(T)]; } src, dst;
            src.val = val;
            for (size_t i = 0; i < sizeof(T); ++i)
                dst.raw[i] = src.raw[sizeof(T) - 1 - i];
            return dst.val;
        }
            */

            static_assert(std::is_arithmetic_v<T>, "必须为可计算类型");
            T ret;
            uint8_t* src = reinterpret_cast<uint8_t*>(&val);
            uint8_t* dst = reinterpret_cast<uint8_t*>(&ret);
            for (size_t i = 0; i < sizeof(T); ++i) {
                dst[i] = src[sizeof(T) - 1 - i];
            }
            return ret;
        }

        /**
         * @brief 转换为十进制文本格式
         * @tparam CharType 字符类型
         * @param data 数据指针
         * @param size 数据大小
         * @return 格式如"{1,2,3}"的字符串
         */
        template<typename CharType = char>
        [[nodiscard]] inline std::basic_string<CharType> to_decimal(const uint8_t* data, size_t size) {
            if (size == 0) return {};

            std::basic_string<CharType> result;
            result.reserve(size * 4 + 3); // 预留足够空间

            result.push_back('{');
            for (size_t i = 0; i < size; ++i) {
                uint8_t byte = data[i];

                if (byte >= 100) {
                    result.push_back(static_cast<CharType>('0' + byte / 100));
                    result.push_back(static_cast<CharType>('0' + (byte % 100) / 10));
                }
                else if (byte >= 10) {
                    result.push_back(static_cast<CharType>('0' + byte / 10));
                }
                result.push_back(static_cast<CharType>('0' + byte % 10));

                if (i != size - 1) {
                    result.push_back(',');
                }
            }
            result.push_back('}');

            return result;
        }

        /**
         * @brief 转换为十六进制文本
         * @tparam CharType 字符类型
         * @param data 数据指针
         * @param size 数据大小
         * @param use_lowercase 是否使用小写字母
         * @return 十六进制字符串
         */
        template<typename CharType = char>
        [[nodiscard]] inline std::basic_string<CharType> to_hex(const uint8_t* data, size_t size, bool use_lowercase = true) {
            if (size == 0) return {};

            static constexpr CharType hex_digits_upper[16] = {
                '0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'
            };

            static constexpr CharType hex_digits_lower[16] = {
                '0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f'
            };

            const auto* digits = use_lowercase ? hex_digits_lower : hex_digits_upper;
            std::basic_string<CharType> result(size * 2, CharType{});

            for (size_t i = 0; i < size; ++i) {
                uint8_t byte = data[i];
                result[i * 2] = digits[byte >> 4];
                result[i * 2 + 1] = digits[byte & 0x0F];
            }

            return result;
        }

        /**
         * @brief Base64编码
         * @tparam CharType 字符类型
         * @param data 数据指针
         * @param size 数据大小
         * @param alphabet Base64字母表
         * @return Base64编码字符串
         */
        template<typename CharType = char>
        [[nodiscard]] inline std::basic_string<CharType> to_base64(
            const uint8_t* data, size_t size,
            const std::basic_string<CharType>& alphabet =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/") {

            if (size == 0) return {};

            std::basic_string<CharType> result;
            result.reserve((size + 2) / 3 * 4);

            for (size_t i = 0; i < size; i += 3) {
                uint32_t triple = 0;
                size_t bytes_in_triple = std::min(size - i, size_t(3));

                for (size_t j = 0; j < bytes_in_triple; ++j) {
                    triple |= static_cast<uint32_t>(data[i + j]) << (16 - j * 8);
                }

                for (size_t j = 0; j < 4; ++j) {
                    if (j * 6 < bytes_in_triple * 8) {
                        uint8_t index = (triple >> (18 - j * 6)) & 0x3F;
                        result.push_back(alphabet[index]);
                    }
                    else {
                        result.push_back('=');
                    }
                }
            }

            return result;
        }

        /**
         * @brief 转换为字符串
         * @tparam CharType 字符类型
         * @param data 数据指针
         * @param size 数据大小
         * @return 字符串表示
         */
        template<typename CharType = char>
        [[nodiscard]] inline std::basic_string<CharType> to_string(const uint8_t* data, size_t size) {
            if (size == 0) return {};

            std::basic_string<CharType> result(
                reinterpret_cast<const CharType*>(data),
                size / sizeof(CharType));
            return result;
        }
    }



    /**
     * @class membin
     * @brief 高性能二进制数据容器
     *
     * 实现高效的二进制数据存储和管理，支持以下特性：
     * 1. SSO（小对象优化）：小数据直接存储在对象内部，避免堆分配
     * 2. 引用计数：大数据使用写时复制（COW）共享，减少内存开销
     * 3. 异常安全：所有操作均保证强异常安全
     * 4. STL兼容：提供完整迭代器支持，可与标准算法配合使用
     * 5. 多种编码支持：十六进制、Base64、十进制文本等
     */
    class membin {
    public:
        /**
        * @class view
        * @brief membin 的只读视图类
        *
        * 提供对 membin 对象的只读访问，支持查找等操作。
        */
        class view {

            const uint8_t* _ptr;
            size_t _len;
        public:
            // ==================== 构造函数 ====================
            // 方便从 membin 隐式转换
            view(const membin& m) : _ptr(m.data()), _len(m.size()) {}
            view(const uint8_t* p, size_t l) : _ptr(p), _len(l) {}

            // ==================== 基本属性 ====================
            const uint8_t* data() const { return _ptr; }
            size_t size() const { return _len; }
            bool empty() const { return _len == 0; }

            // ==================== 查找操作 ====================
            /**
             * @brief 正向查找子序列
            * @param pattern 要查找的字节序列
            * @param start_offset 起始搜索位置
             * @return 找到的位置索引，未找到返回npos
            */
            [[nodiscard]] size_t find(const membin& pattern, size_t start_offset = 0) const {
                return membin::internal_find(_ptr, _len, pattern.data(), pattern.size(), start_offset);
            }

            /**
             * @brief 反向查找子序列
             * @param pattern 要查找的字节序列
             * @param start_offset 起始搜索位置
             * @return 找到的位置索引，未找到返回npos
             */
            [[nodiscard]] size_t rfind(const membin& pattern, size_t start_offset = membin::npos) const {
                return membin::internal_reverse_find(_ptr, _len, pattern.data(), pattern.size(), start_offset);
            }


            [[nodiscard]] view left(size_t n) const {
                return view(_ptr, std::min(n, _len));
            }
            [[nodiscard]] view mid(size_t pos, size_t n = membin::npos) const {
                if (pos >= _len) return { nullptr, 0 };
                return view(_ptr + pos, std::min(n, _len - pos));
            }
            [[nodiscard]] view right(size_t n) const {
                if (n >= _len) return view(_ptr, _len);
                return view(_ptr + (_len - n), n);
            }
            [[nodiscard]] membin to_membin() const {
                return membin(_ptr, _len);
            }

            operator membin() const {
                return to_membin();
            }


            // ==================== 转换操作 ====================

            /**
             * @brief 转换为十进制文本格式
             * @tparam CharType 字符类型
             * @return 格式如"{1,2,3}"的字符串
             */
            template<typename CharType = char>
            [[nodiscard]] std::basic_string<CharType> decimal() const {
                return _conversion::to_decimal<CharType>(_ptr, _len);
            }

            /**
             * @brief 转换为十六进制文本
             * @tparam CharType 字符类型
             * @param use_lowercase 是否使用小写字母
             * @return 十六进制字符串
             */
            template<typename CharType = char>
            [[nodiscard]] std::basic_string<CharType> hex(bool use_lowercase = true) const {
                return _conversion::to_hex<CharType>(_ptr, _len, use_lowercase);
            }

            /**
             * @brief Base64编码
             * @tparam CharType 字符类型
             * @param alphabet Base64字母表
             * @return Base64编码字符串
             */
            template<typename CharType = char>
            [[nodiscard]] std::basic_string<CharType> base64(
                const std::basic_string<CharType>& alphabet =
                "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/") const {
                return _conversion::to_base64<CharType>(_ptr, _len, alphabet);
            }

            /**
             * @brief 转换为字符串
             * @tparam CharType 字符类型
             * @return 字符串表示
             */
            template<typename CharType = char>
            [[nodiscard]] std::basic_string<CharType> to_string() const {
                return _conversion::to_string<CharType>(_ptr, _len);
            }

            // ==================== 元素访问 ====================
            /**
            * @brief 获取指定位置的字节（只读）
            * @param position 位置索引
            * @return 指定位置的字节
            * @throws std::out_of_range 如果索引越界
            */
            auto at(size_t position) const {
                if (position >= _len) {
                    throw std::out_of_range("view访问下标超出范围");
                }
                return _ptr[position];
            }

            /**
             * @brief 下标访问操作符（只读）
             * @param position 位置索引
             * @return 指定位置的字节
             */
            auto operator[](size_t position) const {
                // 注意：不进行边界检查以提高性能
                return _ptr[position];
            }

            /**
             * @brief 获取第一个字节
             * @return 第一个字节
             */
            auto front() const noexcept {
                return _ptr[0];
            }

            /**
             * @brief 获取最后一个字节
             * @return 最后一个字节
             */
            auto back() const noexcept {
                return _ptr[_len - 1];
            }


        };
    public:
        // 类型别名
        using value_type = uint8_t;
        using iterator = value_type*;
        using const_iterator = const value_type*;
        using reverse_iterator = std::reverse_iterator<iterator>;
        using const_reverse_iterator = std::reverse_iterator<const_iterator>;
        using reference = value_type&;
        using const_reference = const value_type&;

        // 常量定义
        static constexpr bool kIsLittleEndian = (std::endian::native == std::endian::little);
        static constexpr size_t npos = static_cast<size_t>(-1);

        // 数值类内存布局
        enum class Endianness {
            BigEndian,
            LittleEndian,
            Native = static_cast<int>(std::endian::native == std::endian::little ? 1:0)
        };

    private:

        /**
         * @brief 数据存储类别枚举
         */
        enum class StorageCategory : uint8_t {
            isSmall = 0,                           ///< 小数据（SSO优化）
            isMedium = kIsLittleEndian ? 0x80 : 0x2, ///< 中等数据（独占堆分配）
            isLarge = kIsLittleEndian ? 0x40 : 0x1,  ///< 大数据（引用计数共享）

        };
        bool is_small() {
            return storage_category() == StorageCategory::isSmall;
        };
        bool is_med() {
            auto cat = storage_category();
            return  (static_cast<uint8_t>(cat) & static_cast<uint8_t>(StorageCategory::isMedium)) != 0;
        };
        bool is_lrg() {
            auto cat = storage_category();
            return  (static_cast<uint8_t>(cat) & static_cast<uint8_t>(StorageCategory::isLarge)) != 0;
        };
        /**
         * @brief 中等或大数据存储结构
         */
        struct alignas(void*) MediumLargeStorage {
            value_type* data_ptr;     ///< 指向堆分配数据的指针
            size_t current_size;       ///< 当前有效数据大小
            size_t capacity_with_flags;   ///< 容量（包含类别标记位）

            /**
             * @brief 获取实际容量（排除标记位）
             * @return 实际分配的字节容量
             */
            size_t actual_capacity() const {
                return kIsLittleEndian ?
                    capacity_with_flags & kCapacityExtractMask :
                    capacity_with_flags >> 2;
            }

            /**
             * @brief 设置容量并存储类别信息
             * @param capacity 实际容量
             * @param category 数据存储类别
             */
            void set_capacity_with_category(size_t capacity, StorageCategory category) {
                capacity_with_flags = kIsLittleEndian ?
                    capacity | (static_cast<size_t>(category) << kCategoryShift) :
                    (capacity << 2) | static_cast<size_t>(category);
            }
        };

        /**
         * @brief 引用计数块结构
         */
        struct RefCountedBlock {
            std::atomic<size_t> reference_count;  ///< 原子引用计数
            value_type data_start[1];                ///< 数据起始位置（柔性数组）

            /**
             * @brief 获取数据起始偏移量
             * @return 数据区相对于结构体起始的偏移量
             */
            static constexpr size_t data_offset() {
                return offsetof(RefCountedBlock, data_start);
            }

            /**
             * @brief 从数据指针获取RefCountedBlock指针
             * @param data_pointer 数据区指针
             * @return 对应的RefCountedBlock指针
             */
            static RefCountedBlock* from_data_pointer(value_type* data_pointer) {
                return reinterpret_cast<RefCountedBlock*>(
                    reinterpret_cast<value_type*>(data_pointer) - data_offset());
            }

            /**
             * @brief 获取当前引用计数
             * @param data_pointer 数据区指针
             * @return 当前引用计数值
             */
            static size_t get_reference_count(value_type* data_pointer) {
                return from_data_pointer(data_pointer)->reference_count.load(std::memory_order_acquire);
            }

            /**
             * @brief 增加引用计数
             * @param data_pointer 数据区指针
             */
            static void increment_reference_count(value_type* data_pointer) {
                from_data_pointer(data_pointer)->reference_count.fetch_add(1, std::memory_order_acq_rel);
            }

            /**
             * @brief 减少引用计数，计数为0时释放内存
             * @param data_pointer 数据区指针
             */
            static void decrement_reference_count(value_type* data_pointer) {
                auto block_ptr = from_data_pointer(data_pointer);
                size_t old_count = block_ptr->reference_count.fetch_sub(1, std::memory_order_acq_rel);
                if (old_count == 1) {
                    std::free(block_ptr);
                }
            }

            /**
             * @brief 创建新的引用计数块
             * @param requested_size 请求的数据大小（输入输出参数）
             * @return 新分配的RefCountedBlock指针
             * @throws std::length_error 如果大小计算溢出
             */
            static RefCountedBlock* create_block(size_t* requested_size) {
                size_t capacity_bytes;
                if (!_imp::checked_add(&capacity_bytes, *requested_size, size_t(1))) {
                    throw std::length_error("容量计算溢出");
                }
                if (!_imp::checked_muladd(&capacity_bytes, capacity_bytes,
                    sizeof(value_type), data_offset())) {
                    throw std::length_error("容量计算溢出");
                }

                auto block_ptr = static_cast<RefCountedBlock*>(_imp::checked_malloc(capacity_bytes));
                block_ptr->reference_count.store(1, std::memory_order_release);
                *requested_size = (capacity_bytes - data_offset()) / sizeof(value_type) - 1;
                return block_ptr;
            }

            /**
             * @brief 创建引用计数块并复制数据
             * @param source_data 源数据指针
             * @param requested_size 请求的数据大小（输入输出参数）
             * @return 新分配的RefCountedBlock指针
             */
            static RefCountedBlock* create_block_with_data(const value_type* source_data, size_t* requested_size) {
                const size_t original_size = *requested_size;
                auto block_ptr = create_block(requested_size);
                if (original_size > 0) {
                    std::memcpy(block_ptr->data_start, source_data, original_size);
                }
                return block_ptr;
            }

            /**
             * @brief 重新分配引用计数块
             * @param old_data_pointer 原数据指针
             * @param current_size 当前数据大小
             * @param current_capacity 当前容量
             * @param new_capacity 新容量（输入输出参数）
             * @return 重新分配的RefCountedBlock指针
             * @throws std::length_error 如果大小计算溢出
             */
            static RefCountedBlock* reallocate_block(value_type* old_data_pointer, size_t current_size,
                size_t current_capacity, size_t* new_capacity) {
                size_t capacity_bytes;
                if (!_imp::checked_add(&capacity_bytes, *new_capacity, size_t(1))) {
                    throw std::length_error("容量计算溢出");
                }
                if (!_imp::checked_muladd(&capacity_bytes, capacity_bytes,
                    sizeof(value_type), data_offset())) {
                    throw std::length_error("容量计算溢出");
                }

                auto old_block_ptr = from_data_pointer(old_data_pointer);
                auto new_block_ptr = static_cast<RefCountedBlock*>(
                    _imp::smart_realloc(old_block_ptr,
                        data_offset() + (current_size + 1) * sizeof(value_type),
                        data_offset() + (current_capacity + 1) * sizeof(value_type),
                        capacity_bytes));

                *new_capacity = (capacity_bytes - data_offset()) / sizeof(value_type) - 1;
                return new_block_ptr;
            }
        };

        // 静态常量
        static constexpr size_t kLastByteIndex = sizeof(MediumLargeStorage) - 1;
        static constexpr size_t kMaxSmallSize = kLastByteIndex / sizeof(value_type);
        static constexpr size_t kMaxMediumSize = 254 / sizeof(value_type);
        static constexpr value_type kCategoryExtractMask = kIsLittleEndian ? 0xC0 : 0x3;
        static constexpr size_t kCategoryShift = (sizeof(size_t) - 1) * 8;
        static constexpr size_t kCapacityExtractMask = kIsLittleEndian ?
            ~(static_cast<size_t>(kCategoryExtractMask) << kCategoryShift) : 0x0;

        // 内存布局
        union alignas(void*) {
            value_type raw_bytes[sizeof(MediumLargeStorage)];          ///< 字节访问视图
            value_type small_data[kMaxSmallSize + 1];            ///< 小数据存储区
            MediumLargeStorage medium_large;                             ///< 中大数据存储结构
        };

        // 静态断言确保内存布局正确
        static_assert(sizeof(MediumLargeStorage) % sizeof(value_type) == 0,
            "MediumLargeStorage内存布局损坏");
        static_assert(offsetof(MediumLargeStorage, data_ptr) == 0,
            "MediumLargeStorage内存布局损坏");
        static_assert(offsetof(MediumLargeStorage, current_size) == sizeof(medium_large.data_ptr),
            "MediumLargeStorage内存布局损坏");
        static_assert(offsetof(MediumLargeStorage, capacity_with_flags) == 2 * sizeof(medium_large.data_ptr),
            "MediumLargeStorage内存布局损坏");
        static_assert(alignof(MediumLargeStorage) >= alignof(value_type),
            "Alignment requirement not met");
        static_assert(std::is_trivially_destructible_v<MediumLargeStorage>,
            "MediumLargeStorage 必须是平凡析构的，以确保手动资源转移的安全性");
    public:
        // ==================== 构造与析构 ====================

        /**
         * @brief 默认构造函数
         */
        membin() noexcept { reset_to_empty(); }

        /**
         * @brief 拷贝构造函数
         * @param source 源对象
         */
        membin(const membin& source) {
            reset_to_empty();
            switch (source.storage_category()) {
            case StorageCategory::isSmall: copy_small_storage(source); break;
            case StorageCategory::isMedium: copy_medium_storage(source); break;
            case StorageCategory::isLarge: copy_large_storage(source); break;
            }
        }

        /**
         * @brief 移动构造函数
         * @param source 源对象（将被移动）
         */
        membin(membin&& source) noexcept {
            medium_large = source.medium_large;
            source.reset_to_empty();
        }

        /**
         * @brief 从原始数据构造
         * @param data_pointer 数据指针
         * @param data_size 数据大小（字节）
         */
        membin(const void* data_pointer, size_t data_size) {
            reset_to_empty();
            if (data_size == 0) return;

            const value_type* byte_data = reinterpret_cast<const value_type*>(data_pointer);
            if (data_size <= kMaxSmallSize) {
                initialize_small_storage(byte_data, data_size);
            }
            else if (data_size <= kMaxMediumSize) {
                initialize_medium_storage(byte_data, data_size);
            }
            else {
                initialize_large_storage(byte_data, data_size);
            }
        }

        /**
         * @brief 从迭代器范围构造
         * @param begin_iterator 起始迭代器
         * @param end_iterator 结束迭代器
         */
        membin(const value_type* begin_iterator, const value_type* end_iterator)
            : membin(begin_iterator, static_cast<size_t>(end_iterator - begin_iterator)) {
        }

        /**
         * @brief 重复单个字节构造
         * @param repeat_count 重复次数
         * @param fill_byte 填充字节值（默认为0）
         */
        membin(size_t repeat_count, value_type fill_byte = 0) {
            reset_to_empty();
            if (repeat_count == 0) return;

            auto new_data_ptr = expand_without_initialization(repeat_count);
            std::memset(new_data_ptr, fill_byte, repeat_count);
        }

        /**
         * @brief 重复字节序列构造
         * @param repeat_count 重复次数
         * @param pattern 重复的字节序列
         */
        membin(size_t repeat_count, const membin& pattern) {
            reset_to_empty();
            if (repeat_count == 0 || pattern.empty()) return;

            auto new_data_ptr = expand_without_initialization(repeat_count * pattern.size());
            for (size_t i = 0; i < repeat_count; ++i) {
                std::memcpy(new_data_ptr, pattern.data(), pattern.size());
                new_data_ptr += pattern.size();
            }
        }

        /**
         * @brief 初始化列表构造
         * @param initializer_list 初始化列表
         */
        membin(std::initializer_list<uint8_t> initializer_list) {
            reset_to_empty();
            reserve(initializer_list.size());
            for (auto value : initializer_list) {
                push_back(static_cast<uint8_t>(value));
            }
        }

        /**
         * @brief 析构函数
         */
        ~membin() noexcept {
            if (storage_category() != StorageCategory::isSmall) {
                destroy_medium_large_storage();
            }
        }

        // ==================== 赋值操作符 ====================

        /**
         * @brief 拷贝赋值操作符
         * @param source 源对象
         * @return 当前对象引用
         */
        membin& operator=(const membin& source) {
            if (this != &source) {
                assign(source.data(), source.size());
            }
            return *this;
        }

        /**
         * @brief 移动赋值操作符
         * @param source 源对象
         * @return 当前对象引用
         */
        membin& operator=(membin&& source) noexcept {
            /*
            原版
            if (this != &source) {
                this->~membin();
                new (this) membin(std::move(source));
            }
            return *this;*/

            if (this != &source) {
                if (storage_category() != StorageCategory::isSmall) {
                    destroy_medium_large_storage();
                }

                this->medium_large = source.medium_large;
                source.reset_to_empty();
            }
            return *this;

        }

        /**
         * @brief 初始化列表赋值
         * @param initializer_list 初始化列表
         * @return 当前对象引用
         */
        membin& operator=(std::initializer_list<value_type> initializer_list) {
            return assign(initializer_list.begin(), initializer_list.size());
        }

        /**
         * @brief 追加操作符
         * @param data_to_append 要追加的字节序列
         * @return 当前对象引用
         */
        membin& operator+=(const membin& data_to_append) {
            return append(data_to_append);
        }

        /**
         * @brief 移动追加操作符
         * @param data_to_append 要追加的字节序列
         * @return 当前对象引用
         */
        membin& operator+=(membin&& data_to_append) {
            return append(std::move(data_to_append));
        }

        /**
         * @brief 初始化列表追加
         * @param initializer_list 初始化列表
         * @return 当前对象引用
         */
        membin& operator+=(std::initializer_list<value_type> initializer_list) {
            return append(initializer_list.begin(), initializer_list.size());
        }

        /**
         * @brief 常量下标访问操作符
         * @param position 位置索引
         * @return 指定位置的字节常量引用
         */
        const_reference operator[](size_t position) const {
            return *(data() + position);
        }

        /**
         * @brief 非常量下标访问操作符
         * @param position 位置索引
         * @return 指定位置的字节引用
         */
        reference operator[](size_t position) {
            return *(mutable_data() + position);
        }

        // ==================== 迭代器 ====================

        iterator begin() noexcept { return mutable_data(); }
        const_iterator begin() const noexcept { return data(); }
        const_iterator cbegin() const noexcept { return begin(); }

        iterator end() noexcept { return mutable_data() + size(); }
        const_iterator end() const noexcept { return data() + size(); }
        const_iterator cend() const noexcept { return end(); }

        reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }
        const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }
        const_reverse_iterator crbegin() const noexcept { return rbegin(); }

        reverse_iterator rend() noexcept { return reverse_iterator(begin()); }
        const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }
        const_reverse_iterator crend() const noexcept { return rend(); }

        // ==================== 容量操作 ====================

        /**
         * @brief 获取字节序列大小
         * @return 字节数
         */
        [[nodiscard]] size_t size() const noexcept {
            if constexpr (kIsLittleEndian) {
                auto possible_small_size = kMaxSmallSize - small_data[kMaxSmallSize];
                return (static_cast<ptrdiff_t>(possible_small_size) >= 0) ?
                    possible_small_size : medium_large.current_size;
            }
            else {
                return (storage_category() == StorageCategory::isSmall) ? small_storage_size() : medium_large.current_size;
            }
        }

        /**
         * @brief 检查是否为空
         * @return true如果字节序列为空
         */
        [[nodiscard]] bool empty() const noexcept { return size() == 0; }

        /**
         * @brief 获取当前分配容量
         * @return 当前分配的容量（字节）
         */
        [[nodiscard]] size_t capacity() const {
            switch (storage_category()) {
            case StorageCategory::isSmall:
                return kMaxSmallSize;
            case StorageCategory::isLarge:
                // 共享的大数据没有可用容量
                if (RefCountedBlock::get_reference_count(medium_large.data_ptr) > 1) {
                    return medium_large.current_size;
                }
                break;
            default:
                break;
            }
            return medium_large.actual_capacity();
        }

        /**
         * @brief 预留容量
         * @param min_capacity 最小容量要求
         */
        void reserve(size_t min_capacity) {
            switch (storage_category()) {
            case StorageCategory::isSmall: reserve_small_storage(min_capacity); break;
            case StorageCategory::isMedium: reserve_medium_storage(min_capacity); break;
            case StorageCategory::isLarge: reserve_large_storage(min_capacity); break;
            }
        }

        /**
         * @brief 调整字节序列大小
         * @param new_size 新的大小
         * @param fill_byte 填充字节（默认为0）
         */
        void resize(size_t new_size, value_type fill_byte = 0) {
            size_t current_size = size();
            if (new_size <= current_size) {
                shrink_by(current_size - new_size);
            }
            else {
                auto growth_amount = new_size - current_size;
                auto new_data_ptr = expand_without_initialization(growth_amount);
                std::memset(new_data_ptr, fill_byte, growth_amount);
            }
        }

        /**
         * @brief 清空字节序列
         */
        void clear() noexcept { shrink_by(size()); }

        /**
         * @brief 交换两个字节序列
         * @param other 要交换的对象
         */
        void swap(membin& other) noexcept {
            std::swap(medium_large, other.medium_large);
        }

        // ==================== 元素访问 ====================
         /**
         * @brief 获取可修改的数据指针
         * @return 指向可修改数据的指针
         */
        [[nodiscard]] value_type* mutable_data() {
            switch (storage_category()) {
            case StorageCategory::isSmall:
                return small_data;
            case StorageCategory::isMedium:
                return medium_large.data_ptr;
            case StorageCategory::isLarge:
                return get_mutable_large_data();
            default:
                throw std::bad_alloc();
            }
        }
        /**
         * @brief 获取只读数据指针
         * @return 指向数据的常量指针
         */
        [[nodiscard]] const value_type* data() const noexcept {
            return (storage_category() == StorageCategory::isSmall) ? small_data : medium_large.data_ptr;
        }

        /**
         * @brief 检查数据是否被共享
         * @return true如果数据被共享（且为大数据类型）
         * @note 不能用作线程同步原语
         */
        [[nodiscard]] bool is_shared() const noexcept {
            // SSO和Medium类型从不共享
            if (storage_category() != StorageCategory::isLarge) {
                return false;
            }
            return RefCountedBlock::get_reference_count(medium_large.data_ptr) > 1;
        }

        /**
         * @brief 安全擦除字节序列内容（物理填零）
         */
        void secure_erase() noexcept {
            size_t current_size = size();
            if (current_size == 0) return;

            if (storage_category() == StorageCategory::isSmall) {
                // 擦除SSO内部缓冲区
                std::memset(small_data, 0, kMaxSmallSize);
            }
            else {

                if (is_med() ||
                    (is_lrg() &&
                        RefCountedBlock::get_reference_count(medium_large.data_ptr) == 1)) {
                    // 使用capacity确保缓冲区尾部也被清理
                    auto size = medium_large.actual_capacity();
                    volatile unsigned char* p = static_cast<volatile unsigned char*>(medium_large.data_ptr);
                    while (size--) {
                        *p++ = 0;
                    }
                }
                // 如果是共享的，出于COW安全考虑，不能直接擦除共享缓冲区,最后一个不使用时再擦除,因为内存存在一段数据，分离复制再擦除元数据还是存在所以不处理等待全部使用后擦除
            }
            clear(); // 逻辑清空并重置为SSO状态
        }

        /**
         * @brief 带边界检查的访问（常量版本）
         * @param position 位置索引
         * @return 指定位置的字节常量引用
         * @throws std::out_of_range 如果索引越界
         */
        const_reference at(size_t position) const {
            if (position >= size()) {
                throw std::out_of_range("字节集访问下标超出定义范围");
            }
            return (*this)[position];
        }

        /**
         * @brief 带边界检查的访问（非常量版本）
         * @param position 位置索引
         * @return 指定位置的字节引用
         * @throws std::out_of_range 如果索引越界
         */
        reference at(size_t position) {
            if (position >= size()) {
                throw std::out_of_range("字节集访问下标超出定义范围");
            }
            return (*this)[position];
        }

        /**
         * @brief 获取第一个字节的引用
         * @return 第一个字节的引用
         */
        reference front() noexcept {
            return mutable_data()[0];
        }

        /**
         * @brief 获取第一个字节的常量引用
         * @return 第一个字节的常量引用
         */
        const_reference front() const noexcept {
            return data()[0];
        }

        /**
         * @brief 获取最后一个字节的引用
         * @return 最后一个字节的引用
         */
        reference back() noexcept {
            return mutable_data()[size() - 1];
        }

        /**
         * @brief 获取最后一个字节的常量引用
         * @return 最后一个字节的常量引用
         */
        const_reference back() const noexcept {
            return data()[size() - 1];
        }

        // ==================== 修改操作 ====================

        /**
         * @brief 追加数据
         * @param data_pointer 数据指针
         * @param data_size 数据大小
         * @return 当前对象引用
         */
        membin& append(const void* data_pointer, size_t data_size) {
            if (data_size == 0) return *this;

            const value_type* byte_data = reinterpret_cast<const value_type*>(data_pointer);
            auto old_size = size();
            auto old_data = data();
            auto new_data_ptr = expand_without_initialization(data_size, true);

            // 处理别名情况（源数据在当前对象内部）
            std::less_equal<const value_type*> less_equal;
            if (less_equal(old_data, byte_data) && !less_equal(old_data + old_size, byte_data)) {
                byte_data = data() + (byte_data - old_data);
                std::memmove(new_data_ptr, byte_data, data_size);
            }
            else {
                std::memcpy(new_data_ptr, byte_data, data_size);
            }

            return *this;
        }

        /**
         * @brief 追加字节序列
         * @param data_to_append 要追加的字节序列
         * @return 当前对象引用
         */
        membin& append(const membin& data_to_append) {
            return append(data_to_append.data(), data_to_append.size());
        }

        /**
         * @brief 移动追加字节序列
         * @param data_to_append 要追加的字节序列
         * @return 当前对象引用
         */
        membin& append(membin&& data_to_append) {
            return append(data_to_append.data(), data_to_append.size());
        }

        /**
         * @brief 追加初始化列表
         * @param initializer_list 初始化列表
         * @return 当前对象引用
         */
        membin& append(std::initializer_list<value_type> initializer_list) {
            return append(initializer_list.begin(), initializer_list.size());
        }

        /**
         * @brief 从迭代器范围追加
         * @tparam InputIterator 输入迭代器类型
         * @param begin_iterator 起始迭代器
         * @param end_iterator 结束迭代器
         * @return 当前对象引用
         */
        template <typename InputIterator>
        membin& append(InputIterator begin_iterator, InputIterator end_iterator) {
            size_t data_size = static_cast<size_t>(std::distance(begin_iterator, end_iterator));
            auto new_data_ptr = expand_without_initialization(data_size, true);
            std::copy(begin_iterator, end_iterator, new_data_ptr);
            return *this;
        }

        /**
         * @brief 在末尾添加一个字节
         * @param byte_value 要添加的字节值
         */
        void push_back(value_type byte_value) {
            *expand_without_initialization(1, true) = byte_value;
        }

        /**
         * @brief 赋值操作
         * @param source_data 源数据指针
         * @param data_size 数据大小
         * @return 当前对象引用
         */
        membin& assign(const void* source_data, size_t data_size) {
            if (data_size == 0) {
                clear();
            }
            else if (size() >= data_size) {
                std::memmove(mutable_data(), source_data, data_size);
                shrink_by(size() - data_size);
            }
            else {
                clear();
                std::memcpy(expand_without_initialization(data_size), source_data, data_size);
            }
            return *this;
        }

        /**
         * @brief 移动赋值
         * @param data_to_assign 要移动的字节序列
         * @return 当前对象引用
         */
        membin& assign(membin&& data_to_assign) {
            return *this = std::move(data_to_assign);
        }

        /**
         * @brief 初始化列表赋值
         * @param initializer_list 初始化列表
         * @return 当前对象引用
         */
        membin& assign(std::initializer_list<value_type> initializer_list) {
            return assign(initializer_list.begin(), initializer_list.size());
        }

        /**
         * @brief 插入数据
         * @param position 插入位置
         * @param data_pointer 数据指针
         * @param data_size 数据大小
         * @return 当前对象引用
         */
        membin& insert(size_t position, const void* data_pointer, size_t data_size) {
            if (data_size == 0) return *this;
            if (position >= size()) {
                return append(data_pointer, data_size);
            }

            auto old_size = size();
            expand_without_initialization(data_size, true);
            auto buffer_ptr = mutable_data();

            // 移动现有数据
            std::memmove(buffer_ptr + position + data_size, buffer_ptr + position, old_size - position);
            // 插入新数据
            std::memmove(buffer_ptr + position, data_pointer, data_size);

            return *this;
        }

        /**
         * @brief 插入字节序列
         * @param position 插入位置
         * @param data_to_insert 要插入的字节序列
         * @return 当前对象引用
         */
        membin& insert(size_t position, const membin& data_to_insert) {
            return insert(position, data_to_insert.data(), data_to_insert.size());
        }

        /**
         * @brief 移动插入字节序列
         * @param position 插入位置
         * @param data_to_insert 要插入的字节序列
         * @return 当前对象引用
         */
        membin& insert(size_t position, membin&& data_to_insert) {
            return insert(position, data_to_insert.data(), data_to_insert.size());
        }

        /**
         * @brief 插入初始化列表
         * @param position 插入位置
         * @param initializer_list 初始化列表
         * @return 当前对象引用
         */
        membin& insert(size_t position, std::initializer_list<value_type> initializer_list) {
            return insert(position, initializer_list.begin(), initializer_list.size());
        }

        /**
         * @brief 在迭代器位置插入
         * @tparam ForwardIterator 前向迭代器类型
         * @param iterator_position 插入位置迭代器
         * @param begin_iterator 起始迭代器
         * @param end_iterator 结束迭代器
         * @return 当前对象引用
         */
        template <typename ForwardIterator>
        membin& insert(const_iterator iterator_position, ForwardIterator begin_iterator, ForwardIterator end_iterator) {
            size_t position = iterator_position - cbegin();
            size_t data_size = static_cast<size_t>(std::distance(begin_iterator, end_iterator));
            auto old_size = size();
            expand_without_initialization(data_size, true);
            auto buffer_ptr = mutable_data();

            std::memmove(buffer_ptr + position + data_size, buffer_ptr + position, old_size - position);
            std::copy(begin_iterator, end_iterator, buffer_ptr + position);

            return *this;
        }

        // ==================== 替换操作 ====================

        /**
         * @brief 替换指定范围的字节
         * @param start_position 起始位置
         * @param replace_length 要替换的字节数
         * @param new_data_pointer 新数据指针
         * @param new_data_size 新数据大小
         * @return 当前对象引用
         */
        membin& replace(size_t start_position, size_t replace_length,
            const void* new_data_pointer, size_t new_data_size) {
            if (start_position > size()) {
                start_position = size() - 1;
            }
            if (start_position + replace_length > size()) {
                replace_length = size() - start_position;
            }

            size_t new_total_size = size() - replace_length + new_data_size;
            if (start_position == 0) {
                if (new_data_size == 0) {
                    membin temp(begin() + replace_length, new_total_size);
                    swap(temp);
                }
                else {
                    membin temp(new_data_pointer, new_data_size);
                    temp.append(begin() + replace_length, size() - replace_length);
                    swap(temp);
                }
            }
            else {
                membin temp(begin(), start_position);
                if (new_data_size > 0) {
                    temp.append(new_data_pointer, new_data_size);
                }
                if (start_position + new_data_size < new_total_size) {
                    temp.append(begin() + start_position + replace_length, size() - start_position - replace_length);
                }
                swap(temp);
            }

            return *this;
        }

        /**
         * @brief 替换为字节序列
         * @param start_position 起始位置
         * @param replace_length 要替换的字节数
         * @param replacement_data 替换的字节序列
         * @return 当前对象引用
         */
        membin& replace(size_t start_position, size_t replace_length, const membin& replacement_data = {}) {
            return replace(start_position, replace_length, replacement_data.data(), replacement_data.size());
        }

        /**
         * @brief 子序列替换
         * @param old_subsequence 要被替换的子序列
         * @param new_subsequence 用作替换的子序列
         * @param start_index 起始搜索位置
         * @param max_replace_count 最大替换次数
         * @return 当前对象引用
         */
        membin& replace_sub(const membin& old_subsequence, const membin& new_subsequence = {},
            size_t start_index = 0,
            size_t max_replace_count = std::numeric_limits<size_t>::max()) {
            if (empty() || old_subsequence.empty()) {
                return *this;
            }

            const auto* source_data = data();
            size_t source_length = size();
            const auto* target_data = old_subsequence.data();
            size_t target_length = old_subsequence.size();

            if (start_index >= source_length || target_length > source_length) {
                return *this;
            }

            const auto* replacement_data = new_subsequence.empty() ? nullptr : new_subsequence.data();
            size_t replacement_length = new_subsequence.size();

            membin result;
            const auto* first_match = source_data;
            const auto* search_start = source_data + start_index;
            size_t remaining_length = source_length;

            for (; max_replace_count > 0; --max_replace_count) {
                size_t position = internal_find(search_start, remaining_length, target_data, target_length);
                if (position == npos) break;

                if (search_start + position > first_match) {
                    result.append(first_match, search_start + position - first_match);
                }

                if (replacement_length > 0) {
                    result.append(replacement_data, replacement_length);
                }

                search_start += position + target_length;
                first_match = search_start;
                remaining_length -= position + target_length;
            }

            if (source_data + source_length - first_match > 0) {
                result.append(first_match, source_data + source_length - first_match);
            }

            *this = std::move(result);
            return *this;
        }

        // ==================== 查找操作 ====================

        /**
         * @brief 正向查找子序列
         * @param pattern 要查找的字节序列
         * @param start_offset 起始搜索位置
         * @return 找到的位置索引，未找到返回npos
         */
        [[nodiscard]] size_t find(const membin& pattern, size_t start_offset = 0) const {
            return internal_find(data(), size(), pattern.data(), pattern.size(), start_offset);
        }

        /**
         * @brief 反向查找子序列
         * @param pattern 要查找的字节序列
         * @param start_offset 起始搜索位置
         * @return 找到的位置索引，未找到返回npos
         */
        [[nodiscard]] size_t rfind(const membin& pattern, size_t start_offset = npos) const {
            return internal_reverse_find(data(), size(), pattern.data(), pattern.size(), start_offset);
        }

        // ==================== 子序列操作 ====================

        /**
         * @brief 取左侧子序列
         * @param count 要取的字节数
         * @return 新的字节序列
         */
        [[nodiscard]] membin left(size_t count) const {
            if (empty() || count == 0) return {};
            return membin(data(), std::min(count, size()));
        }

        /**
         * @brief 取右侧子序列
         * @param count 要取的字节数
         * @return 新的字节序列
         */
        [[nodiscard]] membin right(size_t count) const {
            if (empty() || count == 0) return {};
            count = std::min(count, size());
            return membin(data() + size() - count, count);
        }

        /**
         * @brief 取中间子序列
         * @param start_position 起始位置
         * @param count 要取的字节数
         * @return 新的字节序列
         */
        [[nodiscard]] membin mid(size_t start_position, size_t count = npos) const {
            if (empty() || start_position >= size()) return {};
            start_position = std::min(start_position, size());
            count = std::min(count, size() - start_position);
            return membin(data() + start_position, count);
        }
        /**
         * @brief 取左侧视图
         * @param n 要取的字节数
         * @return 字节视图
         */
        [[nodiscard]] view left_view(size_t n) const {
            return view(data(), std::min(n, size()));
        }
        /**
         * @brief 取中间视图
         * @param pos 起始位置
         * @param n 要取的字节数
         * @return 字节视图
         */
        [[nodiscard]] view mid_view(size_t pos, size_t n = -1) const {
            if (pos >= size()) return { nullptr, 0 };
            return view(data() + pos, std::min(n, size() - pos));
        }
        /**
         * @brief 取右侧视图
         * @param n 要取的字节数
         * @return 字节视图
         */
        [[nodiscard]] view right_view(size_t n) const {
            if (n >= size()) return view(data(), size());
            return view(data() + (size() - n), n);
        }
        /**
         * @brief 取子序列（middle_subsequence的别名）
         * @param start_position 起始位置
         * @param count 要取的字节数
         * @return 新的字节序列
         */
        [[nodiscard]] membin submem(size_t start_position, size_t count = npos) const {
            return mid(start_position, count);
        }

        // ==================== 转换操作 ====================

        /**
         * @brief 转换为十进制文本格式
         * @tparam CharType 字符类型
         * @return 格式如"{1,2,3}"的字符串
         */
        template<typename CharType = char>
        [[nodiscard]] std::basic_string<CharType> decimal() const {
            return _conversion::to_decimal<CharType>(data(), size());
        }

        /**
         * @brief 转换为十六进制文本
         * @tparam CharType 字符类型
         * @param use_lowercase 是否使用小写字母
         * @return 十六进制字符串
         */
        template<typename CharType = char>
        [[nodiscard]] std::basic_string<CharType> hex(bool use_lowercase = true) const {
            return _conversion::to_hex<CharType>(data(), size(), use_lowercase);
        }

        /**
         * @brief Base64编码
         * @tparam CharType 字符类型
         * @param alphabet Base64字母表
         * @return Base64编码字符串
         */
        template<typename CharType = char>
        [[nodiscard]] std::basic_string<CharType> base64(
            const std::basic_string<CharType>& alphabet =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/") {

            return _conversion::to_base64<CharType>(data(), size(), alphabet);
        }

        /**
         * @brief 提取指定类型的数据
         * @tparam T 数据类型
         * @param offset 偏移量
         * @return 转换后的数据
         */
        template<typename T>
        [[nodiscard]] T extract_data(size_t offset) const {
            static_assert(std::is_trivially_copyable_v<T>,
                "T必须是可平凡复制的类型");

            T value{};
            if (offset + sizeof(T) <= size()) {
                std::memcpy(&value, data() + offset, sizeof(T));
            }
            return value;
        }
        /**
        * @brief 从指定偏移处读取一个数值
        * @tparam T 目标数值类型 (如 uint32_t, int16_t 等)
        * @param offset 读取的起始偏移
        * @param endian 指定字节序，默认为系统原生 (Native)
        * @return 读取到的数值
        * @throws std::out_of_range 如果读取范围越界
        */
        template <typename T>
        [[nodiscard]] T extract_num(size_t offset, Endianness endian = Endianness::Native) const {
            static_assert(std::is_arithmetic_v<T>, "T 必须为算术类型");

            if (offset + sizeof(T) > size()) {
                throw std::out_of_range("get_num 读取越界");
            }

            T value;
            // 使用 std::memcpy 避免对齐问题
            std::memcpy(&value, data() + offset, sizeof(T));

            // 如果指定字节序与原生字节序不同，则进行转换
            // 注意：这里假设转换逻辑只针对跨字节读取，对于char等单字节类型无需转换
            if constexpr (sizeof(T) > 1) {
                if (endian != Endianness::Native) {
                    // 如果系统是小端，而要求大端，或者系统是大端，而要求小端
                    // 通过比较 bool 值判断是否需要交换
                    bool native_is_little = (std::endian::native == std::endian::little);
                    bool target_is_little = (endian == Endianness::LittleEndian);

                    if (native_is_little != target_is_little) {
                        value = _conversion::swap_endian(value);
                    }
                }
            }

            return value;
        }
        /**
         * @brief 反转字节序
         * @tparam T 数据类型
         * @param start_offset 起始位置
         * @param end_offset 结束位置
         * @return 当前对象引用
         * @throws std::invalid_argument 如果范围不是类型大小的倍数
         */
        template <typename T>
        membin& reverse_endianness(size_t start_offset = 0, size_t end_offset = npos) {
            static_assert(std::is_arithmetic_v<T>,
                "T必须是算术类型（如int, float, double）");

            if (end_offset == npos || end_offset > size()) {
                end_offset = size();
            }

            size_t total_bytes = end_offset - start_offset;
            constexpr size_t type_size = sizeof(T);

            if (total_bytes % type_size != 0) {
                throw std::invalid_argument("范围大小必须是类型大小的倍数");
            }

            for (size_t i = start_offset; i < end_offset; i += type_size) {
                std::reverse(begin() + i, begin() + i + type_size);
            }

            return *this;
        }

        /**
         * @brief 反转整个字节序列
         * @return 当前对象引用
         */
        membin& reverse() {
            std::reverse(begin(), end());
            return *this;
        }

        /**
         * @brief 分割字节序列
         * @param separator 分隔符字节序列
         * @param max_split_count 最大分割次数
         * @return 分割后的字节序列数组
         */
        [[nodiscard]] std::vector<membin> split(const membin& separator = { 0 },
            size_t max_split_count = std::numeric_limits<size_t>::max()) const {
            if (empty() || separator.empty() || max_split_count == 0) {
                return {};
            }

            std::vector<membin> result;
            const value_type* source_data = data();
            size_t source_length = size();
            size_t separator_length = separator.size();

            const value_type* current_position = source_data;
            const value_type* end_position = source_data + source_length;

            for (size_t count = 0; count < max_split_count - 1 && current_position < end_position; ++count) {
                size_t position = internal_find(current_position, end_position - current_position,
                    separator.data(), separator_length, 0);
                if (position == npos) break;

                result.emplace_back(current_position, current_position + position);
                current_position += position + separator_length;
            }

            if (current_position < end_position) {
                result.emplace_back(current_position, end_position);
            }

            return result;
        }

        // ==================== 静态工厂方法 ====================

        /**
         * @brief 从Base64字符串解码
         * @tparam CharType 字符类型
         * @param base64_string Base64字符串
         * @param remove_padding 是否移除填充字符
         * @param alphabet Base64字母表
         * @return 解码后的字节序列
         */
        template<typename CharType = char>
        [[nodiscard]] static membin from_base64(
            const std::basic_string<CharType>& base64_string,
            bool remove_padding = true,
            const std::basic_string<CharType>& alphabet =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/") {

            if (base64_string.empty()) return {};

            // 创建解码表
            value_type decode_table[256] = { 0 };
            for (size_t i = 0; i < alphabet.size(); ++i) {
                decode_table[static_cast<value_type>(alphabet[i])] = static_cast<value_type>(i);
            }

            // 计算输出大小
            size_t input_length = base64_string.size();
            size_t padding_count = 0;

            if (input_length >= 2 && base64_string[input_length - 1] == '=') {
                padding_count++;
                if (input_length >= 2 && base64_string[input_length - 2] == '=') {
                    padding_count++;
                }
            }

            size_t output_length = (input_length * 3) / 4 - padding_count;
            membin result(output_length);

            // 解码
            const CharType* input_data = base64_string.data();
            value_type* output_data = result.mutable_data();
            size_t output_index = 0;

            uint32_t buffer = 0;
            int bits_collected = 0;

            for (size_t i = 0; i < input_length; ++i) {
                CharType character = input_data[i];
                if (character == '=') break;

                value_type value = decode_table[static_cast<value_type>(character)];
                buffer = (buffer << 6) | value;
                bits_collected += 6;

                if (bits_collected >= 8) {
                    bits_collected -= 8;
                    output_data[output_index++] = static_cast<value_type>((buffer >> bits_collected) & 0xFF);
                }
            }

            if (remove_padding) {
                result.resize(output_index);
            }

            return result;
        }

        /**
         * @brief 从Base64字符串解码（C风格字符串版本）
         * @tparam CharType 字符类型
         * @param base64_cstring Base64 C风格字符串
         * @param remove_padding 是否移除填充字符
         * @param alphabet_cstring Base64字母表C风格字符串
         * @return 解码后的字节序列
         */
        template<typename CharType = char>
        [[nodiscard]] static membin from_base64(
            const CharType* base64_cstring,
            bool remove_padding = true,
            const CharType* alphabet_cstring =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/") {
            return from_base64(std::basic_string<CharType>(base64_cstring),
                remove_padding, std::basic_string<CharType>(alphabet_cstring));
        }

        /**
         * @brief 从十六进制字符串解码
         * @tparam CharType 字符类型
         * @param hex_string 十六进制字符串
         * @return 解码后的字节序列
         */
        template<typename CharType = char>
        [[nodiscard]] static membin from_hex(const std::basic_string<CharType>& hex_string) {
            if (hex_string.empty()) return {};

            // 创建十六进制字符到数值的映射表
            value_type hex_map[256] = { 0 };
            for (int i = 0; i < 10; ++i) {
                hex_map['0' + i] = static_cast<value_type>(i);
            }
            for (int i = 0; i < 6; ++i) {
                hex_map['A' + i] = hex_map['a' + i] = static_cast<value_type>(10 + i);
            }

            // 过滤非十六进制字符并计算有效长度
            size_t hex_length = 0;
            for (CharType character : hex_string) {
                if (hex_map[static_cast<value_type>(character)] != 0 ||
                    (character >= '0' && character <= '9') ||
                    (character >= 'A' && character <= 'F') ||
                    (character >= 'a' && character <= 'f')) {
                    hex_length++;
                }
            }

            if (hex_length % 2 != 0) {
                throw std::invalid_argument("十六进制字符串长度必须为偶数");
            }

            membin result(hex_length / 2);
            value_type* output_data = result.mutable_data();
            size_t output_index = 0;

            value_type high_nibble = 0;
            bool has_high_nibble = false;

            for (CharType character : hex_string) {
                value_type value = hex_map[static_cast<value_type>(character)];
                if (value == 0 && !(character >= '0' && character <= '9') &&
                    !(character >= 'A' && character <= 'F') && !(character >= 'a' && character <= 'f')) {
                    continue; // 跳过无效字符
                }

                if (!has_high_nibble) {
                    high_nibble = value << 4;
                    has_high_nibble = true;
                }
                else {
                    output_data[output_index++] = high_nibble | value;
                    has_high_nibble = false;
                }
            }

            return result;
        }

        /**
         * @brief 从十六进制字符串解码（C风格字符串版本）
         * @tparam CharType 字符类型
         * @param hex_cstring 十六进制C风格字符串
         * @return 解码后的字节序列
         */
        template<typename CharType>
        [[nodiscard]] static membin from_hex(const CharType* hex_cstring) {
            return from_hex(std::basic_string<CharType>(hex_cstring));
        }

        /**
         * @brief 生成随机字节序列
         * @param size 字节序列大小
         * @return 随机字节序列
         */
        [[nodiscard]] static membin from_random(size_t size) {
            membin result(size);

            std::random_device random_device;
            std::mt19937_64 generator(random_device());
            std::uniform_int_distribution<uint16_t> distribution(0, 255);

            value_type* data_ptr = result.mutable_data();
            for (size_t i = 0; i < size; ++i) {
                data_ptr[i] = static_cast<value_type>(distribution(generator));
            }

            return result;
        }

        /**
         * @brief 从文件读取内容
         * @tparam CharType 字符类型
         * @param filename 文件名
         * @return 文件内容字节序列
         */
        template<typename CharType = char>
        [[nodiscard]] static membin from_file(const std::basic_string<CharType>& filename) {
            std::basic_ifstream<CharType> file(filename, std::ios::binary | std::ios::ate);
            if (!file) {
                return {};
            }

            std::streamsize file_size = file.tellg();
            file.seekg(0, std::ios::beg);

            membin result(static_cast<size_t>(file_size));
            if (file_size > 0) {
                file.read(reinterpret_cast<CharType*>(result.mutable_data()), file_size);
            }

            return result;
        }

        /**
         * @brief 从文件读取内容（C风格字符串版本）
         * @tparam CharType 字符类型
         * @param filename_cstring 文件名C风格字符串
         * @return 文件内容字节序列
         */
        template<typename CharType>
        [[nodiscard]] static membin from_file(const CharType* filename_cstring) {
            return from_file(std::basic_string<CharType>(filename_cstring));
        }

        /**
         * @brief 写入文件
         * @tparam CharType 字符类型
         * @param filename 文件名
         * @return true如果写入成功
         */
        template<typename CharType = char>
        bool write_to_file(const std::basic_string<CharType>& filename) const {
            std::basic_ofstream<CharType> file(filename, std::ios::binary);
            if (!file) {
                return false;
            }

            if (!empty()) {
                file.write(reinterpret_cast<const CharType*>(data()),
                    static_cast<std::streamsize>(size()));
            }

            return file.good();
        }

        /**
         * @brief 写入文件（C风格字符串版本）
         * @tparam CharType 字符类型
         * @param filename_cstring 文件名C风格字符串
         * @return true如果写入成功
         */
        template<typename CharType>
        bool write_to_file(const CharType* filename_cstring) {
            return write_to_file(std::basic_string<CharType>(filename_cstring));
        }

        /**
         * @brief 转换为字符串
         * @tparam CharType 字符类型
         * @return 字符串表示
         */
        template<typename CharType = char>
        [[nodiscard]] std::basic_string<CharType> to_string() const {
            return _conversion::to_string<CharType>(data(), size());
        }
    public:
            operator std::span<const uint8_t>() const noexcept {
                return { data(), size() };
            }
    private:
        // ==================== 私有辅助方法 ====================

        /**
         * @brief 获取数据存储类别
         * @return 当前存储类别
         */
        [[nodiscard]] StorageCategory storage_category() const noexcept {
            return static_cast<StorageCategory>(raw_bytes[kLastByteIndex] & kCategoryExtractMask);
        }

        /**
         * @brief 获取小数据存储的大小
         * @return 小数据存储的实际大小
         */
        [[nodiscard]] size_t small_storage_size() const noexcept {
            constexpr auto shift = kIsLittleEndian ? 0 : 2;
            auto small_shifted = static_cast<size_t>(small_data[kMaxSmallSize]) >> shift;
            return kMaxSmallSize - small_shifted;
        }

        /**
         * @brief 设置小数据存储的大小
         * @param new_size 新的大小
         */
        void set_small_storage_size(size_t new_size) noexcept {
            constexpr auto shift = kIsLittleEndian ? 0 : 2;
            small_data[kMaxSmallSize] = static_cast<value_type>((kMaxSmallSize - new_size) << shift);
        }

        /**
         * @brief 重置为空状态
         */
        void reset_to_empty() noexcept {
            set_small_storage_size(0);
        }



        /**
         * @brief 销毁中大数据存储
         */
        void destroy_medium_large_storage() noexcept {
            if (is_med()) {
                std::free(medium_large.data_ptr);
            }
            else if (is_lrg()) {
                RefCountedBlock::decrement_reference_count(medium_large.data_ptr);
            }
        }

        /**
         * @brief 扩展内存空间（不初始化新增部分）
         * @param growth_amount 需要扩展的字节数
         * @param use_exponential_growth 是否使用指数增长策略
         * @return 新扩展区域的首地址
         */
        value_type* expand_without_initialization(size_t growth_amount, bool use_exponential_growth = false) {
            size_t old_size, new_size;

            if (storage_category() == StorageCategory::isSmall) {
                old_size = small_storage_size();
                new_size = old_size + growth_amount;
                if (new_size <= kMaxSmallSize) {
                    set_small_storage_size(new_size);
                    return small_data + old_size;
                }
                reserve_small_storage(use_exponential_growth ?
                    std::max(new_size, 2 * kMaxSmallSize) : new_size);
            }
            else {
                old_size = medium_large.current_size;
                new_size = old_size + growth_amount;
                if (new_size > capacity()) {
                    reserve(use_exponential_growth ?
                        std::max(new_size, 1 + capacity() * 3 / 2) : new_size);
                }
            }

            medium_large.current_size = new_size;
            return medium_large.data_ptr + old_size;
        }

        /**
         * @brief 缩小字节序列
         * @param shrink_amount 需要缩小的字节数
         */
        void shrink_by(size_t shrink_amount) {
            if (shrink_amount == 0) return;

            if (is_small()) {
                set_small_storage_size(small_storage_size() - shrink_amount);
            }
            else if (is_med() ||
                RefCountedBlock::get_reference_count(medium_large.data_ptr) == 1) {
                medium_large.current_size -= shrink_amount;
            }
            else {
                membin(medium_large.data_ptr, medium_large.current_size - shrink_amount).swap(*this);
            }
        }

        /**
         * @brief 初始化小数据存储
         * @param source_data 源数据指针
         * @param data_size 数据大小
         */
        void initialize_small_storage(const value_type* source_data, size_t data_size) {
            if (data_size > 0) {
                std::memcpy(small_data, source_data, data_size);
            }
            set_small_storage_size(data_size);
        }

        /**
         * @brief 初始化中数据存储
         * @param source_data 源数据指针
         * @param data_size 数据大小
         */
        void initialize_medium_storage(const value_type* source_data, size_t data_size) {
            medium_large.data_ptr = static_cast<value_type*>(_imp::checked_malloc(data_size));
            if (data_size > 0) {
                std::memcpy(medium_large.data_ptr, source_data, data_size);
            }
            medium_large.current_size = data_size;
            medium_large.set_capacity_with_category(data_size, StorageCategory::isMedium);
        }

        /**
         * @brief 初始化大数据存储
         * @param source_data 源数据指针
         * @param data_size 数据大小
         */
        void initialize_large_storage(const value_type* source_data, size_t data_size) {
            size_t effective_capacity = data_size;
            auto new_block = RefCountedBlock::create_block_with_data(source_data, &effective_capacity);

            medium_large.data_ptr = new_block->data_start;
            medium_large.current_size = data_size;
            medium_large.set_capacity_with_category(effective_capacity, StorageCategory::isLarge);
        }

        /**
         * @brief 拷贝小数据存储
         * @param source 源对象
         */
        void copy_small_storage(const membin& source) {
            medium_large = source.medium_large;
        }

        /**
         * @brief 拷贝中数据存储
         * @param source 源对象
         */
        void copy_medium_storage(const membin& source) {
            medium_large.data_ptr = static_cast<value_type*>(_imp::checked_malloc(source.medium_large.current_size));
            std::memcpy(medium_large.data_ptr, source.medium_large.data_ptr, source.medium_large.current_size);
            medium_large.current_size = source.medium_large.current_size;
            medium_large.set_capacity_with_category(source.medium_large.current_size, StorageCategory::isMedium);
        }

        /**
         * @brief 拷贝大数据存储
         * @param source 源对象
         */
        void copy_large_storage(const membin& source) {
            medium_large = source.medium_large;
            RefCountedBlock::increment_reference_count(medium_large.data_ptr);
        }

        /**
         * @brief 为小数据存储预留容量
         * @param min_capacity 最小容量要求
         */
        void reserve_small_storage(size_t min_capacity) {
            if (min_capacity <= kMaxSmallSize) {
                return; // 小容量不需要扩展
            }

            size_t current_size = small_storage_size();

            if (min_capacity <= kMaxMediumSize) {
                // 转换为中数据存储
                auto new_data_ptr = static_cast<value_type*>(_imp::checked_malloc(min_capacity));
                std::memcpy(new_data_ptr, small_data, current_size);

                medium_large.data_ptr = new_data_ptr;
                medium_large.current_size = current_size;
                medium_large.set_capacity_with_category(min_capacity, StorageCategory::isMedium);
            }
            else {
                // 转换为大数据存储
                size_t capacity = min_capacity;
                auto new_block = RefCountedBlock::create_block(&capacity);
                std::memcpy(new_block->data_start, small_data, current_size);

                medium_large.data_ptr = new_block->data_start;
                medium_large.current_size = current_size;
                medium_large.set_capacity_with_category(capacity, StorageCategory::isLarge);
            }
        }

        /**
         * @brief 为中数据存储预留容量
         * @param min_capacity 最小容量要求
         */
        void reserve_medium_storage(size_t min_capacity) {
            if (min_capacity <= medium_large.actual_capacity()) {
                return; // 容量足够
            }

            if (min_capacity <= kMaxMediumSize) {
                // 保持中数据存储，重新分配
                medium_large.data_ptr = static_cast<value_type*>(
                    _imp::smart_realloc(medium_large.data_ptr, medium_large.current_size,
                        medium_large.actual_capacity(), min_capacity));
                medium_large.set_capacity_with_category(min_capacity, StorageCategory::isMedium);
            }
            else {
                // 转换为大数据存储
                membin temporary;
                temporary.reserve(min_capacity);
                temporary.medium_large.current_size = medium_large.current_size;
                std::memcpy(temporary.medium_large.data_ptr, medium_large.data_ptr, medium_large.current_size);
                temporary.swap(*this);
            }
        }

        /**
         * @brief 为大数据存储预留容量
         * @param min_capacity 最小容量要求
         */
        void reserve_large_storage(size_t min_capacity) {
            if (RefCountedBlock::get_reference_count(medium_large.data_ptr) > 1) {
                // 数据被共享，需要解除共享
                unshare(min_capacity);
            }
            else if (min_capacity > medium_large.actual_capacity()) {
                // 数据未被共享，但需要更多容量
                size_t new_capacity = min_capacity;
                auto new_block = RefCountedBlock::reallocate_block(medium_large.data_ptr, medium_large.current_size,
                    medium_large.actual_capacity(), &new_capacity);
                medium_large.data_ptr = new_block->data_start;
                medium_large.set_capacity_with_category(new_capacity, StorageCategory::isLarge);
            }
        }

        /**
         * @brief 解除共享（确保数据唯一）
         * @param min_capacity 最小容量要求
         */
        void unshare(size_t min_capacity = 0) {
            size_t effective_capacity = std::max(min_capacity, medium_large.actual_capacity());
            auto new_block = RefCountedBlock::create_block(&effective_capacity);

            std::memcpy(new_block->data_start, medium_large.data_ptr, medium_large.current_size);
            RefCountedBlock::decrement_reference_count(medium_large.data_ptr);

            medium_large.data_ptr = new_block->data_start;
            medium_large.set_capacity_with_category(effective_capacity, StorageCategory::isLarge);
        }

        /**
         * @brief 获取可修改的大数据指针
         * @return 指向可修改的大数据的指针
         */
        value_type* get_mutable_large_data() {
            if (RefCountedBlock::get_reference_count(medium_large.data_ptr) > 1) {
                unshare();
            }
            return medium_large.data_ptr;
        }

        /**
         * @brief 查找算法实现
         * @param haystack 目标数据指针
         * @param haystack_length 目标数据长度
         * @param needle 要查找的数据指针
         * @param needle_length 要查找的数据长度
         * @param start_offset 起始偏移量
         * @return 找到的位置索引，未找到返回npos
         */
        static size_t internal_find(const value_type* haystack, size_t haystack_length,
            const value_type* needle, size_t needle_length,
            size_t start_offset = 0) {
            if (haystack == nullptr || haystack_length == 0 || needle == nullptr || needle_length == 0)
                return npos;

            if (needle_length > haystack_length)
                return npos;

            if (start_offset > haystack_length)
                return npos;

            if (needle_length + start_offset > haystack_length)
                return npos;

            const size_t search_length = haystack_length - start_offset;
            const auto* search_start = haystack + start_offset;
            auto match_iterator = std::search(search_start, search_start + search_length, needle, needle + needle_length);

            if (match_iterator != search_start + search_length) {
                return match_iterator - haystack;
            }
            return npos;
        }

        /**
         * @brief 反向查找算法实现
         * @param source_data 源数据缓冲区
         * @param source_length 源数据有效长度
         * @param target_pattern 目标模式数据
         * @param pattern_length 目标模式长度
         * @param max_offset 最大搜索偏移
         * @return 找到的位置索引，未找到返回npos
         */
        static size_t internal_reverse_find(
            const value_type* source_data,
            size_t source_length,
            const value_type* target_pattern,
            size_t pattern_length,
            size_t max_offset)
        {
            if (source_data == nullptr || target_pattern == nullptr)
                return npos;

            if (max_offset <= source_length)
                source_length = max_offset;

            if (source_length == 0 || pattern_length == 0 || pattern_length > source_length)
                return npos;

            size_t offset = source_length - pattern_length;

            if (pattern_length == 1) {
                for (size_t i = offset; ; --i) {
                    if (source_data[i] == *target_pattern) {
                        return i;
                    }
                    if (i == 0) break;
                }
                return npos;
            }

            size_t skip_table[256];
            for (size_t i = 0; i < 256; i++) {
                skip_table[i] = pattern_length;
            }
            for (size_t i = pattern_length; i > 0; i--) {
                skip_table[target_pattern[i - 1]] = i;
            }

            // 使用优化的Boyer-Moore算法进行匹配
            for (const unsigned char* current_address = source_data + offset;
                current_address >= source_data;
                current_address -= skip_table[current_address[-1]]) {
                if (std::memcmp(current_address, target_pattern, pattern_length) == 0) {
                    return current_address - source_data;
                }
            }
            return npos;
        }

        /**
         * @brief 反向查找算法实现（无最大偏移限制）
         * @param source_data 源数据缓冲区
         * @param source_length 源数据有效长度
         * @param target_pattern 目标模式数据
         * @param pattern_length 目标模式长度
         * @return 找到的位置索引，未找到返回npos
         */
        static size_t internal_reverse_find(
            const value_type* source_data,
            size_t source_length,
            const value_type* target_pattern,
            size_t pattern_length)
        {
            return internal_reverse_find(source_data, source_length, target_pattern, pattern_length, source_length);
        }
    };

    // ==================== 全局操作符 ====================

    /**
     * @brief 输出流操作符
     * @tparam CharType 字符类型
     * @param output_stream 输出流
     * @param binary_data 字节集对象
     * @return 输出流引用
     */
    template <typename CharType>
    inline std::basic_ostream<CharType>& operator<<(
        std::basic_ostream<CharType>& output_stream, const membin& binary_data) {
        auto decimal_string = binary_data.decimal<CharType>();
        output_stream << decimal_string;
        return output_stream;
    }

    /**
     * @brief 连接操作符（两个常量引用）
     * @param left_operand 左操作数
     * @param right_operand 右操作数
     * @return 连接后的新字节集
     */
    inline membin operator+(const membin& left_operand, const membin& right_operand) {
        membin result;
        result.reserve(left_operand.size() + right_operand.size());
        result.append(left_operand).append(right_operand);
        return result;
    }

    /**
     * @brief 连接操作符（左值移动）
     * @param left_operand 左操作数（将被移动）
     * @param right_operand 右操作数
     * @return 连接后的字节集
     */
    inline membin operator+(membin&& left_operand, const membin& right_operand) {
        if (left_operand.capacity() >= left_operand.size() + right_operand.size()) {
            return std::move(left_operand.append(right_operand));
        }
        return left_operand + right_operand;
    }

    /**
     * @brief 连接操作符（右值移动）
     * @param left_operand 左操作数
     * @param right_operand 右操作数（将被移动）
     * @return 连接后的字节集
     */
    inline membin operator+(const membin& left_operand, membin&& right_operand) {
        return std::move(right_operand.insert(0, left_operand));
    }

    /**
     * @brief 连接操作符（双移动）
     * @param left_operand 左操作数（将被移动）
     * @param right_operand 右操作数（将被移动）
     * @return 连接后的字节集
     */
    inline membin operator+(membin&& left_operand, membin&& right_operand) {
        return std::move(left_operand.append(std::move(right_operand)));
    }

    /**
     * @brief 连接操作符（字节集 + 初始化列表）
     * @param left_operand 左操作数（字节集）
     * @param right_operand 右操作数（初始化列表）
     * @return 连接后的新字节集
     */
    inline membin operator+(const membin& left_operand, std::initializer_list<int> right_operand) {
        membin result;
        result.reserve(left_operand.size() + right_operand.size());
        result.append(left_operand);
        for (const auto& byte_value : right_operand) {
            if (byte_value < 0 || byte_value > 255) {
                throw std::out_of_range("初始化列表中的整数值必须在0到255之间");
            }
            result.push_back(static_cast<uint8_t>(byte_value));
        }
        return result;
    }

    /**
     * @brief 连接操作符（初始化列表 + 字节集）
     * @param left_operand 左操作数（初始化列表）
     * @param right_operand 右操作数（字节集）
     * @return 连接后的新字节集
     */
    inline membin operator+(std::initializer_list<int> left_operand, const membin& right_operand) {
        membin result;
        result.reserve(left_operand.size() + right_operand.size());
        for (const auto& byte_value : left_operand) {
            if (byte_value < 0 || byte_value > 255) {
                throw std::out_of_range("初始化列表中的整数值必须在0到255之间");
            }
            result.push_back(static_cast<uint8_t>(byte_value));
        }
        result.append(right_operand);
        return result;
    }

    /**
     * @brief 相等比较操作符
     * @param left_operand 左操作数
     * @param right_operand 右操作数
     * @return true如果两个字节集相等
     */
    inline bool operator==(const membin& left_operand, const membin& right_operand) {
        if (&left_operand == &right_operand) {
            return true;
        }
        return left_operand.size() == right_operand.size() &&
            std::equal(left_operand.begin(), left_operand.end(), right_operand.begin());
    }

    /**
     * @brief 不等比较操作符
     * @param left_operand 左操作数
     * @param right_operand 右操作数
     * @return true如果两个字节集不相等
     */
    inline bool operator!=(const membin& left_operand, const membin& right_operand) {
        return !(left_operand == right_operand);
    }

    // ==================== 转换函数 ====================
    /********
    * 
	*  直接转换对于底层来说实际上是不严谨的,例如字符串的编码问题,数值类型的字节序问题,以及一些复杂类型的内存布局问题等,
    *  但是大部分情况尤其是业务层是可以满足需求的,所以提供一些直接转换的函数,但需要用户自己确保使用时的正确性和合理性.
    * 
    ******/
    /**
     * @brief 从各种平凡类型转换为字节集,不建议数值类型直接通过此代码进行转换
     * @tparam T 源类型
     * @param data 源数据
     * @return 转换后的字节集
     */
    template<typename T>
    membin to_membin(const T& data) {
        static_assert(std::is_trivially_copyable_v<T>,
            "T必须是可平凡复制的类型");
        return membin(&data, sizeof(T));
    }

    /**
     * @brief 从C字符串转换为字节集
     * @param c_string C字符串
     * @return 转换后的字节集
     */
    inline membin to_membin(const char* c_string) {
        return membin(c_string, std::strlen(c_string));
    }

    /**
     * @brief 从宽字符串转换为字节集
     * @param wide_string 宽字符串
     * @return 转换后的字节集
     */
    inline membin to_membin(const wchar_t* wide_string) {
        return membin(wide_string, std::wcslen(wide_string) * sizeof(wchar_t));
    }

    /**
     * @brief 从UTF-8字符串转换为字节集
     * @param utf8_string UTF-8字符串
     * @return 转换后的字节集
     */
    inline membin to_membin(const char8_t* utf8_string) {
        return membin(utf8_string, std::char_traits<char8_t>::length(utf8_string) * sizeof(char8_t));
    }

    /**
     * @brief 从UTF-16字符串转换为字节集
     * @param utf16_string UTF-16字符串
     * @return 转换后的字节集
     */
    inline membin to_membin(const char16_t* utf16_string) {
        return membin(utf16_string, std::char_traits<char16_t>::length(utf16_string) * sizeof(char16_t));
    }

    /**
     * @brief 从UTF-32字符串转换为字节集
     * @param utf32_string UTF-32字符串
     * @return 转换后的字节集
     */
    inline membin to_membin(const char32_t* utf32_string) {
        return membin(utf32_string, std::char_traits<char32_t>::length(utf32_string) * sizeof(char32_t));
    }

    /**
     * @brief 从字符串转换为字节集
     * @tparam CharType 字符类型
     * @param string 字符串
     * @return 转换后的字节集
     */
    template <typename CharType>
    inline membin to_membin(const std::basic_string<CharType>& string) {
        return membin(string.data(), string.size() * sizeof(CharType));
    }

    /**
     * @brief 从向量转换为字节集
     * @tparam T 元素类型
     * @param vector 向量
     * @return 转换后的字节集
     */
    template <typename T>
    membin to_membin(const std::vector<T>& vector) {
        if (vector.empty()) {
            return {};
        }
        return membin(vector.data(), vector.size() * sizeof(T));
    }
    // ==================== 严谨字节序转换函数 ====================


    /**
    * @brief 将数值转换为 membin 容器
    * @tparam T 可计算数值类型
    * @param num 需要转换的数值
    * @param order 目标字节序
    */
    template <typename T>
    static membin to_membin(T num, membin::Endianness order) {
        static_assert(std::is_arithmetic_v<T>, "必须为可计算类型");

        T data = num;
        // 如果当前系统序与目标序不一致，则转换
        if constexpr (std::endian::native == std::endian::little) {
            if (order == membin::Endianness::BigEndian) data = _conversion::swap_endian(data);
        }
        else {
            if (order == membin::Endianness::LittleEndian) data = _conversion:::swap_endian(data);
        }

        return membin(&data, sizeof(T));
    }


} // namespace krnln

#endif // MEMBIN_HPP



