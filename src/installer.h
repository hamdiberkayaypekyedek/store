#pragma once
#include <string>

#include "catalog.h"

namespace installer {

struct Outcome {
    bool ok = false;
    std::string message;
    int taskId = -1;    // ilerleme takibi icin (RPI gorev kimligi; BGFT gorev no)
    int method = 0;     // 0 = BGFT, 1 = Remote Package Installer
};

// mode: "auto" (once BGFT, olmazsa Remote Package Installer), "bgft" veya "rpi"
Outcome install(const catalog::Item& item, int userId, const std::string& mode);

}  // namespace installer
