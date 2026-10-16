#include "disc.h"

#include <dirent.h>
#include <orbis/libkernel.h>
#include <pthread.h>

#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "log.h"
#include "sfo.h"

namespace disc {
namespace {

const char* kDiscRoot = "/mnt/disc";
const char* kCacheDir = "/data/pkgstore/cache";

pthread_mutex_t g_mx;
Info g_info;
std::atomic<bool> g_active{false};
std::string g_lastListing;

bool statPath(const std::string& p, bool* isDir = nullptr, uint64_t* size = nullptr) {
    OrbisKernelStat st;
    memset(&st, 0, sizeof st);
    if (sceKernelStat(p.c_str(), &st) != 0) return false;
    if (isDir) *isDir = (st.st_mode & 0170000) == 0040000;
    if (size) *size = (uint64_t)st.st_size;
    return true;
}

std::vector<std::string> listDir(const std::string& p) {
    std::vector<std::string> out;
    DIR* d = opendir(p.c_str());
    if (!d) return out;
    while (dirent* e = readdir(d)) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        out.push_back(e->d_name);
    }
    closedir(d);
    return out;
}

bool endsWithI(const std::string& s, const char* suffix) {
    size_t n = strlen(suffix);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; i++)
        if (tolower((unsigned char)s[s.size() - n + i]) != tolower((unsigned char)suffix[i])) return false;
    return true;
}

bool readAt(FILE* f, uint64_t off, size_t len, std::vector<uint8_t>& out) {
    out.resize(len);
    if (fseek(f, (long)off, SEEK_SET) != 0) return false;
    return fread(out.data(), 1, len, f) == len;
}

bool readWhole(const std::string& p, std::vector<uint8_t>& out, size_t maxBytes) {
    FILE* f = fopen(p.c_str(), "rb");
    if (!f) return false;
    out.clear();
    uint8_t buf[16384];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0 && out.size() < maxBytes) out.insert(out.end(), buf, buf + n);
    fclose(f);
    return !out.empty();
}

uint32_t be32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

// PS4 pkg'nin girdi tablosundan param.sfo (0x1000) ve icon0.png (0x1200) okunur.
// Bu girdiler pkg icinde sifresiz durur.
bool readPkgMeta(const std::string& path, std::vector<uint8_t>& sfo, std::vector<uint8_t>& icon) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    std::vector<uint8_t> head;
    bool ok = readAt(f, 0, 0x20, head) && memcmp(head.data(), "\x7F" "CNT", 4) == 0;
    if (ok) {
        uint32_t count = be32(&head[0x10]), table = be32(&head[0x18]);
        std::vector<uint8_t> t;
        ok = count > 0 && count < 10000 && readAt(f, table, (size_t)count * 32, t);
        for (uint32_t i = 0; ok && i < count; i++) {
            const uint8_t* e = &t[(size_t)i * 32];
            uint32_t id = be32(e), off = be32(e + 16), size = be32(e + 20);
            if (id == 0x1000 && size < (1u << 20)) readAt(f, off, size, sfo);
            else if (id == 0x1200 && size < (8u << 20)) readAt(f, off, size, icon);
        }
    }
    fclose(f);
    if (icon.size() < 8 || memcmp(icon.data(), "\x89PNG", 4) != 0) icon.clear();
    return ok && !sfo.empty();
}

void applySfo(Info& in, const std::map<std::string, std::string>& sfo) {
    auto get = [&](const char* k) {
        auto it = sfo.find(k);
        return it == sfo.end() ? std::string() : it->second;
    };
    if (in.titleId.empty()) in.titleId = get("TITLE_ID");
    if (in.title.empty()) in.title = get("TITLE");
    if (in.version.empty()) in.version = get("APP_VER").empty() ? get("VERSION") : get("APP_VER");
    if (in.contentId.empty()) in.contentId = get("CONTENT_ID");
    if (in.category.empty()) in.category = get("CATEGORY");
}

bool looksLikeTitleId(const std::string& s) {
    if (s.size() != 9) return false;
    for (int i = 0; i < 4; i++)
        if (s[i] < 'A' || s[i] > 'Z') return false;
    for (int i = 4; i < 9; i++)
        if (s[i] < '0' || s[i] > '9') return false;
    return true;
}

// Teshis icin: disk koku ve bir alt seviye log'a yazilir (degistiginde).
void logListing(const std::vector<std::string>& top) {
    std::string listing;
    for (auto& n : top) {
        listing += "  " + n;
        bool dir = false;
        if (statPath(std::string(kDiscRoot) + "/" + n, &dir) && dir) {
            listing += "/";
            auto sub = listDir(std::string(kDiscRoot) + "/" + n);
            for (size_t i = 0; i < sub.size() && i < 12; i++) listing += "\n      " + sub[i];
        }
        listing += "\n";
    }
    if (listing == g_lastListing) return;
    g_lastListing = listing;
    applog("disk icerigi (%s):\n%s", kDiscRoot, listing.c_str());
}

Info scan() {
    Info in;
    in.scanned = true;

    bool isDir = false;
    if (!statPath(kDiscRoot, &isDir) || !isDir) {
        if (!statPath("/system_data")) in.error = "Uygulama korumal\xC4\xB1 alanda (sandbox) \xC3\xA7" "al\xC4\xB1\xC5\x9F\xC4\xB1yor; GoldHEN SDK ile derleyin.";
        return in;
    }
    std::vector<std::string> top = listDir(kDiscRoot);
    if (top.empty()) return in;  // bos baglama noktasi: disk yok
    in.present = true;
    logListing(top);

    std::vector<uint8_t> sfoBytes, icon;

    // 1) Klasor duzeni: /mnt/disc/sce_sys/param.sfo
    std::string sfoPath = std::string(kDiscRoot) + "/sce_sys/param.sfo";
    if (readWhole(sfoPath, sfoBytes, 1 << 20)) {
        applySfo(in, parseSfo(sfoBytes));
        in.sourcePath = sfoPath;
        readWhole(std::string(kDiscRoot) + "/sce_sys/icon0.png", icon, 8 << 20);
    }

    // 2) Pkg duzeni: kokte ya da bir alt klasorde *.pkg
    if (in.titleId.empty()) {
        std::vector<std::string> candidates;
        for (auto& n : top) {
            std::string p = std::string(kDiscRoot) + "/" + n;
            if (endsWithI(n, ".pkg")) candidates.push_back(p);
            else if (statPath(p, &isDir) && isDir)
                for (auto& s : listDir(p))
                    if (endsWithI(s, ".pkg")) candidates.push_back(p + "/" + s);
        }
        for (auto& p : candidates) {
            sfoBytes.clear();
            icon.clear();
            if (!readPkgMeta(p, sfoBytes, icon)) continue;
            applySfo(in, parseSfo(sfoBytes));
            in.sourcePath = p;
            statPath(p, nullptr, &in.size);
            if (in.category.empty() || in.category == "gd") break;  // oyun pkg'si bulundu
        }
    }

    // 3) Son care: klasor adi CUSA12345 gibi ise
    if (in.titleId.empty())
        for (auto& n : top)
            if (looksLikeTitleId(n)) { in.titleId = n; in.sourcePath = std::string(kDiscRoot) + "/" + n; break; }

    if (in.titleId.empty()) {
        in.error = "Diskte oyun bilgisi bulunamad\xC4\xB1 (ayr\xC4\xB1nt\xC4\xB1 log.txt'de).";
        return in;
    }

    // Disk oyunlari konsola da kurulur: kurulu kopya ve sistem meta verisi
    std::string installed = "/user/app/" + in.titleId + "/app.pkg";
    if (statPath(installed, nullptr, &in.installedSize)) in.installedPath = installed;
    std::string metaDir = "/system_data/priv/appmeta/" + in.titleId;
    if (in.title.empty()) {
        std::vector<uint8_t> b;
        if (readWhole(metaDir + "/param.sfo", b, 1 << 20)) applySfo(in, parseSfo(b));
    }
    if (in.size == 0) in.size = in.installedSize;

    if (statPath(metaDir + "/icon0.png")) {
        in.iconPath = metaDir + "/icon0.png";
    } else if (!icon.empty()) {
        sceKernelMkdir(kCacheDir, 0777);
        std::string out = std::string(kCacheDir) + "/" + in.titleId + "_icon0.png";
        if (FILE* f = fopen(out.c_str(), "wb")) {
            fwrite(icon.data(), 1, icon.size(), f);
            fclose(f);
            in.iconPath = out;
        }
    }
    return in;
}

bool sameInfo(const Info& a, const Info& b) {
    return a.scanned == b.scanned && a.present == b.present && a.titleId == b.titleId && a.title == b.title &&
           a.sourcePath == b.sourcePath && a.installedPath == b.installedPath && a.size == b.size &&
           a.iconPath == b.iconPath && a.error == b.error;
}

void* worker(void*) {
    for (;;) {
        if (!g_active.load()) {
            sceKernelUsleep(300 * 1000);
            continue;
        }
        Info in = scan();
        pthread_mutex_lock(&g_mx);
        bool changed = !sameInfo(in, g_info);
        g_info = in;
        pthread_mutex_unlock(&g_mx);
        if (changed)
            applog("disk: takili=%d id=%s baslik='%s' kaynak=%s kurulu=%s hata='%s'", (int)in.present,
                   in.titleId.c_str(), in.title.c_str(), in.sourcePath.c_str(), in.installedPath.c_str(),
                   in.error.c_str());
        for (int i = 0; i < 30 && g_active.load(); i++) sceKernelUsleep(100 * 1000);  // ~3 sn
    }
    return nullptr;
}

}  // namespace

void start() {
    pthread_mutex_init(&g_mx, nullptr);
    pthread_t t;
    pthread_create(&t, nullptr, worker, nullptr);
}

void setActive(bool active) { g_active.store(active); }

Info snapshot() {
    pthread_mutex_lock(&g_mx);
    Info copy = g_info;
    pthread_mutex_unlock(&g_mx);
    return copy;
}

}  // namespace disc
