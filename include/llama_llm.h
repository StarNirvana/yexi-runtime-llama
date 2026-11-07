#pragma once
#include <string>
#include <vector>
#include <llama.h>
#include "./msgCache.h"
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
    // KV Cache 位置
    llama_pos kv_pos = 0;
    // 队列
    MsgCache msgCache;
    /* -- -- -- Private function -- -- -- */
    // 指令模板
    std::string llm_template(const std::string& prompt);
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
    void flowChat(const std::string& prompt);
    void cleanUp();
};
