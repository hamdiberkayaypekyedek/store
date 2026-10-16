#include "updater.h"

#include <orbis/libkernel.h>
#include <pthread.h>

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "https.h"
#include "json.h"
#include "log.h"

#ifndef APP_VERSION
#define APP_VERSION "01.00"
#endif

namespace updater {
namespace {

std::atomic<bool> g_ready{false};
Update g_result;
std::string g_url;

// "01.03" > "01.00" karsilastirmasi. Noktayla ayrilmis sayilar; sayisal kiyas.
int cmpVersion(const std::string& a, const std::string& b) {
    size_t ia = 0, ib = 0;
    while (ia < a.size() || ib < b.size()) {
        long na = 0, nb = 0;
        while (ia < a.size() && a[ia] >= '0' && a[ia] <= '9') na = na * 10 + (a[ia++] - '0');
        while (ib < b.size() && b[ib] >= '0' && b[ib] <= '9') nb = nb * 10 + (b[ib++] - '0');
        if (na != nb) return na < nb ? -1 : 1;
        while (ia < a.size() && (a[ia] < '0' || a[ia] > '9')) ia++;
        while (ib < b.size() && (b[ib] < '0' || b[ib] > '9')) ib++;
    }
    return 0;
}

void* worker(void* arg) {
    std::string url = *(std::string*)arg;
    delete (std::string*)arg;

    https::Result r = https::get(url);
    if (!r.error.empty() || r.status != 200) {
        applog("guncelleme kontrol edilemedi: durum=%d hata='%s'", r.status, r.error.c_str());
        g_ready.store(true);
        return nullptr;
    }

    JVal j;
    std::string e;
    if (!jsonParse(r.body, j, e)) {
        applog("version.json okunamadi: %s", e.c_str());
        g_ready.store(true);
        return nullptr;
    }

    std::string remote = j["version"].str();
    std::string pkg = j["pkg"].str();
    if (remote.empty() || pkg.empty()) {
        applog("version.json eksik (version/pkg).");
        g_ready.store(true);
        return nullptr;
    }

    if (cmpVersion(APP_VERSION, remote) < 0) {
        g_result.available = true;
        g_result.version = remote;
        g_result.pkgUrl = pkg;
        g_result.notes = j["notes"].str();
        g_result.title = j["title"].str("PKG Ma\xC4\x9F" "aza");
        double sz = j["size"].num(0);
        g_result.size = sz > 0 ? (uint64_t)sz : 0;
        applog("guncelleme var: %s -> %s", APP_VERSION, remote.c_str());
    } else {
        applog("guncel surum (%s).", APP_VERSION);
    }
    g_ready.store(true);
    return nullptr;
}

}  // namespace

void start(const std::string& versionUrl) {
    if (versionUrl.empty()) {
        g_ready.store(true);
        return;
    }
    pthread_t t;
    pthread_create(&t, nullptr, worker, new std::string(versionUrl));
    pthread_detach(t);
}

bool ready() { return g_ready.load(); }
Update result() { return g_result; }
const char* currentVersion() { return APP_VERSION; }

}  // namespace updater
