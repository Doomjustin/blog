# Copilot Instructions: Modern C++ (C++20/23) & Stroustrup Style

## 1. 基础标准与核心基准 (Base Standards & Guidelines)
- **标准版本：** 严格遵循现代 C++ (C++20 及以上) 标准。
- **核心指南：** 强制遵循由 Bjarne Stroustrup 和 Herb Sutter 制定的 **C++ Core Guidelines**。
- **整体风格：** 严格遵循 Bjarne Stroustrup 的 C++ 编码规范（K&R 变体）。

## 2. 代码排版与个人偏好 (Formatting & Style)
- **大括号规则 (Braces):** - **仅限函数 (Functions)**：左大括号 `{` 必须另起一行。
  - **类、命名空间、结构体与枚举 (Classes/Namespaces/Structs/Enums)**：左大括号 `{` 必须放在声明同一行的末尾，**不要**另起一行。
  - **控制流语句**（`if`, `for`, `while`）：左大括号 `{` 必须放在同一行末尾。
  - 对于仅有一条语句的单行 `for` 或 `while` 循环，绝对**不要**使用大括号 `{}`。
  - 在 `if...else if...else` 语句块中，如果任何一个分支使用了 `{}`，则所有分支必须统一加上 `{}` 保持视觉一致性。

## 3. 现代 C++ 惯用法 (Modern C++ Idioms)
- **模板约束:** 必须使用 C++20 Concepts（如 `requires`）约束类型，**绝对禁止**使用 `std::enable_if` / SFINAE。
- **范围与视图:** 处理容器时优先使用 `<ranges>` 和 `std::views` 进行组合操作，取代传统迭代器和手写冗长循环。
- **连续内存:** 传递连续内存数据时优先使用 `std::span` (遵循 Core Guidelines I.13)，**禁止**使用“裸指针 + 长度”。
- **格式化:** 字符串拼接必须使用 `std::format`，避免使用 `<iostream>` 或 C 风格的 `sprintf`。
- **编译期计算:** 尽可能多地使用 `constexpr` 和 `consteval` 将计算推迟到编译期。

## 4. 资源、内存与错误处理 (Resource & Error Handling)
- **生命周期:** 严格遵守 RAII 原则。遵循“零规则”或“五/六规则”。优先使用值语义（Value Semantics）。
- **指针限制:** **绝对避免**手动的裸 `new` 和 `delete` (遵循 R.11)。动态分配必须使用 `std::make_unique` 或 `std::make_shared`。裸指针仅限用于表示“非拥有权”（Non-owning）的语境。
- **错误处理:** 优先使用 `std::optional` 表示无值，使用 `std::expected` (C++23) 处理错误。若使用异常，遵循“按值抛出，按 const 引用捕获”。
- **Lambda 限制:** 严禁滥用 Lambda。仅限于极简谓词或极短回调（3-5行内）。绝对禁止编写包含复杂控制流的嵌套“胖 Lambda”，复杂逻辑必须提取为独立函数或 Functor。

## 5. 静态分析与高压红线 (Static Analysis & Strict Prohibitions)
- **工具合规:** 生成的代码必须符合 clang-tidy 的 `cppcoreguidelines-*`, `modernize-use-trailing-return-type`, `modernize-use-nodiscard` 以及 `readability-identifier-naming` 规则检查。
- **禁止类型转换:** 绝对禁止 C 风格的强制类型转换（如 `(int)var`），必须使用 `static_cast`, `dynamic_cast`, `std::bit_cast` 等。严禁违反 `cppcoreguidelines-pro-type-reinterpret-cast` 的行为。
- **禁止遗留 C 特性:** 绝对禁止引入 C 语言遗留头文件（如 `<string.h>`），必须使用 C++ 版本（如 `<cstring>`）。
- **禁止污染命名空间:** 绝对不要在任何头文件中使用 `using namespace std;`。
- **禁止魔术数字:** 代码中不得出现 Magic Numbers，必须提取为 `constexpr` 常量。

## 6. 构建系统与单元测试 (Build & Testing)
- **构建系统 (CMake):** 生成 CMake 脚本时，必须遵循 Modern CMake 原则（即基于 Target 的配置）。禁止修改 `CMAKE_CXX_FLAGS`，必须使用 `target_compile_features` 和 `target_compile_options`。
- **包管理 (vcpkg):** 默认假设项目使用 vcpkg 的 Manifest 模式 (`vcpkg.json`)。
- **测试框架 (Catch2 v3):** - 编写单元测试必须使用 **Catch2 v3** 框架的语法。
  - 必须使用 `#include <catch2/catch_test_macros.hpp>`，**绝对禁止**使用旧版的 `#include <catch.hpp>` 或定义 `#define CATCH_CONFIG_MAIN`。
  - 测试结构必须使用 `TEST_CASE` 和 `SECTION`，断言必须使用 `REQUIRE` 或 `CHECK`。
- **单测描述语言：** `TEST_CASE` 与 `SECTION` 的描述文本默认使用中文；技术术语可保留英文原文（如 `span`、`error_code`、`format_as`）。
- **单测数据与可读性：** 避免为单次使用的简单字面量（如单字符、`0`）引入无意义 `constexpr` 包装；仅在复用或提升语义时提取常量。若存在重复类型转换（如 `char` 到 `std::byte`），优先使用局部小型辅助函数降低噪音。

## 7. API 文档与注释 (Doxygen)
- 遵循 LLVM 编码规范，**优先使用单行 Doxygen 注释 `///`** 来标注函数、变量和枚举。
- **注释语言：** 注释内容默认使用中文；专业术语、类型名、标准库组件名与 API 名称保持英文原文。
- 仅在遇到**超长类级别文档**或**包含大量 Markdown 代码块**的注释时，才使用块注释 `/** ... */`。
- 对于**简单函数**，无须强制添加注释。
- 当添加注释时，必须包含 `@brief` 描述，参数使用 `@param[in]/[out]`，返回值使用 `@return`。
- 核心逻辑接口、复杂实现、或 C++20 新特性接口，优先附带一小段 Markdown 代码块示例。
- 语义与约束优先，不写执行步骤：写 What 和 Why，少写 How