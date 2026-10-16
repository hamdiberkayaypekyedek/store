#include "installer.h"

#include <orbis/libkernel.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "http.h"
#include "json.h"
#include "log.h"

namespace installer {
namespace {

// Sistemin arka plan indirme servisi (BGFT). Yapilar flatz'in Remote Package
// Installer'inda kullanilan duzene gore tanimlandi.
struct BgftInitParams {
    void* heap;
    size_t heapSize;
};

struct BgftDownloadParam {
    int32_t userId;
    int32_t entitlementType;
    const char* id;
    const char* contentUrl;
    const char* contentExUrl;
    const char* contentName;
    const char* iconPath;
    const char* skuId;
    int32_t option;
    const char* playgoScenarioId;
    const char* releaseDate;
    const char* packageType;
    const char* packageSubType;
    uint64_t packageSize;
};

struct BgftDownloadParamEx {
    BgftDownloadParam param;
    uint32_t slot;
};

const int32_t kOptDisableCdnQueryParam = 0x10000;
const uint32_t kErrAlreadyExists = 0x80990088;

typedef int (*AppInstUtilInitializeFn)(void);
typedef int (*BgftInitFn)(BgftInitParams*);
typedef int (*BgftRegisterFn)(BgftDownloadParamEx*, int*);
typedef int (*BgftStartFn)(int);

bool g_tried = false;
bool g_ready = false;
std::string g_err;
BgftRegisterFn g_register = nullptr;
BgftStartFn g_start = nullptr;

std::string hex(int v) {
    char b[16];
    snprintf(b, sizeof b, "0x%08X", (unsigned)v);
    return b;
}

int loadModule(const char* path) {
    int h = sceKernelLoadStartModule(path, 0, nullptr, 0, nullptr, nullptr);
    applog("modul %s -> %s", path, hex(h).c_str());
    return h;
}

void* sym(int handle, const char* name) {
    void* p = nullptr;
    if (handle < 0) return nullptr;
    if (sceKernelDlsym(handle, name, &p) != 0) return nullptr;
    return p;
}

bool bgftInit() {
    if (g_tried) return g_ready;
    g_tried = true;

    int appInst = loadModule("/system/common/lib/libSceAppInstUtil.sprx");
    int bgft = loadModule("/system/common/lib/libSceBgft.sprx");
    if (bgft < 0) {
        g_err = "libSceBgft yuklenemedi (" + hex(bgft) + ")";
        return false;
    }

    auto aiInit = (AppInstUtilInitializeFn)sym(appInst, "sceAppInstUtilInitialize");
    if (aiInit) applog("sceAppInstUtilInitialize -> %s", hex(aiInit()).c_str());

    auto init = (BgftInitFn)sym(bgft, "sceBgftServiceIntInit");
    g_register = (BgftRegisterFn)sym(bgft, "sceBgftServiceIntDownloadRegisterTaskByStorageEx");
    g_start = (BgftStartFn)sym(bgft, "sceBgftServiceDownloadStartTask");
    if (!g_start) g_start = (BgftStartFn)sym(bgft, "sceBgftServiceIntDownloadStartTask");
    if (!init || !g_register || !g_start) {
        g_err = "BGFT fonksiyonlari bulunamadi";
        return false;
    }

    static BgftInitParams params;
    params.heapSize = 1024 * 1024;
    params.heap = calloc(1, params.heapSize);
    int r = init(&params);
    applog("sceBgftServiceIntInit -> %s", hex(r).c_str());
    if (r != 0) {
        g_err = "BGFT baslatilamadi (" + hex(r) + ")";
        return false;
    }
    g_ready = true;
    return true;
}

Outcome viaBgft(const catalog::Item& it, int userId, bool& tryFallback) {
    Outcome o;
    tryFallback = true;
    if (!bgftInit()) {
        o.message = g_err;
        return o;
    }

    BgftDownloadParamEx p;
    memset(&p, 0, sizeof p);
    p.param.userId = userId;
    p.param.entitlementType = 5;
    p.param.id = "";
    p.param.contentUrl = it.pkg.c_str();
    p.param.contentExUrl = "";
    p.param.contentName = it.title.c_str();
    p.param.iconPath = "";
    p.param.skuId = "";
    p.param.option = kOptDisableCdnQueryParam;
    p.param.playgoScenarioId = "0";
    p.param.releaseDate = "";
    p.param.packageType = it.packageType.empty() ? "PS4GD" : it.packageType.c_str();
    p.param.packageSubType = "";
    p.param.packageSize = it.size;
    p.slot = 0;

    int task = -1;
    int r = g_register(&p, &task);
    applog("BGFT kayit '%s' -> %s task=%d", it.pkg.c_str(), hex(r).c_str(), task);
    if ((uint32_t)r == kErrAlreadyExists) {
        tryFallback = false;
        o.message = "Bu icerik zaten kurulu ya da indirme kuyrugunda.";
        return o;
    }
    if (r != 0) {
        o.message = "BGFT kayit hatasi (" + hex(r) + ")";
        return o;
    }
    r = g_start(task);
    applog("BGFT baslat task=%d -> %s", task, hex(r).c_str());
    if (r != 0) {
        o.message = "BGFT indirme baslatilamadi (" + hex(r) + ")";
        return o;
    }
    tryFallback = false;
    o.ok = true;
    o.taskId = task;
    o.method = 0;
    o.message = "Indirme basladi. Bildirimler > Indirmeler'den takip edebilirsin.";
    return o;
}

Outcome viaRpi(const catalog::Item& it) {
    Outcome o;
    std::string body = "{\"type\":\"direct\",\"packages\":[\"" + jsonEscape(it.pkg) + "\"]}";
    http::Result r = http::request("POST", "http://127.0.0.1:12800/api/install", body, "application/json");
    applog("RPI -> status=%d err='%s' body='%s'", r.status, r.error.c_str(), r.body.c_str());
    if (!r.error.empty()) {
        o.message = "Remote Package Installer'a ulasilamadi (acik mi?)";
        return o;
    }
    JVal j;
    std::string e;
    if (!jsonParse(r.body, j, e)) {
        o.message = "Remote Package Installer yaniti okunamadi";
        return o;
    }
    if (j["status"].str() == "success") {
        o.ok = true;
        o.method = 1;
        o.taskId = (int)j["task_id"].num(-1);
        o.message = "Remote Package Installer indirmeyi kuyruga ekledi.";
    } else {
        o.message = "RPI hatasi: " + j["error"].str(r.body.substr(0, 120));
    }
    return o;
}

}  // namespace

Outcome install(const catalog::Item& it, int userId, const std::string& mode) {
    std::string bgftMsg;
    if (mode != "rpi") {
        bool fallback = true;
        Outcome o = viaBgft(it, userId, fallback);
        if (o.ok || !fallback || mode == "bgft") return o;
        bgftMsg = o.message;
    }
    Outcome o = viaRpi(it);
    if (!o.ok && !bgftMsg.empty()) o.message = bgftMsg + "  |  " + o.message;
    return o;
}

}  // namespace installer
