#include <future>
#include <iostream>
#include <windows.h>
#include "include/llama_llm.h"
int main() {
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
    llama_llm llm;
    MsgCache msgCache;
    if (! llm.init()) { llm.cleanUp(); return -1; };
    std::string input;
    while (true) {
        std::getline(std::cin, input);
        if (input == "#|exit") break;
        // 异步任务
        std::thread t(&llama_llm::flowChat, &llm, input);
        t.detach();
        std::string value;
        while (msgCache.check()) {
            if ((value = msgCache.take()) == "<|channel>" || value == "thought" || value == "<channel|>") continue;
            std::cout << value;
        }
        msgCache.uStatus(true);
        std::cout << std::endl;
    }
    llm.cleanUp();
    return 0;
}