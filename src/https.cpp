#include "https.h"

#include <orbis/Http.h>
#include <orbis/Net.h>
#include <orbis/Ssl.h>
#include <orbis/Sysmodule.h>
#include <orbis/libkernel.h>

#include <cstdint>
#include <vector>

#include "log.h"

// NOT: sceHttp/sceSsl imzalari OpenOrbis surumune gore degisebilir. Derleme
// hatasi olursa toolchain'in orbis/Http.h basligindaki prototiplerle karsilastirin;
// akis (sysmodule -> ssl init -> http init -> template -> connection -> request)
// aynidir.

namespace https {
namespace {

bool g_ready = false;
int g_httpCtx = -1;

const int kPool = 256 * 1024;
const int kSslHeap = 304 * 1024;

std::string hex(int v) {
    char b[16];
    snprintf(b, sizeof b, "0x%08X", (unsigned)v);
    return b;
}

}  // namespace

bool init() {
    if (g_ready) return true;

    sceSysmoduleLoadModule(ORBIS_SYSMODULE_NET);
    sceSysmoduleLoadModule(ORBIS_SYSMODULE_SSL);
    sceSysmoduleLoadModule(ORBIS_SYSMODULE_HTTP);

    int netMem = sceNetPoolCreate("pkgstore_https", kPool, 0);
    int sslCtx = sceSslInit(kSslHeap);
    int httpCtx = sceHttpInit(netMem, sslCtx, kPool);
    applog("https init: net=%d ssl=%d http=%d", netMem, sslCtx, httpCtx);
    if (httpCtx < 0) {
        applog("sceHttpInit basarisiz (%s)", hex(httpCtx).c_str());
        return false;
    }
    g_httpCtx = httpCtx;
    g_ready = true;
    return true;
}

Result get(const std::string& url, int redirectsLeft) {
    Result res;
    if (!g_ready && !init()) {
        res.error = "HTTPS baslatilamadi";
        return res;
    }

    int tmpl = sceHttpCreateTemplate(g_httpCtx, "PKGMagaza/1.0", ORBIS_HTTP_VERSION_1_1, 1);
    if (tmpl < 0) {
        res.error = "HTTP sablonu olusturulamadi (" + hex(tmpl) + ")";
        return res;
    }
    sceHttpSetConnectTimeOut(tmpl, 15 * 1000 * 1000);
    sceHttpSetRecvTimeOut(tmpl, 15 * 1000 * 1000);
    sceHttpSetAutoRedirect(tmpl, 0);  // yonlendirmeleri kendimiz izleriz (Location okumak icin)

    int conn = sceHttpCreateConnectionWithURL(tmpl, url.c_str(), 1);
    int req = conn >= 0 ? sceHttpCreateRequestWithURL(conn, ORBIS_METHOD_GET, url.c_str(), 0) : conn;
    if (req < 0) {
        if (conn >= 0) sceHttpDeleteConnection(conn);
        sceHttpDeleteTemplate(tmpl);
        res.error = "HTTP istegi olusturulamadi (" + hex(req) + ")";
        return res;
    }

    int r = sceHttpSendRequest(req, nullptr, 0);
    if (r < 0) {
        res.error = "Baglanti/TLS hatasi (" + hex(r) + ")";
    } else {
        int status = 0;
        sceHttpGetStatusCode(req, &status);
        res.status = status;

        // Yonlendirme: Location basligini okuyup yeni adrese git (GitHub raw ->
        // objects.githubusercontent.com yonlendirir).
        if (status >= 300 && status < 400 && redirectsLeft > 0) {
            char* hdrs = nullptr;
            size_t hdrSize = 0;
            std::string next;
            if (sceHttpGetAllResponseHeaders(req, &hdrs, &hdrSize) >= 0 && hdrs && hdrSize) {
                std::string h(hdrs, hdrSize);
                size_t p = h.find("ocation:");  // "Location:" / "location:"
                if (p != std::string::npos) {
                    size_t s = h.find_first_not_of(" \t", p + 8);
                    size_t e = h.find_first_of("\r\n", s);
                    if (s != std::string::npos) next = h.substr(s, e - s);
                }
            }
            if (!next.empty()) {
                sceHttpDeleteRequest(req);
                sceHttpDeleteConnection(conn);
                sceHttpDeleteTemplate(tmpl);
                return get(next, redirectsLeft - 1);
            }
        }

        std::vector<char> buf(16384);
        for (;;) {
            int n = sceHttpReadData(req, buf.data(), buf.size());
            if (n <= 0) break;
            res.body.append(buf.data(), (size_t)n);
            if (res.body.size() > 4 * 1024 * 1024) break;  // version.json kucuktur
        }
    }

    sceHttpDeleteRequest(req);
    sceHttpDeleteConnection(conn);
    sceHttpDeleteTemplate(tmpl);
    return res;
}

}  // namespace https
