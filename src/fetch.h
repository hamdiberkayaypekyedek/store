#pragma once
#include <string>

#include "http.h"
#include "https.h"

// Adres semasina gore dogru istemciyi secer: https:// -> sceHttp (TLS),
// http:// ve "/..." yerel dosya -> ham soket istemcisi. Katalog ve kapak
// gorselleri hem PC sunucusundan (http) hem GitHub'dan (https) okunabilsin diye.
namespace netfetch {

struct Result {
    int status = 0;
    std::string body;
    std::string error;
};

inline Result get(const std::string& url) {
    Result out;
    if (url.compare(0, 8, "https://") == 0) {
        https::Result r = https::get(url);
        out.status = r.status;
        out.body = std::move(r.body);
        out.error = std::move(r.error);
    } else {
        http::Result r = http::request("GET", url);
        out.status = r.status;
        out.body = std::move(r.body);
        out.error = std::move(r.error);
    }
    return out;
}

}  // namespace netfetch
