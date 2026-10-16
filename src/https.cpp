#include "https.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

#include "log.h"

// OpenOrbis'teki orbis/Http.h eksik (fonksiyonlar parametresiz bildirilmis,
// ORBIS_SYSMODULE_NET/SSL/HTTP yok). Bu yuzden gerekli libSceHttp/Ssl/Net/Sysmodule
// fonksiyonlarini dogru imzalariyla burada kendimiz bildiriyoruz. Baglama NID ile
// yapildigi icin -lSceHttp dogru sembole cozer; calisma zamaninda konsoldaki gercek
// modul cagrilir.
//
// NOT: Asagidaki sysmodule kimlikleri (NET/SSL/HTTP) toolchain surumune gore
// farkli olabilir. Yanlis olurlarsa modul yuklenmez ve guncelleme kontrolu sessizce
// atlanir (uygulama yine calisir). Cihazdaki log.txt'de "https init" satirina bakip
// gerekirse buradaki degerleri duzeltecegiz.
extern "C" {
int sceSysmoduleLoadModule(uint16_t id);
int sceNetPoolCreate(const char* name, int size, int flags);
int sceSslInit(uint64_t poolSize);
int sceHttpInit(int netMemId, int sslCtxId, uint64_t poolSize);
int sceHttpCreateTemplate(int httpCtxId, const char* userAgent, int httpVer, int autoProxyConf);
int sceHttpCreateConnectionWithURL(int tmplId, const char* url, int enableKeepalive);
int sceHttpCreateRequestWithURL(int connId, int method, const char* url, uint64_t contentLength);
int sceHttpSendRequest(int reqId, const void* postData, uint64_t size);
int sceHttpGetStatusCode(int reqId, int* statusCode);
int sceHttpGetAllResponseHeaders(int reqId, char** header, uint64_t* headerSize);
int sceHttpReadData(int reqId, void* data, uint64_t size);
int sceHttpSetConnectTimeOut(int id, uint32_t usec);
int sceHttpSetRecvTimeOut(int id, uint32_t usec);
int sceHttpSetAutoRedirect(int id, int enable);
int sceHttpDeleteRequest(int reqId);
int sceHttpDeleteConnection(int connId);
int sceHttpDeleteTemplate(int tmplId);
}

static const uint16_t ORBIS_SYSMODULE_NET = 0x0009;
static const uint16_t ORBIS_SYSMODULE_HTTP = 0x000A;
static const uint16_t ORBIS_SYSMODULE_SSL = 0x000B;
static const int ORBIS_HTTP_VERSION_1_1 = 2;
static const int ORBIS_METHOD_GET = 0;

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
    // sceHttp baglami tek; farkli thread'lerden (katalog + kapaklar + guncelleme
    // kontrolu) ayni anda cagrilmasin diye istekleri seri hale getiriyoruz.
    // get() yonlendirmede kendini cagirdigi icin recursive_mutex.
    static std::recursive_mutex mtx;
    std::lock_guard<std::recursive_mutex> lock(mtx);

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
