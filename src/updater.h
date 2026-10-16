#pragma once
#include <cstdint>
#include <string>

// Uygulamanin kendi guncellemesi: GitHub'daki version.json okunur, surum
// karsilastirilir. Yeni surum pkg'si mevcut kurulum yoluyla (BGFT) indirilir.
namespace updater {

struct Update {
    bool available = false;
    std::string version;   // ornek "01.03"
    std::string notes;     // yenilikler
    std::string pkgUrl;    // GitHub release pkg adresi
    uint64_t size = 0;
    std::string title = "PKG Ma\xC4\x9F" "aza";
};

void start(const std::string& versionUrl);  // arka planda kontrol
bool ready();                                 // kontrol bitti mi
Update result();                              // sonuc (available=false ise guncel/erisilemedi)
const char* currentVersion();

}  // namespace updater
