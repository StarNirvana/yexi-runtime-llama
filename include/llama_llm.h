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
#ifndef YXAGENT_LLAMA_LLM_H
#define YXAGENT_LLAMA_LLM_H
#include <string>
#include <vector>
#include <llama.h>
class llama_llm {
    /* -- -- -- Params and Pointer -- -- -- */
    // GGUF 模型
    llama_model* model =  nullptr;
    llama_model_params model_params = llama_model_default_params();
    // 推理上下文
    llama_context*  context = nullptr;
    llama_context_params context_params = llama_context_default_params();
    // Token 采样器
    llama_sampler* sampler = nullptr;
    llama_sampler_chain_params sampler_params = llama_sampler_chain_default_params();
    // Token 词表
    const llama_vocab* vocab = nullptr;
    // 会话记录
    struct llm_message { std::string role; std::string prompt; };
    std::vector<llm_message> conversation;
    /* -- -- -- Private function -- -- -- */
    // 指令模板
    std::string llm_template();
    // 分词器
    std::vector<llama_token> participle(const char* prompt);
public:
    llama_llm() = default;
    ~llama_llm() = default;
    llama_llm(const llama_llm&) = delete;
    llama_llm& operator=(const llama_llm&) = delete;
    // 初始化设置
    bool init();
    // 流式对话
    void flowChat(std::string prompt);
    void cleanUp();
#endif //YXAGENT_LLAMA_LLM_H
};
