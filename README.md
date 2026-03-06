# ---

**krnln-core: 高性能基础开发组件库**

krnln-core 是为 C++ 系统级开发打造的核心组件集。本库核心设计原则是**在极限性能与工程易用性之间取得平衡**。
参考了fbstring的实现，来实现专注于内存数据的操作，提供了类似python bytes 的操作
## ---

**核心组件全览**



| 模块名称 | 定位 | 核心技术点 | 备注 |
| :---- | :---- | :---- | :---- |
| **membin** | 高性能二进制缓冲区 | SSO, 引用计数, COW (写时复制) | 参考fbstring的结构优化 |
| **protobuf** | 轻量级协议编解码 | 零拷贝视图 (std::string\_view) | 脱离谷歌库 |
| **json** | 兼容性数据处理 | std::variant 递归结构 | 一个简化实现不建议用于大数据处理 |

## ---

**重点模块：membin (高性能二进制核心)**

membin 是本库的灵魂，其设计理念深度参考 fbstring，专门解决传统 std::vector\<uint8\_t\> 在频繁分配与拷贝时的性能损耗问题。

### **为什么选择 membin？**

1. **SSO (Small Object Optimization)**: 当存储的小块数据（通常 \< 23 字节）时，直接在对象内部存储，完全避免堆内存分配，显著降低内存碎片化。  
2. **写时复制 (COW, Copy-On-Write)**: 当多个 membin 对象共享同一块内存时，通过引用计数实现共享；仅在发生修改时才执行内存拷贝。这在传递大数据块时能提供接近 O(1) 的拷贝性能。  
3. **内存对齐与管理**: 封装了 checked\_malloc 与 checked\_realloc，在分配失败时立即抛出 std::bad\_alloc，保证内存操作的确定性。

### **membin 使用实例**

#### 基础
```cpp

#include "krnln/membin.hpp"  
using namespace krnln;

// 1. 自动根据长度切换 SSO 或堆分配  
membin data;   
data.push_back(0x01); // 此时位于 SSO 栈区

// 2. COW 特性：拷贝时仅增加引用计数，不触发底层内存复制  
membin copy = data; 

// 3. 性能友好的 API  
auto sv = data.view(); // 提供 string_view 接口，零拷贝访问底层数据
```

#### 大小序,编码处理
```cpp

#include "krnln/membin.hpp"  
using namespace krnln;

// 1. 文本编码类需要手动处理,根据自身文件编码或者指定文件编码处理 
const auto data = krnln::to_membin("test");//文件编码
const auto u8_data = krnln::to_membin(u8"test");//指定编码   
const auto str = u8_data.to_string();//从当前索引直接拷贝到string中直到结束符,无关编码

// 2.大小端处理(内存布局会根据大小端自动处理),数值类型需要手动处理,
// 写入时按照本机大小端写入
const auto data = krnln::to_membin(0x123456u);
//需写入和平台不一致时请手动翻转
data.reverse_endianness();
//读取时可以根据大小端来进行是否翻转,std::endian::native标准库自带判断大小端函数
const auto num = data.get_data<uint32_t>();
//或者
const auto num = data.submem(sizeof(uint32_t)).reverse_endianness(0).get_data<uint32_t>(0);




```

## ---

**其他模块概览**

### **协议编解码：protobuf**

专为高性能通信协议设计，避免复杂的中间件开销。

* **零拷贝特性**: 解析 WireType 时，直接返回指向原始缓存的 std::string\_view 或 FieldView，无需分配临时内存。  
* **场景**: 网络数据流解析、二进制文件读写。

### **兼容层：json**

作为兼容性组件，不建议在超高频（如每秒百万次解析）路径使用。

* **设计**: 使用 std::variant 进行类型存储，保持代码的简洁与易维护性。  
* **场景**: 系统配置文件、低频率的 API 交互、调试信息打印。

## ---

**开发环境与集成指南**

### **环境要求**

* **编译器**: C++20 兼容环境 (GCC 11+, Clang 13+, MSVC 2022+)。  
* **依赖**: Header-only 库，无任何外部库依赖。  
* **推荐编译选项**:  
  * \-O3 \-march=native: 开启指令集优化，最大化 membin 的内存访问速度。  
  * \-fno-exceptions (如果项目禁用异常，可调整源码配置)。

### **性能优化建议**

* **针对 membin**: 对于预知大小的数据，建议调用 reserve() 方法预分配内存，避免扩容期间的多次 realloc。  
* **针对 protobuf**: 建议在循环解析时复用 reader 实例，以减少 FieldView 对象的创建开销。

---

**注意**: 本库通过引入 membin 作为核心支撑，实现了全库的高性能联动。如果您有极致的性能需求，请始终优先考虑使用 membin 传递二进制上下文，并使用 protobuf 替代 json 进行热点数据交换。
