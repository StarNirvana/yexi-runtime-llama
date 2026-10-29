# YxAgent

> 叶汐 AI 角色专用推理框架 —— 剥离推理特征后的**增量推理内核** (v0.2.1)。
> 一条输入进去，一段文字流出来；历史留在 KV Cache 里，模板自己拼。
> 原计划是将agent能力和推理框架深度集成。本仓库会陆续更新相关计划内容

[![C++26](https://img.shields.io/badge/C%2B%2B-26-blue.svg)](#)
[![llama.cpp](https://img.shields.io/badge/llama.cpp-b11526-green.svg)](#)
[![License: GPL-3.0](https://img.shields.io/badge/License-GPL--3.0-orange.svg)](LICENSE.md)

---

## 这是什么？

YxAgent 是「叶汐 AI 角色专用推理框架」中被单独剥出来的那一层：**只保留推理本身**。

没有角色设定，没有系统提示词，没有提示工程，也没有任何后处理。程序做的事只有一件 —— 把当前这一轮的输入拼成 prompt 交给 GGUF 模型，然后把模型吐出来的 token 一个一个打印到控制台。

**v0.2.1 的核心变化：不再重算历史，改为复用 KV Cache。**

上一版（0.2.0）把每轮 `user` / `model` 文本存进 `conversation`，下一轮把整段历史重新拼成 prompt、重新 tokenize、重新 prefill，成本随轮数线性上涨。这一版把 `conversation` 整个删掉，换成一个位置游标 `kv_pos`：

- 每轮**只把新输入** prefill 进上下文，位置从 `kv_pos` 接着写；
- 模型上一轮的输出早就躺在 KV Cache 里，不需要重新喂；
- 生成的每个 token 也按 `kv_pos` 逐位写入，游标随之递增。

于是"记忆"从**文本层**下沉到了**缓存层**：没有历史字符串，只有一段一直在长的 K/V。

适合用来：

- 作为角色框架的**最小会话底座**，往上叠人格、长期记忆、对话管理；
- 验证 GGUF 模型能否正常加载 / 跑通多轮对话（最小可复现样例）；
- 学习 llama.cpp C API 的完整调用链（加载 → 分词 → 手工 batch → prefill → 采样 → 流式输出 → 释放）。

整个内核由 3 个源文件组成，约 210 行（含 GPL 版权头），不依赖 server、不依赖 Python、不依赖任何胶水层。

## 版本变更

| 项目 | 0.1.0 | 0.2.0                           | 0.2.1                                          |
| --- | --- |---------------------------------|------------------------------------------------|
| 交互 | 读一次输入、答一轮就结束 | `while` 循环，可连续多轮，`<\|exit\|>`退出 | 同左                                             |
| 记忆载体 | 无 | `conversation` 文本记录             | **KV Cache + `kv_pos` 游标**                     |
| 每轮 prefill | 单条 prompt | 全部历史重算                          | **仅新增 token**                                  |
| Batch 构造 | `llama_batch_get_one` | 同左                              | **`llama_batch_init` + 手写 `pos` / `seq_id` / `logits`** |
| 上下文成本 | — | 随轮数线性增长，总成本 O(n²)               | 每轮 O(新增)，累计 O(n)                               |
| 会话文本 | 无 | 完整保留                            | 利用 KV Cache 保存 (单会话)                           |
| 清理 | 成功路径不清理 | 同左（`main` 直接 `return 0`）        | 成功路径在结束时清理   |

一句话总结：**快了，也"忘"得更彻底 —— 历史只剩在缓存里，进程一退就真没了。**

## 包含与不包含

| 本程序包含 | 本程序不包含 |
| --- | --- |
| GGUF 模型加载（默认全层卸载到 GPU） | 角色人设 / 系统提示词 |
| KV Cache 增量复用（`kv_pos` 游标，只 prefill 新增 token） | 上下文腾挪（无滑动窗口、无 KV shift、无摘要） |
| 手工 batch 构造（逐 token 写 `pos` / `seq_id` / `logits`） | 通用 chat template（未调用 `llama_chat_apply_template`） |
| 分词 → prefill → 自回归采样 | 会话文本记录 / 历史持久化（退出即丢，无落盘） |
| top-k / top-p / temperature / 重复惩罚采样链 | 推理链（CoT）、工具调用、RAG、输出后处理 |
| 逐 token 流式打印 | 命令行参数、配置文件、日志系统 |
| Windows 控制台 UTF-8 修正 | 跨平台支持 |

## 特性

- **极简**：`main.cpp` + `llama_llm.h/.cpp`，一个类、三个公开方法（`init` / `flowChat` / `cleanUp`）。
- **增量 prefill**：只把这一轮的新输入写进上下文，历史交给 KV Cache，不再整段重算。
- **显式位置控制**：`pos` 由 `kv_pos` 统一分配，prefill 与逐 token 生成共用同一个游标。
- **省显存的 logits**：整段 prefill 只对最后一个位置开 `logits`，其余置 `false`。
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
           ├─ llm_template(input)   拼接本轮 prompt（见下方"注意"）
           ├─ participle()          分词（两段式：先问长度，再填 buffer）
           ├─ llama_batch_init(n_tokens, 0, 1)
           │     token[i] = tokens[i]
           │     pos[i]   = kv_pos + i          // 接在历史后面
           │     seq_id[i][0] = 0, n_seq_id[i] = 1
           │     logits[i] = false（仅末位 true）
           ├─ llama_decode()        prefill —— 只算新增的一轮
           ├─ kv_pos += n_tokens
           ├─ 最多 512 次循环：
           │     sample → 判 EOG → 写入单 token batch（pos = kv_pos）→ decode → kv_pos++
           │     → 跳控制符 → token_to_piece → cout
           └─ llama_batch_free()
```

`kv_pos` 是唯一的位置来源：prefill 用它铺 `pos`，生成阶段每解码一个 token 就 `+1`，因此下一轮的 prefill 天然接在旧历史之后。

> ⚠ **注意**：`llm_template()` 目前的实现是 `prompt.append("<|turn>\nuser").append(prompt).append("<turn|>\n<|turn>model\n")`，也就是对形参自身做 append —— 用户输入会出现两次，且 `<|turn>` 落在输入之后，并不是合法的 Gemma turn 格式。详见[已知限制](#已知限制--todo)第 1 条。

进程退出时不显式释放（`main` 直接 `return 0`）；初始化失败路径才会调用 `cleanUp()`。

## 目录结构

```text
YxAgent/
├── main.cpp                    # 入口：UTF-8 控制台、初始化、多轮输入循环、<|exit|> 退出
├── include/llama_llm.h         # llama_llm 声明：模型 / 上下文 / 采样器 / 词表 / kv_pos 游标
├── src/llama_llm.cpp           # 模板拼接、分词、模型加载、采样链、手写 batch、流式生成
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
- 历史只活在 KV Cache 与 `kv_pos` 里，进程退出即丢；重开就是全新会话。
- 注意 `main.cpp` 使用的是 `std::cin >> input`，**只读取第一个空白分隔的词**，带空格的句子会被截断。

## 关键参数

所有参数均为硬编码，改值需重新编译。

| 位置 | 参数 | 当前值 | 说明 |
| --- | --- | --- | --- |
| `src/llama_llm.cpp:41` | 模型路径 | `./model/gemma4.gguf` | 相对当前工作目录 |
| `src/llama_llm.cpp:39` | `n_gpu_layers` | `-1` | 全部层卸载到 GPU |
| `src/llama_llm.cpp:46` | `n_threads` | `6` | 单 token 生成线程数 |
| `src/llama_llm.cpp:49` | `n_threads_batch` | `14` | prefill 批处理线程数 |
| `src/llama_llm.cpp:47` | `n_batch` | `2048` | 单次 decode 的最大批大小 |
| `src/llama_llm.cpp:48` | `n_ctx` | `4096` | 上下文窗口（2048 × 2），**所有轮次共享的总预算** |
| `src/llama_llm.cpp:54` | 重复惩罚 | `1.1`（窗口 64） | penalties 采样器，跨轮累积 |
| `src/llama_llm.cpp:55` | `top_k` | `60` | — |
| `src/llama_llm.cpp:56` | `top_p` | `1.0` | 等于关闭核采样 |
| `src/llama_llm.cpp:57` | `temperature` | `0.7` | — |
| `src/llama_llm.cpp:85` | `MAX_TOKENS` | `512` | 单轮输出上限 |
| `src/llama_llm.cpp:25` | turn 标记 | `<|turn>` / `<turn|>` | 与 Gemma 模板绑定，换模型必须改 |
| `include/llama_llm.h:37` | `kv_pos` | `0`（起始） | KV Cache 写指针，只增不减，不会回退 |

## 已知限制 / TODO
1. **上下文只增不减**：`kv_pos` 单调递增，`n_ctx = 4096` 是**所有轮次 prompt + 生成**的总预算。写满后 `llama_decode()` 返回非 0，函数直接 `return`，且 `kv_pos` 不推进、不重置 —— 此后每一轮都会失败，只能重启进程。没有滑动窗口、没有 `llama_kv_self_seq_rm` 之类的腾挪。
2. **历史没有文本副本**：`conversation` 已删除，`flowChat()` 里的 `content` 仍在收集字符串却从未被使用。想落盘、摘要压缩、重建会话，都得先把文本记录加回来。
3. **输入截断**：`std::cin >> input` 遇空白即停，带空格的中文/英文句子会被切碎，应改为 `std::getline` 读整行。
4. **EOF 未检查**：`std::cin >> input` 失败（如 Ctrl+Z / 重定向读完）后循环不会退出，`input` 为空串却继续调用 `flowChat("")`，会空转。
5. **控制符也进 KV**：判定为 control 的 token 只跳过打印，`llama_decode()` 仍会把它写进上下文并占用一个位置。
6. **模板是手搓的**：硬编码 Gemma 的 `<|turn>` 格式，换模型要改 `llm_template()`；正确做法是读 GGUF 的 `tokenizer.chat_template` 走 `llama_chat_apply_template`。
7. **长度写死**：`n_ctx` 4096、`MAX_TOKENS` 512。
8. **不可配置**：模型路径、线程数、采样参数、退出词均为编译期常量，无命令行开关。
9. **成功路径不清理**：`main` 走完直接 `return 0`，`cleanUp()` 与 `llama_backend_free()` 都不会执行，靠进程退出回收。
10. **采样器状态跨轮累积**：penalties 采样器会在 `accept` 时记录历史 token，多轮之后重复词惩罚覆盖的是很久以前的输出，可能影响用词。
11. **仅 Windows**：`SetConsoleCP` 等调用把平台锁死在 Windows。
12. **残留调试注释**：`src/llama_llm.cpp` 末尾留着两条试机用的 prompt 注释（"记住口令 lyx" / "请复述口令"），提交前建议清掉。

## 从内核到角色框架

单次推理只是地基，往上叠的顺序大致是：

1. **修模板**：把 `llm_template()` 的自我 append 改对，最好换成 `llama_chat_apply_template`，让模板跟着模型走；
2. **上下文管理**：`kv_pos` 逼近 `n_ctx` 时用 `seq_rm` 丢掉最早的轮次（保留首轮），或做摘要压缩，并在 prefill 失败时回滚游标；
3. **恢复文本记录**：重新保留一份 `conversation`（或至少存 user 侧原文），为落盘、摘要、UI 展示留数据源；
4. **参数化**：模型路径、采样参数、输出上限、退出词走命令行或配置文件；
5. **回调式输出**：把 `std::cout` 换成 token 回调，便于接 GUI / TTS / 流式协议；
6. **持久化会话**：文本记录 + KV 落盘，支持跨进程续聊；
7. **角色层**：最后再叠人设、长期记忆与工具 —— 那才是"叶汐"的部分，不在本仓库范围内。

## 许可

- 本程序代码：**GNU General Public License v3.0**，Copyright (C) 2026 夏瑞朋，详见 [LICENSE.md](LICENSE.md)。
- llama.cpp：MIT License。
- 原模型权重：遵循其自身许可（Gemma 4 License），不受 GPL 约束，亦不随本仓库分发。
