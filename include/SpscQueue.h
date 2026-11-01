/*
* 叶汐AI角色专用推理框架 - 通用开源版
 * Copyright (C) 2026 夏瑞朋
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
#pragma once
#include <string>
class SpscQueue {
    static inline std::string value[256];
    static inline int w = 0, r = -1;
    static inline bool status = true;
public:
    SpscQueue() = default;
    void enter(std::string value);
    std::string take();
    bool check();
    void uStatus(const bool status_) { status = status_; }
};