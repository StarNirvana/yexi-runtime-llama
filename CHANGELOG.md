# 版本变更记录

YxAgent 的版本历史与各版本的行为差异。当前版本 **v0.2.3**。

版本号沿用 `0.x.y`：`0.1.x` 是单次推理内核，`0.2.x` 起进入多轮增量推理，小版本号对应内核结构的一次实质变化。

## 速览

| 项目 | 0.1.0 | 0.2.0 | 0.2.1 | 0.2.2 | 0.2.3 |
| --- | --- | --- | --- | --- | --- |
| 交互 | 读一次输入、答一轮就结束 | `while` 循环，可连续多轮 | 同左 | 同左（`getline` 整行读） | 同左 |
| 退出方式 | 无 | `<\|exit\|>` | 同左 | **`#\|exit`** | 同左 |
| 记忆载体 | 无 | `conversation` 文本记录 | KV Cache + `kv_pos` 游标 | 同左 | 同左 |
| 每轮 prefill | 单条 prompt | 全部历史重算 | 仅新增 token | 同左 | 同左 |
| 模板 | 裸 prompt 直接喂 | 硬拼 Gemma turn 串 | 同左（拼接有 bug） | **gemma4 特判 + 通用 `chat_template` 回退** | 同左 |
| 输出方式 | `std::cout` 直接打印 | 同左 | 同左 | 同左 | **推理线程 → SPSC 环形队列 → 主线程打印** |
| 语言标准 | C++17 | C++17 | C++17 | **C++23（面向 C++26 特性）** | 同左 |
| llama.cpp | b11526 | b11526 | b11526 | **b12000** | 同左 |
| 清理 | 成功路径不清理 | 同左 | 同左 | **循环结束后 `cleanUp()`** | 同左 |

一句话总结：**输入完整了、模板通用了、输出解耦了 —— 但上下文还是只增不减。**

---

## 0.2.3 —— 异步输出

- **日期**：2026-11-02（工作区改动，尚未提交）
- **这一版在解决什么**：0.2.2 之前，`flowChat()` 在调用线程里一边生成一边 `std::cout`，输出与推理死死绑在一起，接 GUI / TTS / 网络都得改内核。0.2.3 把"生"和"显示"拆开：推理线程只负责往队列里塞 token，主线程负责取出来打印。

### 新增

- `include/SpscQueue.h`、`src/SpscQueue.cpp`：一个 256 槽的环形缓冲队列，接口四个 —— `enter(std::string)` 入队、`take()` 出队、`check()` 查状态、`uStatus(bool)` 改状态。
  - 写指针 `w` 从 0 起，读指针 `r` 从 -1 起，两者相等即队列空；
  - `status` 是"生产者还在工作"的开关，初始 `true`，`flowChat()` 结束时置 `false`。

### 变更

| 位置 | 变化 |
| --- | --- |
| `include/llama_llm.h:40` | 新增成员 `SpscQueue spsc_queue;`（同时 `#include "./SpscQueue.h"`） |
| `include/llama_llm.h:52` | `bool init()` → `bool init(const SpscQueue& spsc_queue_)` —— **公开 API 变更** |
| `src/llama_llm.cpp:74` | `init()` 末尾 `this -> spsc_queue = spsc_queue_;` |
| `src/llama_llm.cpp:125` | 生成循环里 `std::cout << piece` → `spsc_queue.enter(std::string(piece_buf.data(), n_chars))` |
| `src/llama_llm.cpp:129` | 函数末尾 `std::cout << std::endl` → `spsc_queue.uStatus(false)` |
| `main.cpp:33-34` | `flowChat` 改为在 `std::thread` 上跑，随后 `t.detach()` |
| `main.cpp:36-38` | 主线程 `while (spsc_queue.check()) { std::cout << spsc_queue.take(); }` 忙等取词 |
| `main.cpp:39` | 每轮结束后 `spsc_queue.uStatus(true)`，为下一轮复位 |
| `CMakeLists.txt` | 源文件列表加入 `include/SpscQueue.h`、`src/SpscQueue.cpp` |

### 行为差异

- 输出去掉了原来的整体流式口感：token 先入队，主线程再打印，中间隔一层环形缓冲。
- `flowChat()` 从"阻塞到生成完"变成"跑在独立线程上"，主线程靠 `status` 判断这一轮是否结束。
- `flowChat()` 不再自己换行，行尾的 `std::endl` 由 `main.cpp:40` 负责。

### 已知代价

1. **队列不是线程安全的**：`value[256]`、`w`、`r`、`status` 全部是 `static inline` —— 没有 `std::atomic`、没有内存序、没有 `volatile`，靠编译器不重排的运气在跑；
2. **全进程只有一份**：所有成员都是静态的，`init()` 里那句 `this -> spsc_queue = spsc_queue_;` 实际什么都没复制，多实例共享同一组槽位与指针；
3. **主线程忙等**：`while (check())` 是自旋，生成期间会吃满一个核，没有条件变量、没有 sleep；
4. **尾段可能丢**：`status` 翻成 `false` 后循环立即退出，队列里还没取走的 token 不会补打；
5. **队列满不报错**：`enter()` 不判断写指针是否追上读指针，绕圈时会静默覆盖未读数据；
6. **生命周期没管**：`t.detach()` 之后主线程没有任何 join 手段，`cleanUp()` 可能在推理线程还在写 `kv_pos` 时释放上下文。

---

## 0.2.2 —— 整行输入、通用模板、工具链换代

- **日期**：2026-10-31
- **提交**：`5dad0ab` feat: Universal chat tmplate (Towards Google/Gemma4 compatible)
- **这一版在解决什么**：0.2.1 的模板是个手写字符串，且形参自己 `append` 自己（用户输入进两次、`<|turn>` 位置错）；`std::cin >>` 遇空格就断句；依赖还停在 b11526。

### 新增

- `gemma4_template()`（`src/llama_llm.cpp:23`）：专职拼 gemma4 的 turn 串，产物为 `<|turn>user\n<输入><turn|>\n<|turn>model\n`。
- `llm_template()` 双路径（`src/llama_llm.cpp:29`）：先读 `general.architecture`（`:32`），`std::strcmp(check, "gemma4")` 命中走专用拼装（`:33`）；否则取 GGUF 内嵌 `chat_template` 交给 `llama_chat_apply_template`。
- `target_compile_options(llama PRIVATE /Zc:char8_t-)`：对 llama.cpp 回退 `char8_t` 旧行为。

### 变更

| 位置 | 变化 |
| --- | --- |
| `main.cpp:30` | `std::cin >> input` → `std::getline(std::cin, input)`，整行读 |
| `main.cpp:31` | 退出词 `<\|exit\|>` → `#\|exit` |
| `main.cpp:42` | 循环后补上 `llm.cleanUp()`，成功路径也清理 |
| `src/llama_llm.cpp` | 删掉末尾两条调试注释（"记住口令 lyx"） |
| `CMakeLists.txt` | `CMAKE_CXX_STANDARD 17` → `23`，加 `CMAKE_CXX_EXTENSIONS OFF`，依赖 `llama.cpp-b11526` → `llama.cpp-b12000` |

### 行为差异

- 带空格的中文句子终于能整句送进去；
- 换非 gemma4 模型不用再改源码，模板由 GGUF 自带；
- 模板取失败不再崩，只打 `[ ERROR ]` 并返回空串 —— 也就是把空 prompt 喂给模型（详见 README 已知限制第 3 条）；
- 正常退出路径会走 `cleanUp()`，不再直接 `return 0` 丢掉句柄。

---

## 0.2.1 —— KV Cache 增量复用

- **日期**：2026-10-30
- **提交**：`176a9a6` feat: KV Cache reuse
- **这一版在解决什么**：0.2.0 每轮把全部历史重新拼一遍再 prefill，轮次一多就慢得离谱，而且 `conversation` 存的是文本、不是模型真正的上下文状态。

### 变更

| 位置 | 变化 |
| --- | --- |
| `include/llama_llm.h:38` | 删除 `conversation`，改为 `llama_pos kv_pos = 0` |
| `src/llama_llm.cpp` | `flowChat()` 用 `llama_batch_init` 手写 `pos` / `seq_id` / `logits`，不再走 `llama_decode` 的默认批 |
| `src/llama_llm.cpp` | prefill 后 `kv_pos += tokens.size()`；生成阶段每 token 单独组批，`pos = kv_pos`，解完 `kv_pos++` |

### 行为差异

- 每轮只 prefill **这一轮新增**的 token，历史靠 KV Cache 接着用；
- 位置不再由 llama.cpp 自己数，而是由 `kv_pos` 统一发放，prefill 和生成共用同一个游标；
- 整段 prefill 只给最后一个位置开 `logits`，其余 `false`，省显存；
- 历史彻底没有文本副本 —— 想落盘、摘要、UI 展示都得另存。

### 已知代价

- `kv_pos` 只增不减，`n_ctx` 是所有轮次的总预算，写满后每轮 `llama_decode()` 都失败，只能重启；
- prefill 失败时游标不回滚。

---

## 0.2.0 —— 多轮对话

- **日期**：2026-10-26
- **提交**：`86372f1` feat: Conversation memory; Multi-round dialogue
- **这一版在解决什么**：0.1.0 问一句就退出，连"你刚才说了什么"都答不了。

### 变更

- `main.cpp` 外面套 `while (true)`，连续读输入；
- 新增退出词 `<|exit|>`；
- 新增 `conversation` 成员，累积历史文本，每轮把整段历史重新拼进 prompt；
- 模板改为硬拼 Gemma 的 turn 串（这一版写错了，见 0.2.2）。

### 行为差异

- 可以连续多轮对话；
- 每轮都重算全部历史，轮次越多越慢；
- 退出必须精确输入 `<|exit|>`。

---

## 0.1.0 —— 单次推理内核

- **日期**：2026-10-25
- **提交**：`d24e31c` feat: Atomization reasoning
- **这一版在解决什么**：把「叶汐」角色框架里的推理部分单独剥出来，做成最小可复现样例。

### 内容

- `main.cpp`：切 UTF-8 代码页 → `llama_backend_init()` → `llm.init()` → 读一次输入 → `flowChat()` → `return 0`；
- 输入用 `std::cin >> input`，遇空格断句；
- `flowChat(const char*)` 直接吃裸 prompt，无模板；
- 无退出词、无循环、无历史。

### 与后续版本的差异

- 当时的 `init()` 还调了 `llama_backend_init()`，后续版本去掉了它，却保留了对偶的 `llama_backend_free()` —— 这是后来"backend_init 缺失"问题的来源；
- 当时的 `flowChat()` 收 `const char*`，0.2.0 起改为 `const std::string&`；
- 当时的 `init()` 无参，0.2.3 起需要传入 `SpscQueue`。

---

## 公开 API 演变

```cpp
// 0.1.0
bool init();
void flowChat(const char* prompt);

// 0.2.0 ~ 0.2.2
bool init();
void flowChat(const std::string& prompt);

// 0.2.3
bool init(const SpscQueue& spsc_queue_);
void flowChat(const std::string& prompt);
```

下游若要跟着升级，三处要留意：`init()` 的签名、退出词的字符串、以及输出不再由内核直接打印。
