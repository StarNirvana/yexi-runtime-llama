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
/* -- -- -- -- Private Function -- -- -- -- */
/* ** 分词器 ** */
std::vector<llama_token> llama_llm::participle(const char* prompt) {
    std::vector<llama_token> prompt_tokens(-llama_tokenize(vocab, prompt, static_cast<int32_t>(strlen(prompt) + 1), nullptr, 0, true, true ));
    llama_tokenize(vocab, prompt, static_cast<int32_t>(strlen(prompt) + 1), prompt_tokens.data(), static_cast<int32_t>(prompt_tokens.size()), true, true);
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
void llama_llm::flowChat(const char* prompt) {
    // Token 分词处理
    std::vector<llama_token> tokens = participle(prompt);
    // 构建 Batch
    const llama_batch batch = llama_batch_get_one(tokens.data(), static_cast<int32_t>(tokens.size()));
    // 首次解码并 Prefill
    if (llama_decode(context, batch) != 0) { std::cerr << "[ ERROR ] >> FlowChat : 解码处理失败"; return; }
    // 自回归生成循环
    std::string content;
    constexpr int MAX_TOKENS= 512;
    for (int index = 0; index < MAX_TOKENS; ++index) {
        // 采集生成
        llama_token new_token_id = llama_sampler_sample(sampler, context, -1);
        // EOG Token 结束
        if (llama_vocab_is_eog(vocab, new_token_id)) break;
        // 解码
        char buf[256];
        if (const int n_chars = llama_token_to_piece(vocab, new_token_id, buf, sizeof(buf), 0, false); n_chars > 0) std::cout << std::string(buf, n_chars);
        // 自回归自增
        if (llama_decode(context, llama_batch_get_one(&new_token_id, 1)) != 0) { std::cerr << "[ ERROR ] >> FlowChat : 解码处理失败"; return; }
    }
}
/* ** 资源清理 ** */
void llama_llm::cleanUp() {
    if (sampler) { llama_sampler_free(sampler); sampler = nullptr; }
    if (context) { llama_free(context); context = nullptr; }
    if (model) { llama_model_free(model); model = nullptr; }
    vocab = nullptr;
    llama_backend_free();
}