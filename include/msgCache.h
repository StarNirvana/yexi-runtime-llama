#pragma once
#include <string>
class MsgCache {
    static inline std::string value[256];
    static inline int w = 0, r = -1;
    static inline bool status = true;
public:
    MsgCache() = default;
    void enter(std::string value);
    std::string take();
    bool check();
    void uStatus(const bool status_) { status = status_; }
};