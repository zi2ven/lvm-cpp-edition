# Lightweight Virtual Machine (LVM)

一个用现代C++（C++20）编写的轻量级虚拟机实现。

## 项目概述

LVM（Lightweight Virtual Machine）是一个简单的虚拟机实现，用于执行自定义字节码。该项目展示了虚拟机的基本工作原理，包括内存管理、指令执行、线程处理等核心概念。

## 特性

- 基于栈的虚拟机架构
- 自定义字节码指令集
- 内存管理（分配、释放、重分配）
- 多线程支持
- 文件I/O操作
- 模块化设计

## 技术栈

- C++20
- CMake 3.28+
- argparse（命令行参数解析库，作为子模块包含）

## 构建说明

### 环境要求

- 支持C++20的编译器（GCC 10+, Clang 12+, MSVC 19.29+等）
- CMake 3.28或更高版本

### 构建步骤

```bash
# 克隆项目
git clone <repository-url>
cd lvm-cpp-edition

# 初始化子模块（argparse）
git submodule update --init --recursive

# 创建构建目录
mkdir build
cd build

# 配置项目
cmake ..

# 编译项目
cmake --build .
```

## 使用方法

编译完成后，可以通过以下方式运行虚拟机：

```bash
./lvm_cpp_edition [选项] [文件]
```

### 命令行参数

- `file` - 要执行的字节码文件（默认: t.lvme）
- `--stack-size`, `-s` - 栈大小（默认: 4MB）

### 示例

```bash
# 使用默认文件和栈大小
./lvm_cpp_edition

# 指定字节码文件
./lvm_cpp_edition my_program.lvme

# 指定栈大小
./lvm_cpp_edition -s 8388608 my_program.lvme
```

## 架构组件

### 虚拟机 (VirtualMachine)

虚拟机核心组件，负责管理内存、线程、执行单元等。

### 内存管理 (Memory)

提供虚拟内存管理功能，包括页面管理、内存分配与释放等。

### 模块系统 (Module)

处理字节码模块的加载和管理。

### 字节码 (Bytecode)

定义了虚拟机支持的所有指令集。

### 执行单元 (ExecutionUnit)

负责实际执行字节码指令。

## 许可证

请根据实际情况添加许可证信息。

## 贡献

欢迎提交Issue和Pull Request来改进这个项目。