// PKG Mağaza — store tarzı fake pkg kurucu (OpenOrbis + SDL2)
#include <SDL2/SDL.h>
#include <orbis/Pad.h>
#include <orbis/UserService.h>
#include <orbis/libkernel.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include "catalog.h"
#include "disc.h"
#include "gfx.h"
#include "http.h"
#include "https.h"
#include "installer.h"
#include "log.h"
#include "updater.h"

#ifdef USE_GOLDHEN_SDK
extern "C" {
#include <GoldHEN.h>
}
static struct jailbreak_backup g_jb;
#endif

#ifndef ORBIS_KERNEL_PRIO_FIFO_LOWEST
#define ORBIS_KERNEL_PRIO_FIFO_LOWEST 0x2FF
#endif

namespace {

const int SW = 1920, SH = 1080;
const char* kDataDir = "/data/pkgstore";
const char* kConfigPath = "/data/pkgstore/config.ini";
const char* kLocalCatalog = "/data/pkgstore/catalog.json";
const float kPi = 3.14159265f;

// Izgara yerlesimi
const int kCols = 6;
const int kTile = 264;
const int kGap = 28;
const int kGridX = (SW - (kCols * kTile + (kCols - 1) * kGap)) / 2;
const int kGridTop = 240;
const int kRowH = kTile + 100;
const int kClipTop = 212;
const int kHintY = 1000;

namespace C {
const Color white{255, 255, 255, 255};
const Color text2{255, 255, 255, 175};
const Color text3{255, 255, 255, 115};
const Color accent{0, 112, 209, 255};
const Color shadow{0, 0, 0, 110};
const Color cross{70, 130, 230, 255};
const Color circle{230, 70, 80, 255};
const Color ok{60, 200, 120, 255};
const Color err{235, 80, 80, 255};
const Color bgTop{0, 14, 46, 255};
const Color bgBottom{0, 62, 150, 255};
}  // namespace C

struct Config {
    std::string catalogUrl;
    std::string installer = "auto";
    std::string updateUrl;  // GitHub version.json (https). Bos: guncelleme kapali.
};

enum class Screen { Loading, Error, Browse, Detail };

struct Toast {
    std::string title, body;
    Color accent{0, 0, 0, 0};
    uint32_t until = 0;
};

struct Pad {
    int handle = -1;
    uint32_t cur = 0, prev = 0, fired = 0;
    struct Repeat {
        uint32_t mask;
        uint32_t t0, last;
    };
    Repeat reps[4] = {{ORBIS_PAD_BUTTON_UP, 0, 0},
                      {ORBIS_PAD_BUTTON_DOWN, 0, 0},
                      {ORBIS_PAD_BUTTON_LEFT, 0, 0},
                      {ORBIS_PAD_BUTTON_RIGHT, 0, 0}};

    void init(int user) {
        scePadInit();
        handle = scePadOpen(user, 0, 0, nullptr);
        applog("scePadOpen(user=%d) -> %d", user, handle);
    }
    void update() {
        prev = cur;
        OrbisPadData d;
        memset(&d, 0, sizeof d);
        cur = (handle >= 0 && scePadReadState(handle, &d) == 0) ? d.buttons : 0;
        fired = cur & ~prev;
        uint32_t now = SDL_GetTicks();
        for (auto& r : reps) {
            if (!(cur & r.mask)) continue;
            if (fired & r.mask) {
                r.t0 = r.last = now;
            } else if (now - r.t0 > 380 && now - r.last > 85) {
                fired |= r.mask;
                r.last = now;
            }
        }
    }
    bool hit(uint32_t m) const { return (fired & m) != 0; }
};

struct State {
    SDL_Renderer* r = nullptr;
    SDL_Texture* bg = nullptr;
    Config cfg;
    catalog::Catalog cat;
    std::string error;
    Screen screen = Screen::Loading;
    int catIdx = 0;
    int sel = 0;
    float scroll = 0;
    int userId = -1;
    Pad pad;
    Toast toast;
    bool busy = false;
    std::string busyText;
    bool updatePrompted = false;  // guncelleme kontrolu sonucu islendi
    bool showUpdate = false;      // guncelleme ekrani acik
    updater::Update update;
} g;

// ---------------------------------------------------------------- yardimcilar

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

Config loadConfig() {
    Config c;
    sceKernelMkdir(kDataDir, 0777);
    FILE* f = fopen(kConfigPath, "r");
    if (!f) {
        c.catalogUrl = "http://192.168.1.10:8000/catalog.json";
        f = fopen(kConfigPath, "w");
        if (f) {
            fputs("# PKG Magaza ayarlari\n"
                  "# Katalog adresi (PC'de: python tools/serve.py <klasor>). Bos birakilirsa\n"
                  "# /data/pkgstore/catalog.json kullanilir.\n"
                  "catalog_url=http://192.168.1.10:8000/catalog.json\n"
                  "# Kurulum yontemi: auto | bgft | rpi\n"
                  "installer=auto\n"
                  "# Uygulamanin kendi guncellemesi (GitHub version.json, https).\n"
                  "# Bos birakilirsa guncelleme kapatilir.\n"
                  "update_url=https://raw.githubusercontent.com/hamdiberkayaypekyedek/store/main/version.json\n",
                  f);
            fclose(f);
        }
        return c;
    }
    char line[1024];
    while (fgets(line, sizeof line, f)) {
        std::string l = trim(line);
        if (l.empty() || l[0] == '#') continue;
        size_t eq = l.find('=');
        if (eq == std::string::npos) continue;
        std::string k = trim(l.substr(0, eq)), v = trim(l.substr(eq + 1));
        if (k == "catalog_url") c.catalogUrl = v;
        else if (k == "installer") c.installer = v;
        else if (k == "update_url") c.updateUrl = v;
    }
    fclose(f);
    return c;
}

std::string catalogSource() { return g.cfg.catalogUrl.empty() ? kLocalCatalog : g.cfg.catalogUrl; }

std::string initials(const std::string& t) {
    std::string out;
    bool take = true;
    int count = 0;
    size_t i = 0;
    while (i < t.size() && count < 2) {
        unsigned char c = (unsigned char)t[i];
        size_t len = c < 0x80 ? 1 : c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
        if (c == ' ' || c == '-' || c == ':') {
            take = true;
            i++;
            continue;
        }
        if (take) {
            out += t.substr(i, len);
            count++;
            take = false;
        }
        i += len;
    }
    return out;
}

const char* kDiscTab = "Disk Kopyalama";

// Son sekme her zaman Disk Kopyalama; katalog yuklenemese de acilabilir.
bool onDiscTab() { return g.catIdx >= (int)g.cat.categories.size(); }
int tabCount() { return (int)g.cat.categories.size() + 1; }

std::vector<catalog::Item>& currentItems() {
    static std::vector<catalog::Item> none;
    return onDiscTab() ? none : g.cat.categories[g.catIdx].items;
}

const catalog::Item* currentItem() {
    auto& items = currentItems();
    if (items.empty()) return nullptr;
    return &items[std::min(g.sel, (int)items.size() - 1)];
}

void showToast(const std::string& title, const std::string& body, Color accent, uint32_t ms) {
    g.toast.title = title;
    g.toast.body = body;
    g.toast.accent = accent;
    g.toast.until = SDL_GetTicks() + ms;
}

// ---------------------------------------------------------------- cizim parcalari

void drawSpinner(int cx, int cy, int r) {
    int head = (int)(SDL_GetTicks() / 90) % 10;
    for (int i = 0; i < 10; i++) {
        float a = i * 2 * kPi / 10 - kPi / 2;
        int k = (head - i + 10) % 10;
        gfx::fillCircle(cx + (int)(std::cos(a) * r), cy + (int)(std::sin(a) * r), 7,
                        withAlpha(C::white, (uint8_t)(255 - k * 22)));
    }
}

void drawPlaceholder(const catalog::Item& it, int x, int y, int s) {
    static const Color pal[] = {{22, 60, 130, 255}, {90, 40, 120, 255}, {20, 100, 110, 255},
                                {120, 60, 30, 255}, {40, 90, 50, 255},  {110, 30, 60, 255}};
    unsigned hsh = 0;
    for (char ch : it.title) hsh = hsh * 31 + (unsigned char)ch;
    gfx::fillRect(x, y, s, s, pal[hsh % 6]);
    int px = s / 3;
    gfx::text(initials(it.title), x + s / 2, y + s / 2 - px * 2 / 3, px, withAlpha(C::white, 210), true, 0,
              gfx::Align::Center);
}

void drawCoverArt(const catalog::Item& it, int x, int y, int s) {
    int w = 0, h = 0;
    SDL_Texture* t = images::get(it.cover, &w, &h);
    if (t) gfx::drawImageCover(t, w, h, x, y, s, s);
    else drawPlaceholder(it, x, y, s);
}

void drawBackdrop(const catalog::Item* it, uint8_t alpha) {
    gfx::drawImage(g.bg, 0, 0, SW, SH);
    if (!it) return;
    int w = 0, h = 0;
    SDL_Texture* t = images::get(it->background.empty() ? it->cover : it->background, &w, &h);
    if (t) gfx::drawImageCover(t, w, h, 0, 0, SW, SH, alpha);
}

enum Hint { HCross, HCircle, HL1R1, HOptions };

void drawHints(std::initializer_list<std::pair<Hint, const char*>> hints) {
    gfx::fillRect(0, kHintY, SW, SH - kHintY, Color{0, 8, 25, 150});
    int x = kGridX;
    const int cy = kHintY + 40;
    auto badge = [&](const char* s) {
        int w = gfx::textWidth(s, 20, true) + 22;
        gfx::fillRoundRect(x, cy - 15, w, 30, 6, withAlpha(C::white, 230));
        gfx::text(s, x + 11, cy - 14, 20, Color{10, 20, 40, 255}, true);
        x += w + 8;
    };
    for (auto& h : hints) {
        switch (h.first) {
            case HCross:
                gfx::fillCircle(x + 16, cy, 16, withAlpha(C::white, 235));
                gfx::line(x + 9, cy - 7, x + 23, cy + 7, 3, C::cross);
                gfx::line(x + 9, cy + 7, x + 23, cy - 7, 3, C::cross);
                x += 42;
                break;
            case HCircle:
                gfx::fillCircle(x + 16, cy, 16, withAlpha(C::white, 235));
                gfx::ring(x + 16, cy, 9, 3, C::circle);
                x += 42;
                break;
            case HL1R1:
                badge("L1");
                badge("R1");
                x += 4;
                break;
            case HOptions:
                badge("OPTIONS");
                x += 4;
                break;
        }
        x += gfx::text(h.second, x, cy - 18, 26, C::text2) + 52;
    }
}

void drawToast() {
    if (SDL_GetTicks() >= g.toast.until) return;
    const int w = 700, h = 128, x = SW - 60 - w, y = 48;
    gfx::fillRoundRect(x, y, w, h, 12, Color{16, 20, 30, 242});
    gfx::fillRoundRect(x, y, 10, h, 5, g.toast.accent);
    gfx::text(g.toast.title, x + 34, y + 20, 30, C::white, true, w - 60);
    gfx::wrapText(g.toast.body, x + 34, y + 64, 23, w - 60, 30, 2, C::text2);
}

// ---------------------------------------------------------------- ekranlar

void drawTile(const catalog::Item& it, int x, int y, bool selected) {
    int s = kTile;
    if (selected) {
        const int grow = 14;
        x -= grow;
        y -= grow;
        s += grow * 2;
    }
    gfx::fillRect(x + 6, y + 10, s, s, C::shadow);
    drawCoverArt(it, x, y, s);
    if (selected) gfx::strokeRect(x - 5, y - 5, s + 10, s + 10, 5, C::white);
    int ty = y + s + (selected ? 18 : 14);
    gfx::text(it.title, x, ty, selected ? 28 : 26, selected ? C::white : C::text2, selected, s);
    gfx::text(catalog::formatSize(it.size), x, ty + 40, 22, C::text3, false, s);
}

void drawDiscGraphic(int cx, int cy, int r, bool present) {
    if (!present) {
        for (int a = 0; a < 36; a += 2) {  // kesikli cember: bos yuva
            float t0 = a * 2 * kPi / 36;
            gfx::fillCircle(cx + (int)(std::cos(t0) * r), cy + (int)(std::sin(t0) * r), 4, withAlpha(C::white, 70));
        }
        return;
    }
    gfx::fillCircle(cx + 6, cy + 18, r, Color{0, 0, 0, 100});
    gfx::fillCircle(cx, cy, r, Color{196, 207, 228, 255});
    for (int k = 0; k < 14; k++) gfx::ring(cx, cy, r - 10 - k * 10, 2, Color{255, 255, 255, (uint8_t)(k % 2 ? 50 : 22)});
    gfx::fillCircle(cx, cy, 78, Color{223, 229, 242, 255});
    gfx::ring(cx, cy, 78, 16, Color{176, 190, 216, 255});
    gfx::fillCircle(cx, cy, 24, Color{10, 22, 48, 255});
}

void renderDiscPanel() {
    disc::Info d = disc::snapshot();
    const int dcx = 380, dcy = 520;
    drawDiscGraphic(dcx, dcy, 230, d.present);

    int iw = 0, ih = 0;
    SDL_Texture* icon = d.present ? images::get(d.iconPath, &iw, &ih) : nullptr;
    if (icon) {
        gfx::fillRect(dcx + 66, dcy + 84, 220, 220, C::shadow);
        gfx::drawImageCover(icon, iw, ih, dcx + 56, dcy + 70, 220, 220);
        gfx::strokeRect(dcx + 56, dcy + 70, 220, 220, 3, withAlpha(C::white, 200));
    }

    const int x = 700, maxW = SW - x - 120;
    int y = 236;
    if (!d.scanned) {
        gfx::text("Disk aran\xC4\xB1yor\xE2\x80\xA6", x, y + 40, 48, C::white, true);
        drawSpinner(x + 40, y + 190, 30);
        return;
    }
    if (!d.present) {
        gfx::text("D\xC4\xB0SK BULUNAMADI", x, y, 24, C::text3, true);
        gfx::text("Bir oyun diski tak\xC4\xB1n", x, y + 40, 60, C::white, true, maxW);
        std::string msg = "Disk tak\xC4\xB1ld\xC4\xB1\xC4\x9F\xC4\xB1nda ba\xC5\x9Fl\xC4\xB1k, s\xC3\xBCr\xC3\xBCm ve boyut burada g\xC3\xB6r\xC3\xBCn\xC3\xBCr.";
        gfx::wrapText(msg, x, y + 150, 28, maxW, 42, 3, C::text2);
        if (!d.error.empty()) gfx::wrapText(d.error, x, y + 220, 26, maxW, 38, 3, Color{255, 170, 170, 255});
        return;
    }

    gfx::text("TAKILI D\xC4\xB0SK", x, y, 24, C::text3, true);
    std::string name = !d.title.empty() ? d.title : !d.titleId.empty() ? d.titleId : "Bilinmeyen disk";
    gfx::text(name, x, y + 40, 60, C::white, true, maxW);
    y += 136;

    int chx = x;
    auto chip = [&](const std::string& s) {
        if (s.empty()) return;
        int w = gfx::textWidth(s, 24) + 36;
        gfx::fillRoundRect(chx, y, w, 44, 22, Color{255, 255, 255, 40});
        gfx::text(s, chx + 18, y + 6, 24, C::white);
        chx += w + 14;
    };
    chip(d.titleId);
    chip(d.version.empty() ? "" : "S\xC3\xBCr\xC3\xBCm " + d.version);
    chip(catalog::formatSize(d.size));
    y += 80;

    auto row = [&](const char* label, const std::string& value) {
        gfx::text(label, x, y, 26, C::text3);
        gfx::text(value.empty() ? "-" : value, x + 300, y, 26, C::text2, false, maxW - 300);
        y += 46;
    };
    row("\xC4\xB0\xC3\xA7" "erik kimli\xC4\x9Fi", d.contentId);
    row("Kaynak", d.sourcePath);
    row("Konsolda kurulu",
        d.installedPath.empty() ? "Hay\xC4\xB1r" : "Evet  (" + catalog::formatSize(d.installedSize) + ")");
    if (!d.error.empty()) {
        gfx::wrapText(d.error, x, y, 26, maxW, 38, 2, Color{255, 170, 170, 255});
        y += 80;
    }

    y += 30;
    const int bw = 520, bh = 86;
    gfx::fillRoundRect(x, y, bw, bh, 10, Color{255, 255, 255, 36});
    gfx::text("Kopyalamay\xC4\xB1 ba\xC5\x9Flat", x + bw / 2, y + bh / 2 - 24, 36, C::text3, true, 0, gfx::Align::Center);
    gfx::text("Kopyalama bir sonraki ad\xC4\xB1mda eklenecek.", x, y + bh + 18, 22, C::text3);
}

void renderBrowse() {
    auto& cats = g.cat.categories;
    auto& items = currentItems();
    const int n = (int)items.size();
    drawBackdrop(currentItem(), 50);

    // Ust bilgi bandi
    gfx::fillRect(0, 0, SW, kClipTop, Color{0, 10, 32, 130});
    gfx::text(g.cat.name.empty() ? "PKG Ma\xC4\x9F" "aza" : g.cat.name, kGridX, 38, 46, C::white, true, 1100);
    std::string count;
    if (onDiscTab()) {
        disc::Info d = disc::snapshot();
        count = !d.scanned ? "" : d.present ? "Disk tak\xC4\xB1l\xC4\xB1" : "Disk yok";
    } else {
        count = std::to_string(n) + " i\xC3\xA7" "erik";
    }
    gfx::text(count, SW - kGridX, 54, 26, C::text2, false, 0, gfx::Align::Right);

    // Sekmeler: katalog kategorileri + Disk Kopyalama (secili sekme gorunur kalacak sekilde kaydirilir)
    std::vector<std::string> names;
    for (auto& c : cats) names.push_back(c.name);
    names.push_back(kDiscTab);
    std::vector<int> widths;
    int total = 0, selEnd = 0;
    for (size_t i = 0; i < names.size(); i++) {
        bool s = (int)i == g.catIdx;
        widths.push_back(gfx::textWidth(names[i], 32, s));
        total += widths.back() + 56;
        if (s) selEnd = total - 56;
    }
    int tabOffset = std::max(0, selEnd - (SW - 2 * kGridX));
    int tx = kGridX - tabOffset;
    for (size_t i = 0; i < names.size(); i++) {
        bool s = (int)i == g.catIdx;
        gfx::text(names[i], tx, 128, 32, s ? C::white : C::text3, s);
        if (s) gfx::fillRect(tx, 180, widths[i], 4, C::white);
        tx += widths[i] + 56;
    }

    if (onDiscTab()) {
        renderDiscPanel();
        drawHints({{HCross, "Kopyala"}, {HL1R1, "Kategori"}});
        return;
    }

    // Izgara
    int rowSel = g.sel / kCols;
    float target = (float)std::max(0, rowSel - 1) * kRowH;
    g.scroll += (target - g.scroll) * 0.22f;
    if (std::fabs(target - g.scroll) < 0.5f) g.scroll = target;

    SDL_Rect clip{0, kClipTop, SW, kHintY - kClipTop};
    SDL_RenderSetClipRect(g.r, &clip);
    for (int i = 0; i < n; i++) {
        if (i == g.sel) continue;
        int x = kGridX + (i % kCols) * (kTile + kGap);
        int y = kGridTop + (i / kCols) * kRowH - (int)g.scroll;
        if (y + kRowH < kClipTop || y > kHintY) continue;
        drawTile(items[i], x, y, false);
    }
    if (n > 0) {
        int x = kGridX + (g.sel % kCols) * (kTile + kGap);
        int y = kGridTop + rowSel * kRowH - (int)g.scroll;
        drawTile(items[g.sel], x, y, true);
    } else {
        gfx::text("Bu kategoride i\xC3\xA7" "erik yok", SW / 2, 500, 34, C::text2, false, 0, gfx::Align::Center);
    }
    SDL_RenderSetClipRect(g.r, nullptr);

    drawHints({{HCross, "G\xC3\xB6r\xC3\xBCnt\xC3\xBCle"}, {HL1R1, "Kategori"}, {HOptions, "Yenile"}});
}

void renderDetail() {
    const catalog::Item* itp = currentItem();
    if (!itp) return;
    const catalog::Item& it = *itp;

    drawBackdrop(&it, 255);
    gfx::fillRect(0, 0, SW, SH, Color{0, 8, 25, 178});

    const int cx = 120, cy = 160, cs = 580;
    gfx::fillRect(cx + 10, cy + 14, cs, cs, C::shadow);
    drawCoverArt(it, cx, cy, cs);

    const int x = cx + cs + 90, maxW = SW - x - 120;
    gfx::text(it.title, x, cy - 8, 64, C::white, true, maxW);
    int y = cy + 92;
    if (!it.publisher.empty()) {
        gfx::text(it.publisher, x, y, 30, C::text2, false, maxW);
        y += 58;
    }

    int chx = x;
    auto chip = [&](const std::string& s) {
        if (s.empty()) return;
        int w = gfx::textWidth(s, 24) + 36;
        gfx::fillRoundRect(chx, y, w, 44, 22, Color{255, 255, 255, 40});
        gfx::text(s, chx + 18, y + 6, 24, C::white);
        chx += w + 14;
    };
    chip(it.version.empty() ? "" : "S\xC3\xBCr\xC3\xBCm " + it.version);
    chip(catalog::formatSize(it.size));
    chip(it.id);
    y += 84;

    // Ana dugme (odakli: beyaz cerceve, hafif nabiz)
    const int bw = 460, bh = 86;
    float pulse = 0.5f + 0.5f * std::sin(SDL_GetTicks() / 260.0f);
    gfx::fillRoundRect(x - 6, y - 6, bw + 12, bh + 12, 14, withAlpha(C::white, (uint8_t)(170 + 85 * pulse)));
    gfx::fillRoundRect(x, y, bw, bh, 10, C::accent);
    gfx::text("\xC4\xB0ndir", x + bw / 2, y + bh / 2 - 25, 38, C::white, true, 0, gfx::Align::Center);
    y += bh + 34;

    gfx::text("Kaynak: " + catalog::hostOf(it.pkg), x, y, 22, C::text3, false, maxW);
    y += 62;
    gfx::wrapText(it.description.empty() ? "A\xC3\xA7\xC4\xB1klama yok." : it.description, x, y, 28, maxW, 42, 8,
                  C::text2);

    drawHints({{HCross, "\xC4\xB0ndir"}, {HCircle, "Geri"}});
}

void renderLoading() {
    gfx::drawImage(g.bg, 0, 0, SW, SH);
    gfx::text("Ma\xC4\x9F" "aza y\xC3\xBCkleniyor\xE2\x80\xA6", SW / 2, SH / 2 - 120, 40, C::white, true, 0,
              gfx::Align::Center);
    drawSpinner(SW / 2, SH / 2 + 10, 40);
}

void renderError() {
    gfx::drawImage(g.bg, 0, 0, SW, SH);
    const int x = kGridX + 40;
    gfx::text("Katalog y\xC3\xBCklenemedi", x, 200, 58, C::white, true);
    gfx::wrapText(g.error, x, 296, 30, 1500, 42, 3, Color{255, 170, 170, 255});
    int y = 460;
    gfx::text("Ayar dosyas\xC4\xB1:  " + std::string(kConfigPath), x, y, 28, C::text2);
    y += 50;
    gfx::text("Katalog adresi:  " + catalogSource(), x, y, 28, C::text2, false, 1500);
    y += 50;
    gfx::text("Bilgisayarda sunucuyu ba\xC5\x9Flat\xC4\xB1n:  python tools/serve.py <pkg klas\xC3\xB6r\xC3\xBC>", x, y, 28,
              C::text2);
    y += 50;
    gfx::text("Ayar dosyas\xC4\xB1n\xC4\xB1 FTP ile d\xC3\xBCzenleyip OPTIONS ile tekrar deneyin.", x, y, 28, C::text3);
    drawHints({{HOptions, "Tekrar dene"}, {HL1R1, "Disk Kopyalama"}});
}

void renderBusy() {
    gfx::fillRect(0, 0, SW, SH, Color{0, 0, 0, 170});
    const int w = 900, h = 230, x = (SW - w) / 2, y = (SH - h) / 2;
    gfx::fillRoundRect(x, y, w, h, 16, Color{16, 22, 36, 248});
    drawSpinner(x + 110, y + h / 2, 34);
    gfx::text(g.busyText, x + 190, y + 66, 32, C::white, true, w - 230);
    gfx::text("L\xC3\xBCtfen bekleyin", x + 190, y + 124, 26, C::text2);
}

void renderUpdate() {
    gfx::fillRect(0, 0, SW, SH, Color{0, 6, 20, 210});
    const int w = 1100, h = 620, x = (SW - w) / 2, y = (SH - h) / 2;
    gfx::fillRoundRect(x, y, w, h, 18, Color{16, 22, 36, 250});
    gfx::fillRoundRect(x, y, w, 10, 5, C::accent);

    const int px = x + 70;
    gfx::text("G\xC3\x9CNCELLEME MEVCUT", px, y + 54, 26, withAlpha(C::accent, 255), true);
    gfx::text(g.update.title, px, y + 96, 54, C::white, true, w - 140);

    std::string ver = std::string("S\xC3\xBCr\xC3\xBCm ") + updater::currentVersion() + "  \xE2\x86\x92  " + g.update.version;
    gfx::text(ver, px, y + 170, 32, C::text2, false, w - 140);
    if (g.update.size) gfx::text(catalog::formatSize(g.update.size), x + w - 70, y + 170, 30, C::text3, false, 0, gfx::Align::Right);

    gfx::fillRect(px, y + 224, w - 140, 2, Color{255, 255, 255, 30});
    gfx::text("Yenilikler", px, y + 244, 26, C::text3, true);
    gfx::wrapText(g.update.notes.empty() ? "Bu s\xC3\xBCr\xC3\xBCm\xC3\xBCn notu girilmemi\xC5\x9F." : g.update.notes,
                 px, y + 288, 28, w - 140, 42, 6, C::text2);

    const int bw = 440, bh = 82, by = y + h - 130;
    gfx::fillRoundRect(px, by, bw, bh, 10, C::accent);
    gfx::text("\xC5\x9Eimdi g\xC3\xBCncelle", px + bw / 2, by + bh / 2 - 23, 34, C::white, true, 0, gfx::Align::Center);
    gfx::text("Daha sonra (\xE2\x97\x8B)", px + bw + 60, by + bh / 2 - 18, 30, C::text3);
}

void render() {
    SDL_SetRenderDrawColor(g.r, 0, 0, 0, 255);
    SDL_RenderClear(g.r);
    switch (g.screen) {
        case Screen::Loading: renderLoading(); break;
        case Screen::Error: renderError(); break;
        case Screen::Browse: renderBrowse(); break;
        case Screen::Detail: renderDetail(); break;
    }
    if (g.showUpdate) renderUpdate();
    if (g.busy) renderBusy();
    drawToast();
    SDL_RenderPresent(g.r);
}

// ---------------------------------------------------------------- eylemler

void loadCatalogNow() {
    g.screen = Screen::Loading;
    render();
    images::clear();
    catalog::Catalog c;
    std::string err;
    std::string src = catalogSource();
    applog("katalog yukleniyor: %s", src.c_str());
    if (catalog::load(src, c, err)) {
        g.cat = std::move(c);
        g.catIdx = 0;
        g.sel = 0;
        g.scroll = 0;
        g.screen = Screen::Browse;
    } else {
        applog("katalog hatasi: %s", err.c_str());
        g.error = err;
        g.screen = Screen::Error;
    }
}

void doInstall(const catalog::Item& it) {
    g.busy = true;
    g.busyText = "\xE2\x80\x9C" + it.title + "\xE2\x80\x9D kuruluma g\xC3\xB6nderiliyor\xE2\x80\xA6";
    render();
    installer::Outcome o = installer::install(it, g.userId, g.cfg.installer);
    g.busy = false;
    if (o.ok) showToast("\xC4\xB0ndirme kuyru\xC4\x9F" "a eklendi", o.message, C::ok, 5000);
    else showToast("Kurulum ba\xC5\x9Flat\xC4\xB1lamad\xC4\xB1", o.message, C::err, 9000);
}

void doUpdate() {
    catalog::Item it;
    it.title = g.update.title + " " + g.update.version;
    it.pkg = g.update.pkgUrl;
    it.size = g.update.size;
    it.packageType = "PS4GD";
    g.busy = true;
    g.busyText = "G\xC3\xBCncelleme indiriliyor\xE2\x80\xA6";
    render();
    installer::Outcome o = installer::install(it, g.userId, g.cfg.installer);
    g.busy = false;
    if (o.ok) showToast("G\xC3\xBCncelleme ba\xC5\x9Flad\xC4\xB1", "\xC4\xB0ndirme bitince Bildirimler'den kurun, ard\xC4\xB1ndan uygulamay\xC4\xB1 yeniden a\xC3\xA7\xC4\xB1n.", C::ok, 7000);
    else showToast("G\xC3\xBCncelleme ba\xC5\x9Flat\xC4\xB1lamad\xC4\xB1", o.message, C::err, 9000);
}

void update() {
    const Pad& P = g.pad;

    if (g.showUpdate) {
        if (P.hit(ORBIS_PAD_BUTTON_CROSS)) { g.showUpdate = false; doUpdate(); }
        else if (P.hit(ORBIS_PAD_BUTTON_CIRCLE)) g.showUpdate = false;
        return;  // ekran acikken alttaki girdiler islenmez
    }

    if ((g.screen == Screen::Error || g.screen == Screen::Browse) && P.hit(ORBIS_PAD_BUTTON_OPTIONS)) {
        g.cfg = loadConfig();
        loadCatalogNow();
        return;
    }
    if (g.screen == Screen::Error && P.hit(ORBIS_PAD_BUTTON_R1)) {
        g.catIdx = (int)g.cat.categories.size();  // katalog olmadan da disk sekmesi acilir
        g.screen = Screen::Browse;
        return;
    }
    if (g.screen == Screen::Browse) {
        if (P.hit(ORBIS_PAD_BUTTON_L1) && g.catIdx > 0) { g.catIdx--; g.sel = 0; g.scroll = 0; }
        if (P.hit(ORBIS_PAD_BUTTON_R1) && g.catIdx + 1 < tabCount()) { g.catIdx++; g.sel = 0; g.scroll = 0; }
        if (onDiscTab()) {
            if (P.hit(ORBIS_PAD_BUTTON_CROSS))
                showToast("Hen\xC3\xBCz haz\xC4\xB1r de\xC4\x9Fil", "Bu s\xC3\xBCr\xC3\xBCm sadece disk bilgisini okuyor; kopyalama sonraki ad\xC4\xB1mda.", C::accent, 4000);
            return;
        }
        const int n = (int)currentItems().size();
        if (n == 0) return;
        if (P.hit(ORBIS_PAD_BUTTON_LEFT) && g.sel % kCols > 0) g.sel--;
        if (P.hit(ORBIS_PAD_BUTTON_RIGHT) && g.sel + 1 < n && g.sel % kCols < kCols - 1) g.sel++;
        if (P.hit(ORBIS_PAD_BUTTON_UP) && g.sel >= kCols) g.sel -= kCols;
        if (P.hit(ORBIS_PAD_BUTTON_DOWN)) {
            if (g.sel + kCols < n) g.sel += kCols;
            else if (g.sel / kCols < (n - 1) / kCols) g.sel = n - 1;
        }
        if (P.hit(ORBIS_PAD_BUTTON_CROSS)) g.screen = Screen::Detail;
    } else if (g.screen == Screen::Detail) {
        if (P.hit(ORBIS_PAD_BUTTON_CIRCLE)) g.screen = Screen::Browse;
        else if (P.hit(ORBIS_PAD_BUTTON_CROSS) && currentItem()) doInstall(*currentItem());
    }
}

}  // namespace

int main() {
    sceKernelMkdir(kDataDir, 0777);
    applog("---- PKG Magaza basliyor ----");

    OrbisUserServiceInitializeParams up;
    memset(&up, 0, sizeof up);
    up.priority = ORBIS_KERNEL_PRIO_FIFO_LOWEST;
    sceUserServiceInitialize(&up);
    sceUserServiceGetInitialUser(&g.userId);

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        applog("SDL_Init hatasi: %s", SDL_GetError());
        for (;;) sceKernelUsleep(1000000);
    }
    SDL_Window* win = SDL_CreateWindow("PKG Magaza", 0, 0, SW, SH, 0);
    g.r = SDL_CreateRenderer(win, -1, 0);
    if (!win || !g.r) {
        applog("pencere/renderer hatasi: %s", SDL_GetError());
        for (;;) sceKernelUsleep(1000000);
    }

    if (!gfx::init(g.r, "/app0/assets/font.ttf", "/app0/assets/font-bold.ttf"))
        applog("UYARI: font yuklenemedi, metinler gorunmeyecek");
    g.bg = gfx::verticalGradient(SH, C::bgTop, C::bgBottom);
#ifdef USE_GOLDHEN_SDK
    // Korumali alandan cikis: /mnt/disc, /user/app ve /system_data okunabilsin
    applog("sys_sdk_jailbreak -> %d", sys_sdk_jailbreak(&g_jb));
#endif
    http::init();
    https::init();
    images::start();
    disc::start();
    updater::start(g.cfg.updateUrl);
    g.pad.init(g.userId);
    g.cfg = loadConfig();
    loadCatalogNow();

    bool running = true;
    while (running) {
        uint32_t frameStart = SDL_GetTicks();
        SDL_Event e;
        while (SDL_PollEvent(&e))
            if (e.type == SDL_QUIT) running = false;
        g.pad.update();
        // Guncelleme kontrolu bitince, bir kere, uygun ekrandayken sor.
        if (!g.updatePrompted && updater::ready() &&
            (g.screen == Screen::Browse || g.screen == Screen::Error)) {
            g.updatePrompted = true;
            g.update = updater::result();
            if (g.update.available) g.showUpdate = true;
        }
        update();
        disc::setActive(g.screen == Screen::Browse && onDiscTab() && !g.showUpdate);
        images::pump();
        render();
        uint32_t spent = SDL_GetTicks() - frameStart;
        if (spent < 16) SDL_Delay(16 - spent);
    }
    SDL_Quit();
    return 0;
}
