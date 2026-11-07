#include "../include/msgCache.h"
#include <iostream>

void MsgCache::enter(std::string value_) {
    value[w] = value_;
    if (w == 255) w = 0;
    else ++ w;
}

std::string MsgCache::take() {
    if (r + 1 == w) return "";
    if (r == 255) r = 0;
    else ++ r;
    return value[r];
}

bool MsgCache::check() { return status; }