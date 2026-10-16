#pragma once
#include <string>

namespace http {

struct Result {
    int status = 0;
    std::string body;
    std::string error;  // bos degilse istek hic tamamlanamadi
};

bool init();

// Sadece http:// desteklenir. "/" ile baslayan adresler yerel dosya olarak okunur.
Result request(const std::string& method, const std::string& url, const std::string& body = "",
               const std::string& contentType = "", int maxRedirects = 3);

}  // namespace http
