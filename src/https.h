#pragma once
#include <string>

// GitHub HTTPS ister; raw sceNet soketimiz TLS yapmaz. Bu katman PS4'un kendi
// libSceHttp'sini kullanir (http ve https). Kucuk metin dosyalari (version.json)
// icindir; buyuk pkg indirmeleri BGFT ile yapilir.
namespace https {

struct Result {
    int status = 0;
    std::string body;
    std::string error;  // bos degilse istek tamamlanamadi
};

bool init();  // sysmodule + ssl + http havuzlari (bir kez)
Result get(const std::string& url, int redirectsLeft = 4);

}  // namespace https
