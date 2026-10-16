#pragma once
#include <string>

#include "catalog.h"

namespace installer {

struct Outcome {
    bool ok = false;
    std::string message;
};

// mode: "auto" (once BGFT, olmazsa Remote Package Installer), "bgft" veya "rpi"
Outcome install(const catalog::Item& item, int userId, const std::string& mode);

}  // namespace installer
