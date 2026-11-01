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
#include "../include/SpscQueue.h"

#include <iostream>

void SpscQueue::enter(std::string value_) {
    value[w] = value_;
    if (w == 255) w = 0;
    else ++ w;
}

std::string SpscQueue::take() {
    if (r + 1 == w) return "";
    if (r == 255) r = 0;
    else ++ r;
    return value[r];
}

bool SpscQueue::check() {
    return status;
}