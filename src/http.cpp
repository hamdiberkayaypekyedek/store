#include "http.h"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "log.h"

// libSceNet fonksiyonlari (stub'lar -lSceNet ile baglanir).
extern "C" {
int sceSysmoduleLoadModule(uint16_t id);
int sceNetInit(void);
int sceNetPoolCreate(const char* name, int size, int flags);
int sceNetResolverCreate(const char* name, int memid, int flags);
int sceNetResolverStartNtoa(int rid, const char* hostname, uint32_t* addr, int timeout, int retry, int flags);
int sceNetResolverDestroy(int rid);
int sceNetSocket(const char* name, int family, int type, int protocol);
int sceNetConnect(int s, const void* addr, uint32_t addrlen);
int sceNetSend(int s, const void* buf, size_t len, int flags);
int sceNetRecv(int s, void* buf, size_t len, int flags);
int sceNetSocketClose(int s);
int sceNetSetsockopt(int s, int level, int optname, const void* optval, uint32_t optlen);
}

namespace http {
namespace {

const int kAF_INET = 2;
const int kSOCK_STREAM = 1;
const int kSOL_SOCKET = 0xffff;
const int kSO_SNDTIMEO = 0x1005;
const int kSO_RCVTIMEO = 0x1006;

struct SockAddrIn {
    uint8_t len;
    uint8_t family;
    uint16_t port;
    uint32_t addr;
    uint8_t zero[8];
};

int g_pool = -1;

bool parseIPv4(const std::string& h, uint32_t& out) {
    unsigned a, b, c, d;
    char extra;
    if (sscanf(h.c_str(), "%u.%u.%u.%u%c", &a, &b, &c, &d, &extra) != 4) return false;
    if (a > 255 || b > 255 || c > 255 || d > 255) return false;
    uint8_t* p = (uint8_t*)&out;
    p[0] = (uint8_t)a; p[1] = (uint8_t)b; p[2] = (uint8_t)c; p[3] = (uint8_t)d;
    return true;
}

bool resolveHost(const std::string& host, uint32_t& addr, std::string& err) {
    if (parseIPv4(host, addr)) return true;
    int rid = sceNetResolverCreate("pkgstore_res", g_pool, 0);
    if (rid < 0) {
        err = "DNS cozumleyici acilamadi";
        return false;
    }
    int r = sceNetResolverStartNtoa(rid, host.c_str(), &addr, 0, 0, 0);
    sceNetResolverDestroy(rid);
    if (r < 0) {
        err = "Adres cozulemedi: " + host;
        return false;
    }
    return true;
}

bool readLocal(const std::string& path, std::string& out) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    fclose(f);
    return true;
}

std::string lower(std::string s) {
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

std::string hostHeader(const std::string& host, int port) {
    return port == 80 ? host : host + ":" + std::to_string(port);
}

bool parseUrl(const std::string& url, std::string& host, int& port, std::string& path, std::string& err) {
    if (url.compare(0, 8, "https://") == 0) {
        err = "HTTPS desteklenmiyor, http:// kullanin: " + url;
        return false;
    }
    if (url.compare(0, 7, "http://") != 0) {
        err = "Gecersiz adres: " + url;
        return false;
    }
    std::string rest = url.substr(7);
    size_t slash = rest.find('/');
    std::string hostport = rest.substr(0, slash);
    path = slash == std::string::npos ? "/" : rest.substr(slash);
    port = 80;
    size_t colon = hostport.rfind(':');
    if (colon != std::string::npos) {
        port = atoi(hostport.c_str() + colon + 1);
        hostport.resize(colon);
    }
    host = hostport;
    if (host.empty() || port <= 0 || port > 65535) {
        err = "Gecersiz adres: " + url;
        return false;
    }
    return true;
}

std::string dechunk(const std::string& in) {
    std::string out;
    size_t p = 0;
    while (p < in.size()) {
        size_t eol = in.find("\r\n", p);
        if (eol == std::string::npos) break;
        unsigned long n = strtoul(in.c_str() + p, nullptr, 16);
        p = eol + 2;
        if (n == 0) break;
        if (p + n > in.size()) { out.append(in, p, std::string::npos); break; }
        out.append(in, p, n);
        p += n + 2;
    }
    return out;
}

}  // namespace

bool init() {
    // NET sysmodule ag fonksiyonlarindan ONCE yuklenmeli, yoksa sceNetInit cokebilir.
    int m = sceSysmoduleLoadModule(0x0009 /* ORBIS_SYSMODULE_NET */);
    int r = sceNetInit();
    g_pool = sceNetPoolCreate("pkgstore_net", 128 * 1024, 0);
    applog("net mod=0x%08X sceNetInit=0x%08X pool=%d", (unsigned)m, (unsigned)r, g_pool);
    return g_pool >= 0;
}

Result request(const std::string& method, const std::string& url, const std::string& body,
               const std::string& contentType, int maxRedirects) {
    Result res;
    if (!url.empty() && url[0] == '/') {
        if (readLocal(url, res.body)) res.status = 200;
        else res.error = "Dosya bulunamadi: " + url;
        return res;
    }

    std::string host, path;
    int port = 80;
    if (!parseUrl(url, host, port, path, res.error)) return res;

    uint32_t addr = 0;
    if (!resolveHost(host, addr, res.error)) return res;

    int s = sceNetSocket("pkgstore_http", kAF_INET, kSOCK_STREAM, 0);
    if (s < 0) {
        res.error = "Soket acilamadi";
        return res;
    }
    int timeout = 10 * 1000 * 1000;  // mikrosaniye
    sceNetSetsockopt(s, kSOL_SOCKET, kSO_RCVTIMEO, &timeout, sizeof timeout);
    sceNetSetsockopt(s, kSOL_SOCKET, kSO_SNDTIMEO, &timeout, sizeof timeout);

    SockAddrIn sa;
    memset(&sa, 0, sizeof sa);
    sa.len = sizeof sa;
    sa.family = kAF_INET;
    sa.port = (uint16_t)(((port & 0xFF) << 8) | ((port >> 8) & 0xFF));
    sa.addr = addr;
    if (sceNetConnect(s, &sa, sizeof sa) < 0) {
        sceNetSocketClose(s);
        res.error = "Baglanilamadi: " + hostHeader(host, port);
        return res;
    }

    std::string req = method + " " + path + " HTTP/1.0\r\nHost: " + hostHeader(host, port) +
                      "\r\nUser-Agent: PKGMagaza/1.0\r\nAccept: */*\r\nConnection: close\r\n";
    if (!body.empty() || method == "POST") {
        req += "Content-Type: " + (contentType.empty() ? std::string("application/octet-stream") : contentType) + "\r\n";
        req += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    }
    req += "\r\n";
    req += body;

    size_t sent = 0;
    while (sent < req.size()) {
        int n = sceNetSend(s, req.data() + sent, req.size() - sent, 0);
        if (n <= 0) break;
        sent += (size_t)n;
    }
    if (sent < req.size()) {
        sceNetSocketClose(s);
        res.error = "Istek gonderilemedi: " + hostHeader(host, port);
        return res;
    }

    std::string raw;
    std::vector<char> buf(64 * 1024);
    for (;;) {
        int n = sceNetRecv(s, buf.data(), buf.size(), 0);
        if (n <= 0) break;
        raw.append(buf.data(), (size_t)n);
    }
    sceNetSocketClose(s);

    size_t he = raw.find("\r\n\r\n");
    if (he == std::string::npos) {
        res.error = "Gecersiz HTTP yaniti: " + hostHeader(host, port);
        return res;
    }
    std::string head = raw.substr(0, he);
    res.body = raw.substr(he + 4);

    size_t sp = head.find(' ');
    if (sp != std::string::npos) res.status = atoi(head.c_str() + sp + 1);

    std::string location;
    bool chunked = false;
    size_t pos = head.find("\r\n");
    while (pos != std::string::npos) {
        size_t next = head.find("\r\n", pos + 2);
        std::string line = head.substr(pos + 2, next == std::string::npos ? std::string::npos : next - pos - 2);
        size_t c = line.find(':');
        if (c != std::string::npos) {
            std::string k = lower(line.substr(0, c));
            std::string v = line.substr(c + 1);
            while (!v.empty() && v[0] == ' ') v.erase(0, 1);
            if (k == "location") location = v;
            else if (k == "transfer-encoding" && lower(v).find("chunked") != std::string::npos) chunked = true;
        }
        pos = next;
    }
    if (chunked) res.body = dechunk(res.body);

    if (res.status >= 300 && res.status < 400 && !location.empty() && maxRedirects > 0) {
        std::string next = location;
        if (next[0] == '/') next = "http://" + hostHeader(host, port) + next;
        bool keepBody = res.status == 307 || res.status == 308;
        return request(keepBody ? method : "GET", next, keepBody ? body : "", contentType, maxRedirects - 1);
    }
    return res;
}

}  // namespace http
