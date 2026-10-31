#pragma once

#include <string>
#include <utility>

namespace HIKARI {

// 省略可能な出力先へ診断文を書く。outMessage が nullptr なら捨てる。
inline void SetMessage(std::string* outMessage, std::string message) {
    if (outMessage != nullptr) {
        *outMessage = std::move(message);
    }
}

} // namespace HIKARI
