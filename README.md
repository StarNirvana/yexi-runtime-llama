# YxAgent

> 叶汐 AI 角色专用推理框架 —— 剥离推理特征后的**多轮推理内核** (v0.2.0-bate)。
> 一条输入进去，一段文字流出来；历史自己记，模板自己拼，仅此而已。
> 原计划是将agent能力和推理框架深度集成。本仓库会陆续更新相关计划内容

[![C++26](https://img.shields.io/badge/C%2B%2B-26-blue.svg)](#)
[![llama.cpp](https://img.shields.io/badge/llama.cpp-b11526-green.svg)](#)
[![License: GPL-3.0](https://img.shields.io/badge/License-GPL--3.0-orange.svg)](LICENSE.md)

---

## 这是什么？

YxAgent 是「叶汐 AI 角色专用推理框架」中被单独剥出来的那一层：**只保留推理本身**。

没有角色设定，没有系统提示词，没有提示工程，也没有任何后处理。程序做的事只有一件 —— 把对话历史拼成一条 prompt 交给 GGUF 模型，然后把模型吐出来的 token 一个一个打印到控制台。

v0.2.0 相比 0.1.0，核心变化是**从"单次问答"升级为"带记忆的会话内核"**：

- 新增 `conversation` 会话记录，每轮把 user / model 两侧的话都存下来；
- 新增 `llm_template()`，按 Gemma 的 turn 格式把整段历史拼成一条 prompt；
- `main()` 改为循环读输入，可以连续对话，输入 `<|exit|>` 退出。

适合用来：

- 作为角色框架的**最小会话底座**，往上叠人格、长期记忆、对话管理；
- 验证 GGUF 模型能否正常加载 / 跑通多轮对话（最小可复现样例）；
- 学习 llama.cpp C API 的完整调用链（加载 → 分词 → prefill → 采样 → 流式输出 → 释放）。

整个内核由 3 个源文件组成，约 197 行（含 GPL 版权头），不依赖 server、不依赖 Python、不依赖任何胶水层。

## 版本变更（0.1.0 → 0.2.0）

| 项目 | 0.1.0 | 0.2.0 |
| --- | --- | --- |
| 交互 | 读一次输入、答一轮就结束 | `while` 循环，可连续多轮，`<|exit|>` 退出 |
| 记忆 | 无 | `conversation` 保存 user / model 全部轮次（仅进程内） |
| 模板 | 裸 prompt 直接喂 | `llm_template()` 手搓 Gemma turn 格式 |
| KV cache | 不用 | **仍然不用**：每轮把全部历史重新 tokenize + prefill |
| 清理 | 成功路径不清理 | 同左（`main` 直接 `return 0`，`cleanUp()` 不会执行） |

一句话总结：**记忆有了，但代价是每轮重算全部历史**。

## 包含与不包含

| 本程序包含 | 本程序不包含 |
| --- | --- |
| GGUF 模型加载（默认全层卸载到 GPU） | 角色人设 / 系统提示词 |
| 多轮会话记录（`conversation`，进程内有效） | 历史持久化（退出即丢，无落盘） |
| Gemma turn 模板拼接（`llm_template()` 手工实现） | 通用 chat template（未调用 `llama_chat_apply_template`） |
| 分词 → prefill → 自回归采样 | KV cache 复用 / 上下文滑动（每轮全量重算） |
| top-k / top-p / temperature / 重复惩罚采样链 | 推理链（CoT）、工具调用、RAG、输出后处理 |
| 逐 token 流式打印 | 命令行参数、配置文件、日志系统 |
| Windows 控制台 UTF-8 修正 | 跨平台支持 |

## 特性

- **极简**：`main.cpp` + `llama_llm.h/.cpp`，一个类、三个公开方法（`init` / `flowChat` / `cleanUp`）。
- **多轮记忆**：`conversation` 以 `{role, prompt}` 形式记录每一轮，下一轮自动带上。
- **直接调 C API**：直接使用 llama.cpp 的 `llama_model`、`llama_context`、`llama_sampler`，不经过 `common` 封装。
- **流式输出**：每采到一个 token 立即 `std::cout`，无缓冲拼接。
- **GPU 优先**：`n_gpu_layers = -1` 全量卸载；无可用 GPU 时 llama.cpp 自动回落到 CPU。
- **中文友好**：启动时切换控制台代码页到 UTF-8，MSVC 加 `/utf-8` 编译，避免中文 prompt 变乱码。
- **源码内置依赖**：llama.cpp 以 `add_subdirectory` 方式随工程编译，关闭 tests / tools / examples / server，只产出库。

## 工作流程

```text
main()
 ├─ SetConsoleCP / SetConsoleOutputCP(UTF-8)
 ├─ llama_llm::init()
 │    ├─ llama_model_load_from_file("./model/gemma4.gguf")   // n_gpu_layers = -1
 │    ├─ llama_model_get_vocab()
 │    ├─ llama_init_from_model()                             // n_ctx 4096 / n_batch 2048
 │    └─ sampler chain: penalties → top_k(60) → top_p(1.0) → temp(0.7) → dist
 └─ while (std::cin >> input)                                // 多轮循环
      ├─ input == "<|exit|>" ? break
      └─ llama_llm::flowChat(input)
           ├─ conversation.push_back({"user", input})
           ├─ llm_template()      整段历史拼成 Gemma turn 串
           │     <|turn>user\n…<turn|>\n<|turn>model\n…
           ├─ participle()        分词（两段式：先问长度，再填 buffer）
           ├─ llama_decode()      prefill —— 全部历史，每轮重来一次
           ├─ 最多 512 次循环：sample → 判 EOG → 跳控制符 → token_to_piece → cout → decode
           └─ conversation.push_back({"model", content})
```

注意 `llm_template()` 末尾固定补一个 `<|turn>model\n`，把"该模型发言"的开口留给采样器；历史里则用 `<|turn>…<turn|>` 成对包裹。

进程退出时不显式释放（`main` 直接 `return 0`）；初始化失败路径才会调用 `cleanUp()`。

## 目录结构

```text
YxAgent/
├── main.cpp                    # 入口：UTF-8 控制台、初始化、多轮输入循环、<|exit|> 退出
├── include/llama_llm.h         # llama_llm 声明：模型 / 上下文 / 采样器 / 词表 / 会话记录
├── src/llama_llm.cpp           # 模板拼接、分词、模型加载、采样链、流式生成、资源释放
├── model/gemma4.gguf           # 模型权重（约 9.2 GB，不随仓库提供）
├── library/llama.cpp-b11526/   # 内置 llama.cpp 源码（不纳入版本库，构建前需就位）
├── LICENSE.md                  # GNU GPL v3
└── CMakeLists.txt              # 构建脚本
```

## 环境要求

| 项目 | 要求 |
| --- | --- |
| 系统 | Windows 10 / 11 x64（`main.cpp` 使用了 `<windows.h>` 与 `SetConsoleCP`，目前仅支持 Windows） |
| 编译器 | MSVC（工程缓存使用 VS 18 / MSVC 14.51，较新的 VS 2022 亦可），需支持 C++17 |
| CMake | ≥ 4.2（与 `CMakeLists.txt` 中 `cmake_minimum_required` 一致） |
| 构建器 | Ninja（CLion 默认配置），或 Visual Studio 生成器 |
| 硬件 | 建议有可用的 GPU；纯 CPU 也能跑，只是慢。模型需约 9.2 GB 磁盘与对应内存/显存 |
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

- CMake 固定读取 `library/llama.cpp-b11526` 路径下的 llama.cpp 依赖，请前往 GitHub 自行拉取对应版本。
- 目前程序模型路径固定，且本仓库不提供权重，请自行准备 `model/gemma4.gguf` 或修改相关源码。

## 运行

```bash
cd <仓库根目录>            # 必须在根目录运行，模型路径是相对路径
./build/YxAgent.exe
```

```text
你好
<模型逐 token 流式输出的文本…>
你还记得我刚才说了什么吗
<模型基于上文的回答…>
<|exit|>
```

- 输入 `<|exit|>` 结束程序（判等是精确匹配，前后不能有空格）。
- 历史只存在内存里，进程退出即丢；重开就是全新会话。
- 注意 `main.cpp` 使用的是 `std::cin >> input`，**只读取第一个空白分隔的词**，带空格的句子会被截断。

## 关键参数

所有参数均为硬编码，改值需重新编译。

| 位置 | 参数 | 当前值 | 说明 |
| --- | --- | --- | --- |
| `src/llama_llm.cpp:49` | 模型路径 | `./model/gemma4.gguf` | 相对当前工作目录 |
| `src/llama_llm.cpp:47` | `n_gpu_layers` | `-1` | 全部层卸载到 GPU |
| `src/llama_llm.cpp:54` | `n_threads` | `6` | 单 token 生成线程数 |
| `src/llama_llm.cpp:57` | `n_threads_batch` | `14` | prefill 批处理线程数 |
| `src/llama_llm.cpp:55` | `n_batch` | `2048` | 单次 decode 的最大批大小 |
| `src/llama_llm.cpp:56` | `n_ctx` | `4096` | 上下文窗口（2048 × 2），**多轮共享** |
| `src/llama_llm.cpp:62` | 重复惩罚 | `1.1`（窗口 64） | penalties 采样器，跨轮累积 |
| `src/llama_llm.cpp:63` | `top_k` | `60` | — |
| `src/llama_llm.cpp:64` | `top_p` | `1.0` | 等于关闭核采样 |
| `src/llama_llm.cpp:65` | `temperature` | `0.7` | — |
| `src/llama_llm.cpp:82` | `MAX_TOKENS` | `512` | 单轮输出上限 |
| `src/llama_llm.cpp:31` | turn 标记 | `<|turn>` / `<turn|>` | 与 Gemma 模板绑定，换模型必须改 |

## 已知限制 / TODO

1. **每轮全量重算**：`flowChat()` 每次都把整段历史重新 tokenize + prefill，不做 KV cache 复用（没有 `context_past`、没有 `llama_batch` 的 pos 推进）。第 n 轮的 prefill 成本随历史线性增长，总成本约 O(n²)，长会话会明显变卡。
2. **上下文不会自己腾地方**：`n_ctx` 固定 4096，历史填满后 `llama_decode()` 返回非 0，函数直接 `return`。此时这一轮的 user 消息**已经压进 `conversation`**，下一轮只会更长、更必然失败 —— 会话实际上被卡死，只能重启。
3. **输入截断**：`std::cin >> input` 遇空白即停，带空格的中文/英文句子会被切碎，应改为 `std::getline` 读整行。
4. **EOF 未检查**：`std::cin >> input` 失败（如 Ctrl+Z / 重定向读完）后循环不会退出，`input` 为空串却继续调用 `flowChat("")`，会空转。
5. **模板是手搓的**：`llm_template()` 硬编码 Gemma 的 `<|turn>` 格式，换模型要改这里；正确做法是读 GGUF 的 `tokenizer.chat_template` 走 `llama_chat_apply_template`。
6. **长度写死**：`n_ctx` 4096、`MAX_TOKENS` 512。
7. **不可配置**：模型路径、线程数、采样参数、退出词均为编译期常量，无命令行开关。
8. **成功路径不清理**：`main` 走完直接 `return 0`，`cleanUp()` 与 `llama_backend_free()` 都不会执行，靠进程退出回收。
9. **采样器状态跨轮累积**：penalties 采样器会在 `accept` 时记录历史 token，多轮之后重复词惩罚覆盖的是很久以前的输出，可能影响用词。
10. **仅 Windows**：`SetConsoleCP` 等调用把平台锁死在 Windows。

## 从内核到角色框架

单次推理只是地基，往上叠的顺序大致是：

1. ~~**chat template**~~：已用手搓 `llm_template()` 顶上，下一步换成 `llama_chat_apply_template`，让模板跟着模型走；
2. **KV cache 复用**：只 decode 新增的那一段，用 `llama_batch.pos` 接续历史，让 `n_past` 递增而不是每轮重算；
3. **上下文管理**：`n_ctx` 快满时做滑动窗口或摘要压缩，并在 prefill 失败时回滚 `conversation`；
4. **参数化**：模型路径、采样参数、输出上限、退出词走命令行或配置文件；
5. **回调式输出**：把 `std::cout` 换成 token 回调，便于接 GUI / TTS / 流式协议；
6. **持久化会话**：`conversation` 落盘，支持跨进程续聊；
7. **角色层**：最后再叠人设、长期记忆与工具 —— 那才是"叶汐"的部分，不在本仓库范围内。

## 许可

- 本程序代码：**GNU General Public License v3.0**，Copyright (C) 2026 夏瑞朋，详见 [LICENSE.md](LICENSE.md)。
- llama.cpp：MIT License。
- 原模型权重：遵循其自身许可（Gemma 4 License），不受 GPL 约束，亦不随本仓库分发。
