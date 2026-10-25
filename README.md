# YxAgent

> 叶汐 AI 角色专用推理框架 —— 剥离推理特征后的**单次推理内核** (v0.1.0-bate)。
> 一条输入进去，一段文字流出来，仅此而已。
> 原计划是将agent能力和推理框架深度集成。本仓库会陆续更新相关计划内容

[![C++26](https://img.shields.io/badge/C%2B%2B-26-blue.svg)](#)
[![llama.cpp](https://img.shields.io/badge/llama.cpp-b11526-green.svg)](#)
[![License: GPL-3.0](https://img.shields.io/badge/License-GPL--3.0-orange.svg)](LICENSE.md)

---

## 这是什么？

YxAgent 是「叶汐 AI 角色专用推理框架」中被单独剥出来的那一层：**只保留推理本身**。

没有角色设定，没有系统提示词，没有多轮记忆，没有提示工程，也没有任何后处理。
程序做的事只有一件 —— 把一条 prompt 交给 GGUF 模型，然后把模型吐出来的 token 一个一个打印到控制台。

适合用来：

- 作为角色框架的**最小推理底座**，往上叠人格、记忆、对话管理；
- 验证 GGUF 模型能否正常加载 / 跑通（最小可复现样例）；
- 学习 llama.cpp C API 的完整调用链（加载 → 分词 → prefill → 采样 → 流式输出 → 释放）。

整个内核由 3 个源文件组成，约 170 行（含 GPL 版权头），不依赖 server、不依赖 Python、不依赖任何胶水层。

## 包含与不包含

| 本程序包含 | 本程序不包含 |
| --- | --- |
| GGUF 模型加载（默认全层卸载到 GPU） | 角色人设 / 系统提示词 |
| 分词 → prefill → 自回归采样 | 多轮对话与历史管理（`conversation` 成员已声明但未使用） |
| top-k / top-p / temperature / 重复惩罚采样链 | chat template 套用（模型自带模板，但代码未调用） |
| 逐 token 流式打印 | 推理链（CoT）、工具调用、RAG、输出后处理 |
| Windows 控制台 UTF-8 修正 | 命令行参数、配置文件、日志系统 |

## 特性

- **极简**：`main.cpp` + `llama_llm.h/.cpp`，一个类、三个公开方法（`init` / `flowChat` / `cleanUp`）。
- **直接调 C API**：直接使用 llama.cpp 的 `llama_model`、`llama_context`、`llama_sampler`，不经过 `common` 封装。
- **流式输出**：每采到一个 token 立即 `std::cout`，无缓冲拼接。
- **GPU 优先**：`n_gpu_layers = -1` 全量卸载；无可用 GPU 时 llama.cpp 自动回落到 CPU。
- **中文友好**：启动时切换控制台代码页到 UTF-8，MSVC 加 `/utf-8` 编译，避免中文 prompt 变乱码。
- **源码内置依赖**：llama.cpp 以 `add_subdirectory` 方式随工程编译，关闭 tests / tools / examples / server，只产出库。

## 工作流程

```text
main()
 ├─ SetConsoleCP / SetConsoleOutputCP(UTF-8)
 ├─ llama_backend_init()
 ├─ llama_llm::init()
 │    ├─ llama_model_load_from_file("./model/gemma4.gguf")   // n_gpu_layers = -1
 │    ├─ llama_model_get_vocab()
 │    ├─ llama_init_from_model()                             // n_ctx 4096 / n_batch 2048
 │    └─ sampler chain: penalties → top_k(60) → top_p(1.0) → temp(0.7) → dist
 ├─ std::cin >> input                                       // 单条输入
 └─ llama_llm::flowChat(input)
      ├─ participle()        分词（两段式：先问长度，再填 buffer）
      ├─ llama_decode()      prefill
      └─ 最多 512 次循环：sample → 判 EOG → token_to_piece → cout → decode 下一个 token
```

成功路径不再显式释放（进程退出即回收）；初始化失败路径会调用 `cleanUp()`。

## 目录结构

```text
YxAgent/
├── main.cpp                    # 入口：UTF-8 控制台、后端初始化、读取输入、单次推理
├── include/llama_llm.h         # llama_llm 声明：模型 / 上下文 / 采样器 / 词表 / 会话记录
├── src/llama_llm.cpp           # 模型加载、采样链构建、流式生成、资源释放
├── model/gemma4.gguf           # 模型权重（约 9.1 GB，不随仓库提供）
├── library/llama.cpp-b11526/   # 内置 llama.cpp 源码（不纳入版本库，构建前需就位）
├── LICENSE.md                  # GNU GPL v3
└── CMakeLists.txt              # 构建脚本
```

## 环境要求

| 项目 | 要求 |
| --- | --- |
| 系统 | Windows 10 / 11 x64（`main.cpp` 使用了 `<windows.h>` 与 `SetConsoleCP`，目前仅支持 Windows） |
| 编译器 | MSVC（工程缓存使用 VS 18 / MSVC 14.51，较新的 VS 2022 亦可） |
| CMake | ≥ 4.2（与 `CMakeLists.txt` 中 `cmake_minimum_required` 一致） |
| 构建器 | Ninja（CLion 默认配置），或 Visual Studio 生成器 |
| 硬件 | 建议有可用的 GPU；纯 CPU 也能跑，只是慢。模型需约 9.1 GB 磁盘与对应内存/显存 |
| 依赖 | llama.cpp 源码置于 `library/llama.cpp-b11526/`（路径在 CMake 中写死） |

## 构建

```bash
# 在仓库根目录执行
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

生成物位于 `build/`：`YxAgent.exe` 以及 `llama.dll`、`ggml.dll`、`ggml-base.dll`、`ggml-cpu.dll` 等动态库。
运行时 DLL 需与 exe 同目录（CMake 默认已放在一起）。

在 CLion 中直接打开工程亦可，会使用 `cmake-build-release` 作为构建目录。

## 准备依赖
- CMake固定读取`library/llama.cpp-b11526`路径下的 llama.cpp 依赖，请前往 GitHub 自行拉取对应版本。
- 目前程序模型读取固定，但本仓库不提供权重，请自行准备并修改相关源码。

## 运行
library/llama.cpp-b11526
```bash
cd <仓库根目录>            # 必须在根目录运行，模型路径是相对路径
./build/YxAgent.exe
```

```text
你好
<模型逐 token 流式输出的文本…>
```

注意 `main.cpp` 使用的是 `std::cin >> input`，**只读取第一个空白分隔的词**，带空格的句子会被截断。

## 关键参数

所有参数均为硬编码，改值需重新编译。

| 位置 | 参数 | 当前值 | 说明 |
| --- | --- | --- | --- |
| `src/llama_llm.cpp:36` | 模型路径 | `./model/gemma4.gguf` | 相对当前工作目录 |
| `src/llama_llm.cpp:34` | `n_gpu_layers` | `-1` | 全部层卸载到 GPU |
| `src/llama_llm.cpp:41` | `n_threads` | `6` | 单 token 生成线程数 |
| `src/llama_llm.cpp:44` | `n_threads_batch` | `14` | prefill 批处理线程数 |
| `src/llama_llm.cpp:42` | `n_batch` | `2048` | 单次 decode 的最大批大小 |
| `src/llama_llm.cpp:43` | `n_ctx` | `4096` | 上下文窗口（2048 × 2） |
| `src/llama_llm.cpp:49` | 重复惩罚 | `1.1`（窗口 64） | penalties 采样器 |
| `src/llama_llm.cpp:50` | `top_k` | `60` | — |
| `src/llama_llm.cpp:51` | `top_p` | `1.0` | 等于关闭核采样 |
| `src/llama_llm.cpp:52` | `temperature` | `0.7` | — |
| `src/llama_llm.cpp:67` | `MAX_TOKENS` | `512` | 单次输出上限 |

## 已知限制 / TODO

1. **单轮一次性**：`conversation` 成员未使用，`context_past` 未推进，每次都是全新上下文。
2. **输入截断**：`std::cin >> input` 遇空白即停，应改为 `std::getline` 读整行。
3. **未套 chat template**：GGUF 里自带 `tokenizer.chat_template`，但代码直接把裸 prompt 喂给 Instruct 模型，对话能力会打折扣，风格偏"续写"。
4. **长度写死**：`n_ctx` 4096、`MAX_TOKENS` 512；超长 prompt 不做截断，prefill 会直接失败。
5. **不可配置**：模型路径、线程数、采样参数均为编译期常量，无命令行开关。
6. **成功路径不清理**：依赖进程退出回收；初始化失败路径才会显式 `cleanUp()`。
7. **仅 Windows**：`SetConsoleCP` 等调用把平台锁死在 Windows。

## 从内核到角色框架

单次推理只是地基，往上叠的顺序大致是：

1. **chat template**：调用 `llama_chat_apply_template` 包装 prompt，恢复 Instruct 模型的对话格式；
2. **多轮会话**：把 `conversation` 填起来，复用 K/V cache，让 `context_past` 递增而不是每轮重算；
3. **参数化**：模型路径、采样参数、输出上限走命令行或配置文件；
4. **回调式输出**：把 `std::cout` 换成 token 回调，便于接 GUI / TTS / 流式协议；
5. **角色层**：最后再叠人设、记忆与工具 —— 那才是"叶汐"的部分，不在本仓库范围内。

## 许可

- 本程序代码：**GNU General Public License v3.0**，Copyright (C) 2026 夏瑞朋，详见 [LICENSE.md](LICENSE.md)。
- llama.cpp：MIT License。
- 原模型权重：遵循其自身许可（Gemma 4 License），不受 GPL 约束，亦不随本仓库分发。
