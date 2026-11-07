# YxAgent

> 叶汐 AI 角色专用推理框架 —— 剥离推理特征后的**增量推理内核** (v0.2.4-Correction)。
> 一条输入进去，一段文字流出来；历史留在 KV Cache 里，模板跟着模型走，输出走队列，思考链藏在通道里。
>
> 版本历史与逐版差异见 [CHANGELOG.md](CHANGELOG.md)；授权、技术选型与长期规划见 [技术执行档案.md](技术执行档案.md)。
> 原计划是将agent能力和推理框架深度集成。本仓库会陆续更新相关计划内容

[![C++26](https://img.shields.io/badge/C%2B%2B-26-blue.svg)](#)
[![llama.cpp](https://img.shields.io/badge/llama.cpp-b12000-green.svg)](#)
[![License: PolyForm NC 1.0.0](https://img.shields.io/badge/License-PolyForm%20NC%201.0.0-orange.svg)](技术执行档案.md)

---

## 这是什么？

YxAgent 是「叶汐 AI 角色专用推理框架」中被单独剥出来的那一层：**只保留推理本身**。

没有角色设定，没有系统提示词，没有提示工程，也没有任何后处理。程序做的事只有一件 —— 把当前这一轮的输入拼成 prompt 交给 GGUF 模型，然后把模型吐出来的 token 一个一个打印到控制台。

**v0.2.4-Correction 的三件事：撤掉误补的线程收尾、滤掉思考通道、把队列改成更适合的名字 —— 这是 0.2 版本的收尾，也是原版 0.2.4 两处严重错误的修正版。**

- **收尾**：原版 0.2.4 在 `t.detach()` 之后又补了 `t.join()`，detach 后的线程不可 join，`join()` 每轮都抛 `std::system_error` 直接崩；修正版把这行删了，只保留 `detach`，真正的线程收尾留到 0.3 用条件变量重做。
- **思考链过滤**：Gemma4 会把思考过程包在 `<|channel>` / `thought` / `<channel|>` 里，现在这三个标记在 `main.cpp:20` 不再输出（后续标记之间的思考正文仍会照常显示）。
- **`MsgCache`**：原来的 `SpscQueue` 类更名为 `MsgCache`，文件同步改为 `include/msgCache.h` / `src/msgCache.cpp`，`init()` 也随之回到无参签名 —— 队列是类的成员，不用再往里塞引用。

**v0.2.3 的异步输出仍是骨架**：`flowChat()` 不碰 `std::cout`，token 经 256 槽环形队列中转，推理跑在 `std::thread` 上，两边的唯一契约是队列里的 `status` 开关。

继承 0.2.2 的**整行输入 + 通用模板**与 0.2.1 的**增量推理**：`conversation` 文本记录早已删除，历史只活在 KV Cache 里，靠游标 `kv_pos` 接着写。

适合用来：

- 作为角色框架的**最小会话底座**，往上叠人格、长期记忆、对话管理；
- 验证 GGUF 模型能否正常加载 / 跑通多轮对话（最小可复现样例）；
- 学习 llama.cpp C API 的完整调用链（加载 → 分词 → 手工 batch → prefill → 采样 → 流式输出 → 释放）；
- 观察"生产线程 → 环形队列 → 消费线程"这条最小异步管线的实际坑位（见文末已知限制）。

整个内核由 5 个源文件组成，约 210 行，不依赖 server、不依赖 Python、不依赖任何胶水层。

## 版本变更

| 版本 | 主题 | 概述                                                   |
| --- | --- |------------------------------------------------------|
| [0.2.4-Correction](CHANGELOG.md#024-correction--补洞改名上档案) | 补洞 / 改名 / 修正 | 撤掉误补的 `join`、滤思考通道、`SpscQueue` → `MsgCache`、授权改 PolyForm NC；修正原版 0.2.4 的两处严重错误 |
| [0.2.3](CHANGELOG.md#023--异步输出) | 异步输出 | 推理线程只塞队列，主线程负责打印                                     |
| [0.2.2](CHANGELOG.md#022--整行输入通用模板工具链换代) | 输入 / 模板 / 工具链 | `getline` 整行读、模板双路径、llama.cpp b12000 + C++23         |
| [0.2.1](CHANGELOG.md#021--kv-cache-增量复用) | 增量推理 | 删掉 `conversation`，改 `kv_pos` + KV Cache              |
| [0.2.0](CHANGELOG.md#020--多轮对话) | 多轮对话 | `while` 循环 + `<\|exit\|>`                            |
| [0.1.0](CHANGELOG.md#010--单次推理内核) | 单次推理 | 一问一答就退出                                              |

横向对比表、每个版本"为什么改 / 改了什么 / 代价是什么"，都在 [CHANGELOG.md](CHANGELOG.md) 里。

## 包含与不包含

| 本程序包含 | 本程序不包含 |
| --- | --- |
| GGUF 模型加载（默认全层卸载到 GPU） | 角色人设 / 系统提示词 |
| KV Cache 增量复用（`kv_pos` 游标，只 prefill 新增 token） | 上下文腾挪（无滑动窗口、无 KV shift、无摘要） |
| 双路径模板：`gemma4` 专用拼装 / 读 GGUF `chat_template` 通用回退 | 多轮模板重放（只对当前这一条消息套模板） |
| 手工 batch 构造（逐 token 写 `pos` / `seq_id` / `logits`） | 会话文本记录 / 历史持久化（退出即丢，无落盘） |
| 分词 → prefill → 自回归采样 | 推理链（CoT）、工具调用、RAG、输出后处理 |
| 思考通道标记过滤（`<\|channel>` / `thought` / `<\|channel\|>`） | 通道控制（无开关、无多通道、过滤靠字面量匹配） |
| top-k / top-p / temperature / 重复惩罚采样链 | 命令行参数、配置文件、日志系统 |
| `MsgCache` 环形队列中转 token（256 槽） | 线程同步（无 atomic、无内存序、无锁、忙等） |
| 主线程异步取词打印 | 跨平台支持 |
| Windows 控制台 UTF-8 修正 | — |

## 特性

- **极简**：`main.cpp` + `llama_llm.h/.cpp` + `msgCache.h/.cpp`，一个推理类、一个队列类。
- **增量 prefill**：只把这一轮的新输入写进上下文，历史交给 KV Cache，不再整段重算。
- **模板自适应**：`gemma4` 走内置拼装，其余模型读 GGUF 自带模板，换模型不必改代码。
- **整行输入**：`std::getline` 读满一行，句子里的空格不再把提示词切碎。
- **显式位置控制**：`pos` 由 `kv_pos` 统一分配，prefill 与逐 token 生成共用同一个游标。
- **省显存的 logits**：整段 prefill 只对最后一个位置开 `logits`，其余置 `false`。
- **直接调 C API**：直接使用 llama.cpp 的 `llama_model`、`llama_context`、`llama_sampler`，不经过 `common` 封装。
- **输出解耦**：`flowChat()` 只往 `MsgCache` 里塞 token，打印由主线程负责，内核不碰 `std::cout`。
- **单生产者单消费者**：推理线程独占写指针，主线程独占读指针，队列用 `status` 标志告知"这一轮说完没有"。
- **思考链过滤**：`<|channel>` / `thought` / `<|channel|>` 三个标记在消费侧被丢弃，正式答复才上屏。
- **GPU 优先**：`n_gpu_layers = -1` 全量卸载；无可用 GPU 时 llama.cpp 自动回落到 CPU。
- **中文友好**：启动时切换控制台代码页到 UTF-8，MSVC 加 `/utf-8` 编译，避免中文 prompt 变乱码。
- **源码内置依赖**：llama.cpp 以 `add_subdirectory` 方式随工程编译，关闭 tests / tools / examples / server，只产出库。

## 工作流程

```text
main()
 ├─ SetConsoleCP / SetConsoleOutputCP(UTF-8)
 ├─ MsgCache msgCache;                                       // 256 槽环形缓冲（静态成员，全进程一份）
 ├─ llama_llm::init()                                        // 不用再传队列
 │    ├─ llama_model_load_from_file("./model/gemma4.gguf")   // n_gpu_layers = -1
 │    ├─ llama_model_get_vocab()
 │    ├─ llama_init_from_model()                             // n_ctx 4096 / n_batch 2048
 │    ├─ sampler chain: penalties → top_k(60) → top_p(1.0) → temp(0.7) → dist
 │    └─ return true
 ├─ while (true)
 │    ├─ std::getline(std::cin, input)                        // 整行读取
 │    ├─ input == "#|exit" ? break
 │    ├─ std::thread t(&llama_llm::flowChat, &llm, input)     // 推理线程
 │    │    ├─ 读 general.architecture
 │    │    ├─ "gemma4" → gemma4_template()
 │    │    │     "<|turn>user\n" + input + "<turn|>\n<|turn>model\n"
 │    │    ├─ 其他 → llama_model_chat_template() + llama_chat_apply_template()
 │    │    ├─ participle()          分词（两段式：先问长度，再填 buffer）
 │    │    ├─ llama_batch_init(n_tokens, 0, 1)
 │    │    │     token[i] = tokens[i]
 │    │    │     pos[i]   = kv_pos + i          // 接在历史后面
 │    │    │     seq_id[i][0] = 0, n_seq_id[i] = 1
 │    │    │     logits[i] = false（仅末位 true）
 │    │    ├─ llama_decode()        prefill —— 只算新增的一轮
 │    │    ├─ kv_pos += n_tokens
 │    │    ├─ 最多 512 次循环：
 │    │    │     sample → 判 EOG → 单 token batch（pos = kv_pos）→ decode → kv_pos++
 │    │    │     → 跳控制符 → token_to_piece → msgCache.enter()   // 入队，不打印
 │    │    ├─ msgCache.uStatus(false)                         // 先告知主线程：说完了
 │    │    └─ llama_batch_free()
 │    ├─ t.detach()                                           // 只 detach，不 join
 │    ├─ while (msgCache.check()) {                           // 忙等取词
 │    │       value = msgCache.take()
 │    │       value ∈ {"<|channel>", "thought", "<channel|>"} → 丢弃   // 思考链过滤
 │    │       else std::cout << value
 │    │  }
 │    ├─ msgCache.uStatus(true)                              // 复位，准备下一轮
 │    └─ std::cout << std::endl
 └─ llama_llm::cleanUp()                                        // 退出前统一释放
```

`kv_pos` 是唯一的位置来源：prefill 用它铺 `pos`，生成阶段每解码一个 token 就 `+1`，因此下一轮的 prefill 天然接在旧历史之后。

模板三处细节：

1. `gemma4_template()` 的产物是 `<|turn>user\n<输入><turn|>\n<|turn>model\n` —— 开头补了 `<|turn>user`，结尾留了一个"该模型发言"的开口；
2. 通用分支只把**当前这一条** user 消息交给 `llama_chat_apply_template`，历史不重放（历史在 KV 里）；
3. 任一分支取模板失败都会打印 `[ ERROR ]` 并返回空字符串，流程继续往下跑。

队列这一侧的约定：

1. `flowChat()` 开始时队列的 `status` 应为 `true`（主线程在每轮末尾复位），结束时它置 `false`；
2. 主线程在 `status == true` 期间不断 `take()`，取到空串就说明此刻没新词，继续试；
3. `status` 一翻 `false`，主线程立刻退出取词循环 —— 队列里还没被取走的 token 不会补打，行尾换行由主线程补；
4. 取出的字符串若正好等于 `<|channel>`、`thought`、`<|channel|>`，直接丢掉（这三个是 Gemma4 的思考通道标记，判定是**整串精确相等**，不是子串包含）。

## 目录结构

```text
YxAgent/
├── main.cpp                    # 入口：UTF-8 控制台、初始化、getline 多轮循环、起线程 + 取词过滤、#|exit
├── include/llama_llm.h         # llama_llm 声明：模型 / 上下文 / 采样器 / 词表 / kv_pos 游标 / msgCache 成员
├── src/llama_llm.cpp           # 双路径模板、分词、模型加载、采样链、手写 batch、生成入队
├── include/msgCache.h          # MsgCache 类声明：256 槽、w/r 指针、status 开关
├── src/msgCache.cpp            # MsgCache::enter() / take() / check() 实现
├── model/gemma4.gguf           # 模型权重（约 9.2 GB，不随仓库提供）
├── library/llama.cpp-b12000/   # 内置 llama.cpp 源码（不纳入版本库，构建前需就位）
├── README.md                   # 本文件
├── CHANGELOG.md                # 版本变更记录
├── 技术执行档案.md              # 授权声明 / 技术选型 / 模型与硬件清单 / 规划中的模块
├── LICENSE.md                  # 历史归档：GPLv3 全文（仅 v0.1.0 ~ v0.2.3 适用）
└── CMakeLists.txt              # 构建脚本
```

## 环境要求

| 项目 | 要求                                                                                       |
| --- |------------------------------------------------------------------------------------------|
| 系统 | Windows 10 / 11 x64（`main.cpp` 使用了 `<windows.h>` 与 `SetConsoleCP`，目前仅支持 Windows）         |
| 编译器 | MSVC Toolset 14.51.36231（工程缓存实测）；CMake 设为 `CMAKE_CXX_STANDARD 23`，源码按 C++26 风格书写         |
| CMake | 4.4.0                                                                                    |
| 构建器 | Visual Studio 生成器                                                                        |
| 编辑器 | JetBrains CLion 26.5.1（实测环境）                                                             |
| 硬件 | 实测 i9-13900H + Iris Xe / LPDDR5 16 GB，无独显；建议有可用 GPU，纯 CPU 也能跑，只是慢。模型需约 9.2 GB 磁盘与对应内存/显存 |
| 依赖 | llama.cpp `b12000` 源码置于 `library/llama.cpp-b12000/`（路径在 CMake 中写死）                       |

## 构建

```bash
# 在仓库根目录执行
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

生成物位于 `build/`：`YxAgent.exe` 以及 `llama.dll`、`ggml.dll`、`ggml-base.dll`、`ggml-cpu.dll` 等动态库。
运行时 DLL 需与 exe 同目录（CMake 默认已放在一起）。

CMake 里还有两处跟语言标准相关的设置，改动时别漏：

- `set(CMAKE_CXX_STANDARD 23)` + `CMAKE_CXX_STANDARD_REQUIRED ON` + `CMAKE_CXX_EXTENSIONS OFF`；
- `target_compile_options(llama PRIVATE /Zc:char8_t-)`：对 llama.cpp 回退旧行为，规避 C++20 起 `char8_t` 带来的类型不兼容。

在 CLion 中直接打开工程亦可，会使用 `cmake-build-release` 作为构建目录。

## 准备依赖

- CMake 固定读取 `library/llama.cpp-b12000` 路径下的 llama.cpp 依赖，请前往 GitHub 自行拉取对应版本。
- 目前程序模型路径固定，且本仓库不提供权重，请自行准备 `model/gemma4.gguf` 或修改相关源码。

## 运行

```bash
cd <仓库根目录>            # 必须在根目录运行，模型路径是相对路径
./build/YxAgent.exe
```

```text
你好，你还记得我的名字吗？
<模型经队列逐词打印出来的文本…>
我刚才问了你什么？
<模型基于上文的回答…>
#|exit
```

- 输入 `#|exit` 结束程序（判等是精确匹配，前后不能有空格）。
- 历史只活在 KV Cache 与 `kv_pos` 里，进程退出即丢；重开就是全新会话。
- 空行（直接回车）不会被过滤，会原样送进 `flowChat("")`。
- 输出形态变了：token 先进队列再由主线程打印，理想情况仍是逐词冒出，但受自旋 + 队列容量影响，可能看到突发成段出现，末尾也可能少几个字（`status` 翻 `false` 后不再补取）。
- **看不到思考过程**：模型若先输出 `<|channel>` / `thought` / `<|channel|>` 包起来的推理，这三个标记会被吞掉，但标记中间的思考文字仍会显示 —— 当前只过滤标记本身。

## 关键参数

所有参数均为硬编码，改值需重新编译。

| 位置 | 参数 | 当前值 | 说明 |
| --- | --- | --- | --- |
| `src/llama_llm.cpp:39` | 模型路径 | `./model/gemma4.gguf` | 相对当前工作目录 |
| `src/llama_llm.cpp:37` | `n_gpu_layers` | `-1` | 全部层卸载到 GPU |
| `src/llama_llm.cpp:44` | `n_threads` | `6` | 单 token 生成线程数 |
| `src/llama_llm.cpp:47` | `n_threads_batch` | `14` | prefill 批处理线程数 |
| `src/llama_llm.cpp:45` | `n_batch` | `2048` | 单次 decode 的最大批大小 |
| `src/llama_llm.cpp:46` | `n_ctx` | `4096` | 上下文窗口（2048 × 2），**所有轮次共享的总预算** |
| `src/llama_llm.cpp:52` | 重复惩罚 | `1.1`（窗口 64） | penalties 采样器，跨轮累积 |
| `src/llama_llm.cpp:53` | `top_k` | `60` | — |
| `src/llama_llm.cpp:54` | `top_p` | `1.0` | 等于关闭核采样 |
| `src/llama_llm.cpp:55` | `temperature` | `0.7` | — |
| `src/llama_llm.cpp:83` | `MAX_TOKENS` | `512` | 单轮输出上限 |
| `include/msgCache.h:4` | 队列槽位 | `256` | `MsgCache` 环形缓冲容量，满时静默覆盖未读项 |
| `src/llama_llm.cpp:9` | turn 标记 | `<\|turn>user` / `<turn\|>` / `<\|turn>model` | gemma4 专用，换模型走通用分支 |
| `src/llama_llm.cpp:16` | 架构判断 | `"gemma4"` | 与 `general.architecture` 字面量比较 |
| `main.cpp:14` | 退出词 | `#\|exit` | 精确匹配 |
| `main.cpp:20` | 思考通道标记 | `<\|channel>` / `thought` / `<\|channel\|>` | 整串精确相等才丢弃 |
| `main.cpp:16` | 推理线程 | `std::thread` + `detach` | 每轮新建、不等待 |
| `main.cpp:23` | 状态复位 | `msgCache.uStatus(true)` | 取词循环结束后复位 `status`，准备下一轮 |
| `include/llama_llm.h:34` | `init` 签名 | `init()` | 0.2.4 起不再传队列 |
| `src/llama_llm.cpp:105` | 出词方式 | `msgCache.enter(...)` | 不再直接 `std::cout` |
| `src/llama_llm.cpp:108` | 结束标志 | `msgCache.uStatus(false)` | 主线程据此退出取词循环 |
| `include/llama_llm.h:20` | `kv_pos` | `0`（起始） | KV Cache 写指针，只增不减，不会回退 |

## 已知限制 / TODO

### 异步输出（0.2.3 引入，0.2.4-Correction 修正）

1. ~~**`detach` 和 `join` 同时出现在一轮里**~~：**已在 v0.2.4-Correction 修复** —— 原版在 `main.cpp:17` 的 `t.detach()` 之后又补了 `t.join()`，detach 后的线程不可 join，`join()` 会抛 `std::system_error`（`invalid_argument`），未捕获即 `std::terminate`，每轮输出结束必崩；修正版删掉了 `t.join()`，只保留 `detach`。
2. **线程仍没人等**：`detach` 之后主线程没有任何 join 手段，`cleanUp()` 可能在推理线程还在写 `kv_pos` 时释放上下文 —— 不崩了，但收尾动作依旧缺失。
3. **队列不是线程安全的**：类改名成 `MsgCache`，内部 `value[256]`、`w`、`r`、`status` 仍是普通变量，没有 `std::atomic`、没有内存序、没有 `volatile` —— 名字里的 SPSC 语义全靠"编译器恰好没重排"，仍在未定义行为边缘。
4. **队列全进程只有一份**：这些成员都是 `static inline`，`main` 里的 `msgCache` 与 `llama_llm::msgCache` 共享同一组槽位与指针；0.2.4 让 `init()` 不再接收队列，但共享仍来自 `static`，与传不传参无关。
5. **主线程忙等自旋**：`while (msgCache.check()) { take(); }` 是纯自旋，生成期间吃满一个核，没有条件变量、没有 `sleep`、没有让出 CPU。
6. **尾段可能丢字**：`status` 一翻 `false`，取词循环立刻退出，队列里尚未被取走的 token 不会补打，答复末尾可能少几个字。
7. **队列满会静默覆盖**：`enter()` 不判断写指针是否追上读指针，绕圈写就会覆盖未读槽位，输出直接错乱或丢段。
8. **思考过滤太窄**：`main.cpp:20` 只丢弃与三个标记**完全相等**的 piece，而且只丢标记本身 —— 标记之间的思考正文仍会打到屏幕上；若某个标记被分词器切成两个 piece（如 `"<|channel"` + `">"`），匹配失败同样会漏出去；换模型（不同通道名）必须改代码。
9. **`<future>` 当 `<thread>` 用**：`main.cpp:1` 只 `#include <future>`，`std::thread` 在 MSVC 上碰巧能编，标准上不保证。

### 内核本身

10. **上下文只增不减**：`kv_pos` 单调递增，`n_ctx = 4096` 是**所有轮次 prompt + 生成**的总预算。写满后 `llama_decode()` 返回非 0，函数直接 `return`，且 `kv_pos` 不推进、不重置 —— 此后每一轮都会失败，只能重启进程。没有滑动窗口、没有 `llama_kv_self_seq_rm` 之类的腾挪。
11. **历史没有文本副本**：`conversation` 已删除，`flowChat()` 里的 `content` 仍在收集字符串却从未被使用。想落盘、摘要压缩、重建会话，都得先把文本记录加回来。
12. **模板失败被"软处理"**：`llm_template()` 取不到模板或 `llama_chat_apply_template` 失败时，只往 stderr 打一行错误、返回空串，`flowChat()` 照常继续 —— 结果是把空 prompt 送进模型让它自由发挥，而不是中止本轮。
13. **EOF 未检查**：`std::getline` 失败（如 Ctrl+Z / 重定向读完）后仍留在 `while (true)` 里，`input` 为空串，于是不断起线程跑 `flowChat("")` 空转；`cleanUp()` 也就永远走不到。
14. **空行不设防**：直接回车会被当成一条空输入送去推理，白跑一轮 prefill，还白开一个线程。
15. **控制符也进 KV**：判定为 control 的 token 只跳过入队，`llama_decode()` 仍会把它写进上下文并占用一个位置。
16. **架构判断是硬编码**：`std::strcmp(check, "gemma4")` 写死了架构名；Gemma 的其他版本（如 `gemma3`）不会命中，会退到读内嵌模板，行为可能与专用拼装不一致。
17. **通用分支只套单条消息**：`llama_chat_apply_template` 每次只收到当前一条 user 消息，不带 system、不带历史 —— 模型看到的对话格式永远只有一轮，想加 system prompt 得自己扩展。
18. **长度写死**：`n_ctx` 4096、`MAX_TOKENS` 512、队列 256 槽。
19. **不可配置**：模型路径、线程数、采样参数、退出词、被过滤的通道标记均为编译期常量，无命令行开关。
20. **`llama_backend_init` 缺失**：0.1.0 里它还在，后续版本删了 `init()` 却留着 `cleanUp()` 的 `llama_backend_free()`，两者未配对；目前靠 llama.cpp 内部兜底，建议补齐或直接去掉后者。
21. **采样器状态跨轮累积**：penalties 采样器会在 `accept` 时记录历史 token，多轮之后重复词惩罚覆盖的是很久以前的输出，可能影响用词。
22. **仅 Windows**：`SetConsoleCP` 等调用把平台锁死在 Windows，`/Zc:char8_t-` 也是 MSVC 专属开关。

## 从内核到角色框架

单次推理只是地基，往上叠的顺序大致是：

1. ~~**KV cache 复用**~~：已完成 —— 只 prefill 新增 token，`pos` 由 `kv_pos` 统一分配；
2. ~~**修模板 / 通用化**~~：已完成 —— gemma4 专用拼装 + `chat_template` 通用回退；
3. ~~**整行输入**~~：已完成 —— `std::getline` 替代 `std::cin >>`；
4. ~~**输出解耦**~~：已完成（半成品）—— 生成线程 + `MsgCache` 队列 + 主线程打印，但还缺线程安全与线程收尾（join）；
5. **线程安全化**：把队列的 `w` / `r` / `status` 换成 `std::atomic` 并补上 acquire/release 语义，`take()` 空转时加条件变量或信号量，去掉忙等；**把 `main.cpp:17` 的 `t.detach()` 改成保存 `std::thread`、取词结束后 `join()`**；
6. **思考通道控制**：把 `main.cpp:20` 的字面量过滤换成状态机/分词级处理，能整段跳过思考正文，并把通道名做成配置；
7. **上下文管理**：`kv_pos` 逼近 `n_ctx` 时用 `seq_rm` 丢掉最早的轮次（保留首轮），或做摘要压缩，并在 prefill 失败时回滚游标；
8. **输入健壮性**：检查 `std::getline` 返回值，过滤空行，处理 EOF；
9. **恢复文本记录**：重新保留一份 `conversation`（或至少存 user 侧原文），为落盘、摘要、UI 展示留数据源；
10. **补齐 system prompt**：让模板层支持 system 消息，否则人设无处安放；
11. **参数化**：模型路径、采样参数、输出上限、队列容量、退出词走命令行或配置文件；
12. **持久化会话**：文本记录 + KV 落盘，支持跨进程续聊；
13. **角色层**：最后再叠人设、长期记忆与工具 —— 那才是"叶汐"的部分，不在本仓库范围内。

## 许可

- **v0.2.4 起（含 v0.2.4-Correction）**：本程序源码改用 **PolyForm Noncommercial License 1.0.0**，禁止商业使用；完整授权条款与附属内容（权重、训练数据、设定、立绘、Prompt 等）的"保留所有权利"声明见 [技术执行档案.md](技术执行档案.md) 第一节。
- **v0.1.0 ~ v0.2.3**：继续按 **GNU General Public License v3.0** 履约，Copyright (C) 2026 夏瑞朋，旧版全文仍留在 [LICENSE.md](LICENSE.md)。
- llama.cpp：MIT License（以 `library/llama.cpp-b12000/` 内自带许可为准）。
- 原模型权重：遵循其自身许可（Gemma 4 License），不受前述授权约束，亦不随本仓库分发。

> 注：`LICENSE.md` 仍是 GPLv3 全文，仅作 v0.1.0 ~ v0.2.3 的历史存档。原版 v0.2.4 换协议后漏删源码中的 GPLv3 文件头，v0.2.4-Correction 已把 `include/msgCache.h` 顶部的 GPLv3 版权声明整段删除；现在源码不再内嵌任何授权声明，授权一律以本文件和 [技术执行档案.md](技术执行档案.md) 第一节为准。
