#pragma once
#include <cstdint>
#include <string>

// Takili oyun diskini arka planda tarar (sadece okuma).
namespace disc {

struct Info {
    bool scanned = false;   // en az bir tarama bitti
    bool present = false;   // disk takili
    std::string titleId, title, version, contentId, category;
    std::string sourcePath;     // bilginin okundugu dosya/klasor
    std::string iconPath;       // yerel png yolu (images::get ile cizilir)
    std::string installedPath;  // /user/app/<ID>/app.pkg (varsa)
    uint64_t size = 0;           // diskteki pkg ya da kurulu app.pkg boyutu
    uint64_t installedSize = 0;
    std::string error;           // okunamadiysa sebebi
};

void start();
void setActive(bool active);  // sadece Disk Kopyalama sekmesi aciksa tarar
Info snapshot();

}  // namespace disc
