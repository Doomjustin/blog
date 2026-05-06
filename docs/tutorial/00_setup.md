# 0.2 环境搭建

> **前置阅读**：[0.1 机制概览](00_overview.md)
> **下一节**：[1.1 第一个协程](01_hello.md)

---

## 系统要求

| 组件 | 最低版本 | 说明 |
|------|---------|------|
| Linux 内核 | 6.1 | io_uring multishot accept 支持 |
| Clang | 17 | 推荐；C++23 协程支持完整（系统包或自行构建均可）|
| GCC | 13 | 可用，部分诊断信息不如 Clang 清晰 |
| CMake | 3.30 | |
| pkg-config | 任意 | 用于查找 liburing、jemalloc |

---

## 安装系统依赖

```bash
# Ubuntu 24.04 / Debian bookworm
sudo apt install -y \
    cmake \
    ninja-build \
    pkg-config
```

> `liburing`、`jemalloc` 及其他 C++ 依赖由 vcpkg manifest 模式自动安装（见下文），**不需要** apt 单独安装。
>
> 编译器（Clang ≥ 17 或 GCC ≥ 13）请按发行版说明安装，例如 `sudo apt install clang-17` 或通过 [llvm.org](https://apt.llvm.org/) 脚本获取。

---

## 获取 vcpkg

项目通过 [vcpkg](https://github.com/microsoft/vcpkg) 管理 C++ 依赖（spdlog、Catch2 等）。
如果你已有 vcpkg，跳过这一步。

```bash
git clone https://github.com/microsoft/vcpkg.git ~/vcpkg
~/vcpkg/bootstrap-vcpkg.sh -disableMetrics
export VCPKG_ROOT=~/vcpkg   # 建议写入 ~/.bashrc 或 ~/.zshrc
```

---

## 克隆并构建

```bash
git clone <repo-url> blog
cd blog

cmake -S . -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"

cmake --build build -j$(nproc)
```

首次构建时，vcpkg **manifest 模式**会在 cmake configure 阶段自动读取根目录的 `vcpkg.json`，下载并编译所有依赖（输出在 `build/vcpkg_installed/`），耗时约 5–10 分钟，后续增量构建不受影响。无需手动运行 `vcpkg install`。

> **Release 构建**：将 `-DCMAKE_BUILD_TYPE=Debug` 替换为 `-DCMAKE_BUILD_TYPE=Release` 并使用
> `build-release/` 作为输出目录。教程示例使用 Debug 构建即可。

---

## 验证

构建完成后，运行第一个 tutorial 示例：

```bash
./build/tutorial/01_hello/tutorial.01_hello
```

预期输出（时间戳与 PID 因运行环境而异）：

```
[2026-05-06 21:52:08.806] [144771] [info] hello from coroutine
```

如果看到这行输出，环境已就绪。接下来在 [1.1 第一个协程](01_hello.md) 中我们会逐行解析这个程序。

---

## 本章小结

系统依赖已就绪，项目可以编译。

> **下一节**：[1.1 第一个协程](01_hello.md) — 解析最小可运行程序，理解 `Task<>` 与 `async::run`。
