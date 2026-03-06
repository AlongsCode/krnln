#ifndef KRNLN_PROTOBUF_OPT_HPP
#define KRNLN_PROTOBUF_OPT_HPP




#include <cstdint>
#include <vector>
#include <string>
#include <stdexcept>
#include <cstring>
#include <concepts>
#include <optional>
#include <span>
#include <unordered_map>
#include "membin.hpp"
#include "json.hpp"
#include <algorithm>
#include <string_view>

namespace krnln {
    namespace protobuf {

        // ==================== 错误处理优化 ====================
        class DecodeError : public std::runtime_error {
            size_t offset_;
            std::string context_;
        public:
            explicit DecodeError(const std::string& msg, size_t offset = 0,
                std::string_view context = "")
                : std::runtime_error(format_message(msg, offset, context))
                , offset_(offset), context_(context) {
            }

            size_t offset() const noexcept { return offset_; }
            const std::string& context() const noexcept { return context_; }

        private:
            static std::string format_message(const std::string& msg,
                size_t offset,
                std::string_view context) {
                std::string result = "Protobuf decode error at offset ";
                result += std::to_string(offset);
                if (!context.empty()) {
                    result += " (";
                    result += context;
                    result += ")";
                }
                result += ": ";
                result += msg;
                return result;
            }
        };

        enum class WireType : uint8_t {
            Varint = 0,
            Bit64 = 1,
            LengthDelimited = 2,
            StartGroup = 3,
            EndGroup = 4,
            Bit32 = 5
        };

        // ==================== Buffer 优化 ====================
        class Buffer {
            membin buf_;
            size_t reserve_size_;

            // 内联辅助函数
            static void encode_varint(uint64_t v, std::back_insert_iterator<membin> it) {
                while (v >= 0x80) {
                    *it++ = static_cast<uint8_t>((v & 0x7F) | 0x80);
                    v >>= 7;
                }
                *it++ = static_cast<uint8_t>(v);
            }

        public:
            Buffer(size_t initial_capacity = 128) : reserve_size_(initial_capacity) {
                buf_.reserve(initial_capacity);
            }

            // 重置缓冲区，复用内存
            void reset() noexcept {
                buf_.clear();
            }

            // 获取当前大小
            size_t size() const noexcept { return buf_.size(); }

            // 写入varint（优化：内联实现）
            void writeVarint(uint64_t v) noexcept {
                while (v >= 0x80) {
                    buf_.push_back(static_cast<uint8_t>((v & 0x7F) | 0x80));
                    v >>= 7;
                }
                buf_.push_back(static_cast<uint8_t>(v));
            }

            // 写入zigzag编码的varint
            void writeZigZag(int64_t s) noexcept {
                writeVarint((static_cast<uint64_t>(s) << 1) ^
                    static_cast<uint64_t>(s >> 63));
            }

            // 写入固定长度数据（模板化）
            template <typename T>
            void writeFixed(T v) noexcept {
                static_assert(std::is_trivially_copyable_v<T>,
                    "T must be trivially copyable");
                const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
                buf_.insert(buf_.end(), p, p + sizeof(T));
            }

            // 直接写入字节数据
            void writeBytes(const uint8_t* data, size_t n) {
                buf_.insert(buf_.end(), data, data + n);
            }

            // 写入长度前缀的数据
            void writeLengthDelimited(const uint8_t* data, size_t n) {
                writeVarint(n);
                writeBytes(data, n);
            }

            // 写入field header（优化：合并操作）
            void writeField(int field, WireType wt) noexcept {
                writeVarint((uint64_t(field) << 3) | uint64_t(wt));
            }

            // 组合操作：field + varint
            void writeFieldVarint(int field, uint64_t v) noexcept {
                writeField(field, WireType::Varint);
                writeVarint(v);
            }

            // 组合操作：field + sint
            void writeFieldSInt(int field, int64_t s) noexcept {
                writeField(field, WireType::Varint);
                writeZigZag(s);
            }

            // 组合操作：field + fixed32
            void writeFieldFixed32(int field, uint32_t v) noexcept {
                writeField(field, WireType::Bit32);
                writeFixed(v);
            }

            // 组合操作：field + fixed64
            void writeFieldFixed64(int field, uint64_t v) noexcept {
                writeField(field, WireType::Bit64);
                writeFixed(v);
            }

            // 组合操作：field + string
            void writeFieldString(int field, const std::string& s) {
                writeField(field, WireType::LengthDelimited);
                writeLengthDelimited(reinterpret_cast<const uint8_t*>(s.data()), s.size());
            }

            // 组合操作：field + bytes
            void writeFieldBytes(int field, const membin& d) {
                writeField(field, WireType::LengthDelimited);
                writeLengthDelimited(d.data(), d.size());
            }

            // 直接访问数据
            const membin& data() const noexcept { return buf_; }
            membin release() noexcept { return std::move(buf_); }

            // 获取写指针（用于直接写入）
            uint8_t* write_ptr(size_t needed) {
                size_t old_size = buf_.size();
                buf_.resize(old_size + needed);
                return buf_.mutable_data() + old_size;
            }
        };

        // ==================== 解码器优化 ====================
        class reader {
        public:
            struct FieldView {
                int field;
                WireType wt;
                const uint8_t* ptr;
                size_t len;

                uint64_t as_uint() const;
                int64_t as_sint() const;
                std::string as_string() const;
                std::string_view as_view() const;  // 新增：零拷贝视图
                reader as_reader() const;
                krnln::membin as_bin() const;

                // 快速类型检查
                bool is_varint() const noexcept { return wt == WireType::Varint; }
                bool is_fixed32() const noexcept { return wt == WireType::Bit32; }
                bool is_fixed64() const noexcept { return wt == WireType::Bit64; }
                bool is_length_delimited() const noexcept { return wt == WireType::LengthDelimited; }
            };

        private:
            membin data_;
            mutable const uint8_t* scan_ptr_;
            mutable const uint8_t* scan_end_;
            mutable bool indexed_ = false;
            mutable std::unordered_map<int, std::vector<FieldView>> index_;

            // 内联varint读取
            static bool readVarint(const uint8_t*& p, const uint8_t* end, uint64_t& out) {
                uint64_t v = 0;
                int shift = 0;
                while (p < end) {
                    uint8_t b = *p++;
                    v |= uint64_t(b & 0x7F) << shift;
                    if (!(b & 0x80)) {
                        out = v;
                        return true;
                    }
                    shift += 7;
                    if (shift > 70) return false;  // Varint 太长
                }
                return false;  // 数据截断
            }

            void scan_to(int fno) const {
                while (scan_ptr_ && scan_ptr_ < scan_end_) {
                    const uint8_t* p = scan_ptr_;
                    uint64_t tag;
                    if (!readVarint(p, scan_end_, tag)) {
                        throw DecodeError("Invalid varint tag");
                    }
                    int cur_f = int(tag >> 3);
                    WireType wt = WireType(tag & 0x07);
                    const uint8_t* payload = nullptr;
                    size_t payload_len = 0;

                    switch (wt) {
                    case WireType::Varint:
                        payload = p;
                        if (!readVarint(p, scan_end_, tag)) {
                            throw DecodeError("Invalid varint payload");
                        }
                        payload_len = p - payload;
                        break;
                    case WireType::Bit64:
                        if (scan_end_ - p < 8) {
                            throw DecodeError("Truncated 64-bit field");
                        }
                        payload = p;
                        p += 8;
                        payload_len = 8;
                        break;
                    case WireType::LengthDelimited: {
                        uint64_t length;
                        if (!readVarint(p, scan_end_, length)) {
                            throw DecodeError("Invalid length varint");
                        }
                        if (static_cast<uint64_t>(scan_end_ - p) < length) {
                            throw DecodeError("Truncated length-delimited field");
                        }
                        payload = p;
                        p += length;
                        payload_len = static_cast<size_t>(length);
                        break;
                    }
                    case WireType::Bit32:
                        if (scan_end_ - p < 4) {
                            throw DecodeError("Truncated 32-bit field");
                        }
                        payload = p;
                        p += 4;
                        payload_len = 4;
                        break;
                    default:
                        throw DecodeError("Unsupported wire type");
                    }

                    index_[cur_f].push_back({ cur_f, wt, payload, payload_len });
                    scan_ptr_ = p;
                    if (cur_f == fno) break;
                }
            }

        public:
            reader(const membin& data, bool strict_mode = false)
                : data_(data)
                , scan_ptr_(data_.data())
                , scan_end_(data_.data() + data_.size()) {
                if (strict_mode) {
                    validate_all();
                }
            }

            // 严格验证整个消息
            void validate_all() const {
                const uint8_t* cur = data_.data();
                const uint8_t* end = data_.data() + data_.size();

                while (cur < end) {
                    uint64_t tag;
                    if (!readVarint(cur, end, tag)) {
                        throw DecodeError("Invalid varint tag");
                    }
                    int wire_type = int(tag & 0x07);

                    switch (wire_type) {
                    case 0: { // Varint
                        uint64_t dummy;
                        if (!readVarint(cur, end, dummy)) {
                            throw DecodeError("Invalid varint payload");
                        }
                        break;
                    }
                    case 1: { // 64-bit
                        if (end - cur < 8) {
                            throw DecodeError("Truncated 64-bit field");
                        }
                        cur += 8;
                        break;
                    }
                    case 2: { // LengthDelimited
                        uint64_t plen;
                        if (!readVarint(cur, end, plen)) {
                            throw DecodeError("Invalid length varint");
                        }
                        if (end - cur < static_cast<ptrdiff_t>(plen)) {
                            throw DecodeError("Truncated length-delimited field");
                        }
                        cur += plen;
                        break;
                    }
                    case 5: { // 32-bit
                        if (end - cur < 4) {
                            throw DecodeError("Truncated 32-bit field");
                        }
                        cur += 4;
                        break;
                    }
                    default:
                        throw DecodeError("Unsupported wire type");
                    }
                }

                if (cur != end) {
                    throw DecodeError("Extra bytes after protobuf message");
                }
            }

            // 获取单个字段
            std::optional<FieldView> get(int fno) const {
                scan_to(fno);
                auto it = index_.find(fno);
                if (it != index_.end() && !it->second.empty()) {
                    return it->second.front();
                }
                return std::nullopt;
            }

            // 获取所有重复字段
            const std::vector<FieldView>& get_all(int fno) const {
                scan_to(fno);
                static const std::vector<FieldView> empty;
                auto it = index_.find(fno);
                return it != index_.end() ? it->second : empty;
            }

            // 解析packed repeated字段
            template<typename T>
                requires std::integral<T> || std::floating_point<T>
            std::vector<T> unpack_packed(const FieldView & fv) const {
                if (fv.wt != WireType::LengthDelimited) {
                    throw DecodeError("Not a packed field");
                }

                std::vector<T> result;
                const uint8_t* ptr = fv.ptr;
                const uint8_t* end = ptr + fv.len;

                // 预分配（估算容量）
                if constexpr (std::is_integral_v<T>) {
                    // 整数：每个varint至少1字节
                    result.reserve(fv.len);
                }
                else {
                    // 浮点数：精确计算
                    constexpr size_t elem_size = sizeof(T);
                    if (fv.len % elem_size != 0) {
                        throw DecodeError("Invalid packed float data length");
                    }
                    result.reserve(fv.len / elem_size);
                }

                while (ptr < end) {
                    if constexpr (std::is_same_v<T, float>) {
                        if (end - ptr < 4) {
                            throw DecodeError("Truncated float data");
                        }
                        uint32_t raw;
                        std::memcpy(&raw, ptr, 4);
                        float value;
                        std::memcpy(&value, &raw, 4);
                        result.push_back(value);
                        ptr += 4;
                    }
                    else if constexpr (std::is_same_v<T, double>) {
                        if (end - ptr < 8) {
                            throw DecodeError("Truncated double data");
                        }
                        uint64_t raw;
                        std::memcpy(&raw, ptr, 8);
                        double value;
                        std::memcpy(&value, &raw, 8);
                        result.push_back(value);
                        ptr += 8;
                    }
                    else if constexpr (std::is_integral_v<T>) {
                        uint64_t varint;
                        if (!readVarint(ptr, end, varint)) {
                            throw DecodeError("Invalid varint in packed data");
                        }

                        if constexpr (std::is_signed_v<T>) {
                            // ZigZag解码
                            int64_t value = static_cast<int64_t>((varint >> 1) ^
                                -(static_cast<int64_t>(varint & 1)));
                            result.push_back(static_cast<T>(value));
                        }
                        else {
                            result.push_back(static_cast<T>(varint));
                        }
                    }
                }

                return result;
            }

            // 转换为JSON
            krnln::json to_json() const {
                using json = krnln::json;
                json::object obj;

                for (const auto &fv : *this) {
                    std::string key = std::to_string(fv.field);
                    json value;

                    switch (fv.wt) {
                    case WireType::Varint: {
                        value = static_cast<int64_t>(fv.as_uint());
                        obj[key] = value;
                        break;
                    }
                    case WireType::Bit32: {
                        uint32_t u32;
                        std::memcpy(&u32, fv.ptr, sizeof(u32));
                        value = static_cast<int64_t>(u32);
                        obj[key] = value;
                        break;
                    }
                    case WireType::Bit64: {
                        uint64_t u64;
                        std::memcpy(&u64, fv.ptr, sizeof(u64));
                        value = static_cast<int64_t>(u64);
                        obj[key] = value;
                        break;
                    }
                    case WireType::LengthDelimited: {
                        try {
                            reader nested(fv.as_bin(), true);
                            value = nested.to_json();
                            obj[key] = value;
                        }
                        catch (...) {
                            // 尝试作为字符串
                            auto view = fv.as_view();
                            bool is_printable = std::all_of(view.begin(), view.end(),
                                [](char c) { return std::isprint(static_cast<unsigned char>(c)) ||
                                c == '\t' || c == '\n' || c == '\r'; });
                            if (is_printable) {
                                value = std::string(view);
                            }
                            else {
                                value = fv.as_bin().base64();
                            }
                            obj[key] = value;
                        }
                        break;
                    }
                    default:
                        value = nullptr;
                        obj[key] = value;
                    }
                }

                return json(std::move(obj));
            }

            // 迭代器支持
            class iterator {
                const uint8_t* cur_;
                const uint8_t* end_;
                FieldView current_{};

            public:
                using iterator_category = std::input_iterator_tag;
                using value_type = FieldView;

                iterator(const uint8_t* p, const uint8_t* end) noexcept
                    : cur_(p), end_(end) {
                    advance();
                }

                iterator() noexcept : cur_(nullptr), end_(nullptr) {}

                iterator& operator++() { advance(); return *this; }
                bool operator!=(const iterator& o) const noexcept {
                    return cur_ != o.cur_;
                }
                FieldView operator*() const noexcept { return current_; }

            private:
                void advance() {
                    if (!cur_ || cur_ >= end_) {
                        cur_ = nullptr;
                        return;
                    }

                    const uint8_t* p = cur_;
                    uint64_t tag;
                    if (!reader::readVarint(p, end_, tag)) {
                        cur_ = nullptr;
                        return;
                    }

                    int field = static_cast<int>(tag >> 3);
                    WireType wt = static_cast<WireType>(tag & 0x07);

                    const uint8_t* payload = nullptr;
                    size_t payload_len = 0;

                    switch (wt) {
                    case WireType::Varint:
                        payload = p;
                        if (!reader::readVarint(p, end_, tag)) {
                            cur_ = nullptr;
                            return;
                        }
                        payload_len = p - payload;
                        break;
                    case WireType::Bit64:
                        if (end_ - p < 8) {
                            cur_ = nullptr;
                            return;
                        }
                        payload = p;
                        p += 8;
                        payload_len = 8;
                        break;
                    case WireType::LengthDelimited: {
                        uint64_t length;
                        if (!reader::readVarint(p, end_, length)) {
                            cur_ = nullptr;
                            return;
                        }
                        if (end_ - p < static_cast<ptrdiff_t>(length)) {
                            cur_ = nullptr;
                            return;
                        }
                        payload = p;
                        p += length;
                        payload_len = static_cast<size_t>(length);
                        break;
                    }
                    case WireType::Bit32:
                        if (end_ - p < 4) {
                            cur_ = nullptr;
                            return;
                        }
                        payload = p;
                        p += 4;
                        payload_len = 4;
                        break;
                    default:
                        cur_ = nullptr;
                        return;
                    }

                    current_ = FieldView{ field, wt, payload, payload_len };
                    cur_ = p;
                }
            };

            iterator begin() const noexcept {
                return iterator(data_.data(), data_.data() + data_.size());
            }

            iterator end() const noexcept {
                return iterator();
            }

            std::optional<FieldView> operator[](int fno) const {
                return get(fno);
            }
        };

        // ==================== 编码器优化 ====================
        class writer {
            Buffer buf_;

        public:
            writer() : buf_(256) {} // 更大的初始缓冲区

            // 通用写入方法
            template <typename T>
                requires std::integral<T> || std::floating_point<T>
            void write(int field, T v) noexcept {
                if constexpr (std::integral<T>) {
                    if constexpr (std::signed_integral<T>) {
                        buf_.writeFieldSInt(field, static_cast<int64_t>(v));
                    }
                    else {
                        buf_.writeFieldVarint(field, static_cast<uint64_t>(v));
                    }
                }
                else {
                    if constexpr (std::same_as<T, float>) {
                        uint32_t raw;
                        std::memcpy(&raw, &v, sizeof(v));
                        buf_.writeFieldFixed32(field, raw);
                    }
                    else {
                        uint64_t raw;
                        std::memcpy(&raw, &v, sizeof(v));
                        buf_.writeFieldFixed64(field, raw);
                    }
                }
            }

            void write(int field, const std::string& s) {
                buf_.writeFieldString(field, s);
            }

            void write(int field, std::string_view sv) {
                buf_.writeField(field, WireType::LengthDelimited);
                buf_.writeLengthDelimited(reinterpret_cast<const uint8_t*>(sv.data()), sv.size());
            }

            void write(int field, const membin& d) {
                buf_.writeFieldBytes(field, d);
            }

            void write(int field, const writer& nested) {
                auto inner = nested.buf_.data();
                buf_.writeFieldBytes(field, inner);
            }

            // 新增：写入packed repeated字段（优化版）
            template<typename T>
                requires std::integral<T> || std::floating_point<T>
            void write_packed(int field, const std::vector<T>&values) {
                if (values.empty()) return;

                // 使用临时Buffer存储packed数据
                Buffer temp_buf(values.size() * 5); // 估算：每个varint最多5字节

                for (const auto& v : values) {
                    if constexpr (std::signed_integral<T>) {
                        temp_buf.writeZigZag(static_cast<int64_t>(v));
                    }
                    else if constexpr (std::integral<T>) {
                        temp_buf.writeVarint(static_cast<uint64_t>(v));
                    }
                    else {
                        temp_buf.writeFixed(v);
                    }
                }

                // 写入field header和packed数据
                buf_.writeField(field, WireType::LengthDelimited);
                auto packed_data = temp_buf.release();
                buf_.writeLengthDelimited(packed_data.data(), packed_data.size());
            }

            // 新增：迭代器版本的write_packed
            template<typename InputIt>
                requires std::integral<typename std::iterator_traits<InputIt>::value_type> ||
            std::floating_point<typename std::iterator_traits<InputIt>::value_type>
                void write_packed(int field, InputIt first, InputIt last) {
                if (first == last) return;

                // 计算元素数量
                size_t count = 0;
                if constexpr (requires { std::distance(first, last); }) {
                    count = std::distance(first, last);
                }

                Buffer temp_buf(count * 5);

                for (auto it = first; it != last; ++it) {
                    if constexpr (std::signed_integral<decltype(*it)>) {
                        temp_buf.writeZigZag(static_cast<int64_t>(*it));
                    }
                    else if constexpr (std::integral<decltype(*it)>) {
                        temp_buf.writeVarint(static_cast<uint64_t>(*it));
                    }
                    else {
                        temp_buf.writeFixed(*it);
                    }
                }

                buf_.writeField(field, WireType::LengthDelimited);
                auto packed_data = temp_buf.release();
                buf_.writeLengthDelimited(packed_data.data(), packed_data.size());
            }

            membin to_buffer() const noexcept { return buf_.data(); }

            // 打包消息（添加长度前缀）
            static membin pack(const membin& msg) {
                uint32_t l = static_cast<uint32_t>(msg.size());
                membin p;
                p.reserve(4 + msg.size());
                // 大端序写入长度
                p.push_back(static_cast<uint8_t>((l >> 24) & 0xFF));
                p.push_back(static_cast<uint8_t>((l >> 16) & 0xFF));
                p.push_back(static_cast<uint8_t>((l >> 8) & 0xFF));
                p.push_back(static_cast<uint8_t>(l & 0xFF));
                p.insert(p.end(), msg.begin(), msg.end());
                return p;
            }

            // 解包消息
            static membin unpack(const uint8_t* data, size_t total_size, size_t& out_len) {
                if (total_size < 4) {
                    throw DecodeError("header too short");
                }
                uint32_t len = (static_cast<uint32_t>(data[0]) << 24) |
                    (static_cast<uint32_t>(data[1]) << 16) |
                    (static_cast<uint32_t>(data[2]) << 8) |
                    static_cast<uint32_t>(data[3]);
                if (static_cast<size_t>(len) + 4 > total_size) {
                    throw DecodeError("length mismatch");
                }
                out_len = len;
                return membin(data + 4, data + 4 + len);
            }
        };

        // ==================== FieldView 方法实现 ====================

        inline uint64_t reader::FieldView::as_uint() const {
            if (wt != WireType::Varint) {
                throw DecodeError("as_uint(): not a varint");
            }
            uint64_t res = 0;
            int shift = 0;
            const uint8_t* p = ptr;
            const uint8_t* end = ptr + len;
            while (p < end) {
                uint8_t b = *p++;
                res |= uint64_t(b & 0x7F) << shift;
                if (!(b & 0x80)) break;
                shift += 7;
            }
            return res;
        }

        inline int64_t reader::FieldView::as_sint() const {
            uint64_t u = as_uint();
            return static_cast<int64_t>((u >> 1) ^ -static_cast<int64_t>(u & 1));
        }

        inline std::string reader::FieldView::as_string() const {
            if (wt != WireType::LengthDelimited) {
                throw DecodeError("as_string(): not length-delimited");
            }
            return std::string(reinterpret_cast<const char*>(ptr), len);
        }

        inline std::string_view reader::FieldView::as_view() const {
            if (wt != WireType::LengthDelimited) {
                throw DecodeError("as_view(): not length-delimited");
            }
            // 直接返回指向原始内存的视图，零拷贝
            return std::string_view(reinterpret_cast<const char*>(ptr), len);
        }

        inline krnln::membin reader::FieldView::as_bin() const {
            if (wt != WireType::LengthDelimited) {
                throw DecodeError("as_bin(): not length-delimited");
            }
            return krnln::membin(ptr, len);
        }

        inline reader reader::FieldView::as_reader() const {
            if (wt != WireType::LengthDelimited) {
                throw DecodeError("as_reader(): not length-delimited");
            }
            return reader(membin(ptr, ptr + len));
        }

    } // namespace protobuf
} // namespace krnln

#endif // KRNLN_PROTOBUF_OPT_HPP
