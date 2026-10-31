/*
 * 叶汐AI角色专用推理框架 - 通用开源版
 * Copyright (C) 2026  夏瑞朋
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "../include/llama_llm.h"
#include <iostream>
#include <ostream>
#include <random>
/* -- -- -- -- Assistance Function -- -- -- -- */
std::string gemma4_template(const std::string_view prompt) {
    std::string turn;
    turn.reserve(21 + prompt.size());
    return turn.append("<|turn>user\n").append(prompt).append("<turn|>\n<|turn>model\n");
}
/* -- -- -- -- Private Function -- -- -- -- */
std::string llama_llm::llm_template(const std::string& prompt) {
    // 检查是否为 Google/Gemma 4 模型
    char check[64] = {0};
    llama_model_meta_val_str(model, "general.architecture", check, sizeof(check));
    if (std::strcmp(check, "gemma4") == 0) return gemma4_template(prompt);
    const char* tmpl = llama_model_chat_template(model, nullptr);
    if (!tmpl) { std::cerr << "[ ERROR ] >> llm_template : 未内嵌 chat template\n"; return {}; }
    const llama_chat_message msg { "user", prompt.c_str() };
    const int32_t needed = llama_chat_apply_template(tmpl, &msg, 1, true, nullptr, 0);
    if (needed == -1) { std::cerr << "[ ERROR ] >> llm_template : template 使用失败\n"; return {}; }
    std::vector<char> buf(static_cast<size_t>(needed));
    const int32_t written = llama_chat_apply_template(tmpl, &msg, 1, true, buf.data(), needed);
    return std::string(buf.data(), written > 0 ? written : 0);
}
/* ** 分词器 ** */
std::vector<llama_token> llama_llm::participle(const char* prompt) {
    std::vector<llama_token> prompt_tokens(-llama_tokenize(vocab, prompt, static_cast<int32_t>(strlen(prompt)), nullptr, 0, true, true ));
    llama_tokenize(vocab, prompt, static_cast<int32_t>(strlen(prompt)), prompt_tokens.data(), static_cast<int32_t>(prompt_tokens.size()), true, true);
    return prompt_tokens;
}

/* -- -- -- -- Public Function -- -- -- -- */
/* ** 初始化 ** */
bool llama_llm::init() {
    // 模型权重卸载层数
    model_params.n_gpu_layers = -1;
    // 加载 GGUF 模型
    model = llama_model_load_from_file("./model/gemma4.gguf", model_params);
    if (! model) { std::cerr << "[ ERROR ] Init : model 无法加载\n"; return false; }
    // 加载词表
    vocab = llama_model_get_vocab(model);
    // 创建推理上下文
    context_params.n_threads = 6;
    context_params.n_batch = 2048;
    context_params.n_ctx = 2048 * 2;
    context_params.n_threads_batch = 14;
    context = llama_init_from_model(model, context_params);
    if (! context) { std::cerr << "[ ERROR ] Init : context 无法创建\n"; return false; }
    // 设置 Token 采样器
    sampler = llama_sampler_chain_init(sampler_params);
    llama_sampler_chain_add(sampler, llama_sampler_init_penalties(llama_vocab_n_tokens(vocab), 64, 1.1f, 0.0f, 0.0f));
    llama_sampler_chain_add(sampler, llama_sampler_init_top_k(60));
    llama_sampler_chain_add(sampler, llama_sampler_init_top_p(1.0f, 1));
    llama_sampler_chain_add(sampler, llama_sampler_init_temp(0.7f));
    llama_sampler_chain_add(sampler, llama_sampler_init_dist(std::random_device{}()));
    return true;
}

/* ** 流式对话 ** */
void llama_llm::flowChat(const std::string& prompt) {
    // Token 分词处理
    const std::vector<llama_token> tokens = participle(llm_template(prompt).c_str());
    // 构建 Batch
    llama_batch batch = llama_batch_init(static_cast<int32_t>(tokens.size()), 0, 1);
    batch.n_tokens = static_cast<int32_t>(tokens.size());
    // 写入 KV Cache
    for (int index = 0; index < tokens.size(); ++index) {
        batch.token[index] = tokens[index];
        batch.pos[index] = kv_pos + index;
        batch.seq_id[index][0] = 0;
        batch.n_seq_id[index] = 1;
        batch.logits[index] = false;
    }
    batch.logits[batch.n_tokens - 1] = true;
    // Prefill
    if (llama_decode(context, batch) != 0) { std::cerr << "[ ERROR ] >> FlowChat : 解码处理失败"; llama_batch_free(batch); return; }
    // 更新 KV Cache 位置
     kv_pos += static_cast<llama_pos>(tokens.size());
    llama_batch_free(batch);
    // 自回归生成循环
    std::string content;
    constexpr int MAX_TOKENS= 512;
    llama_batch reg = llama_batch_init(1, 0, 1);
    for (int index = 0; index < MAX_TOKENS; ++index) {
        // 采集生成
        const llama_token new_token_id = llama_sampler_sample(sampler, context, -1);
        // EOG Token 结束
        if (llama_vocab_is_eog(vocab, new_token_id)) break;

        reg.n_tokens = 1;
        reg.token[0] = new_token_id;
        reg.pos[0] = kv_pos;
        reg.seq_id[0][0] = 0;
        reg.n_seq_id[0] = 1;
        reg.logits[0] = true;

        // 解码
        if (llama_decode(context, reg) != 0) { std::cerr << "[ ERROR ] >> FlowChat : 生成解码失败\n"; llama_batch_free(reg); return; }
        kv_pos ++;
        // 跳过控制类 Token
        if (llama_vocab_is_control(vocab, new_token_id))continue;
        // 翻译成文本
        std::vector<char> piece_buf(4096);
        if (const int n_chars = llama_token_to_piece(vocab, new_token_id, piece_buf.data(), static_cast<int32_t>(piece_buf.size()), 0, false); n_chars > 0) {
            content.append(piece_buf.data(), n_chars);
            std::cout << std::string(piece_buf.data(), n_chars);
        }
    }
    llama_batch_free(reg);
    std::cout << std::endl;
}
/* ** 资源清理 ** */
void llama_llm::cleanUp() {
    if (sampler) { llama_sampler_free(sampler); sampler = nullptr; }
    if (context) { llama_free(context); context = nullptr; }
    if (model) { llama_model_free(model); model = nullptr; }
    vocab = nullptr;
    llama_backend_free();
}