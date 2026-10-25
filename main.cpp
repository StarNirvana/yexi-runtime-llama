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
#include <iostream>
#include <windows.h>
#include "include/llama_llm.h"
int main() {
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
    llama_backend_init();
    llama_llm llm;
    if (! llm.init()) { llm.cleanUp(); return -1;}
    std::string input;
    std::cin >> input;
    llm.flowChat(input.c_str());
    return 0;
}