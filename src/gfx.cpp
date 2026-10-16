#include "gfx.h"

#include <pthread.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <unordered_map>
#include <utility>
#include <vector>

#include "http.h"
#include "log.h"
#include "stb_image.h"
#include "stb_truetype.h"

namespace gfx {
namespace {

SDL_Renderer* g_r = nullptr;

struct Font {
    std::vector<unsigned char> data;
    stbtt_fontinfo info;
    bool ok = false;
};
Font g_fonts[2];

struct TextTex {
    SDL_Texture* tex = nullptr;
    int w = 0, h = 0;
};
std::unordered_map<std::string, TextTex> g_textCache;
std::unordered_map<std::string, std::string> g_ellipsisCache;
std::unordered_map<std::string, std::vector<std::string>> g_wrapCache;

bool readFile(const char* path, std::vector<unsigned char>& out) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return false; }
    out.resize((size_t)n);
    size_t rd = fread(out.data(), 1, (size_t)n, f);
    fclose(f);
    return rd == (size_t)n;
}

bool loadFont(Font& f, const char* path) {
    if (!readFile(path, f.data)) return false;
    int off = stbtt_GetFontOffsetForIndex(f.data.data(), 0);
    if (off < 0) return false;
    f.ok = stbtt_InitFont(&f.info, f.data.data(), off) != 0;
    return f.ok;
}

Font& font(bool bold) { return (bold && g_fonts[1].ok) ? g_fonts[1] : g_fonts[0]; }

uint32_t utf8Next(const std::string& s, size_t& i) {
    unsigned char c = (unsigned char)s[i++];
    if (c < 0x80) return c;
    int n = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : (c >= 0xC0) ? 1 : 0;
    uint32_t cp = c & (0x3F >> n);
    for (int k = 0; k < n && i < s.size(); k++) cp = (cp << 6) | ((unsigned char)s[i++] & 0x3F);
    return cp;
}

int measureRaw(Font& f, const std::string& s, int px) {
    float scale = stbtt_ScaleForPixelHeight(&f.info, (float)px);
    float x = 0;
    uint32_t prev = 0;
    size_t i = 0;
    while (i < s.size()) {
        uint32_t cp = utf8Next(s, i);
        if (prev) x += scale * stbtt_GetCodepointKernAdvance(&f.info, (int)prev, (int)cp);
        int adv, lsb;
        stbtt_GetCodepointHMetrics(&f.info, (int)cp, &adv, &lsb);
        x += adv * scale;
        prev = cp;
    }
    return (int)std::ceil(x);
}

TextTex renderText(Font& f, const std::string& s, int px) {
    TextTex out;
    float scale = stbtt_ScaleForPixelHeight(&f.info, (float)px);
    int asc, desc, gap;
    stbtt_GetFontVMetrics(&f.info, &asc, &desc, &gap);
    int baseline = (int)std::lround(asc * scale);
    int h = (int)std::ceil((asc - desc) * scale) + 2;
    int w = measureRaw(f, s, px) + 4;
    if (w <= 4 || h <= 0) return out;

    std::vector<unsigned char> bmp((size_t)w * h, 0);
    std::vector<unsigned char> glyph;
    float x = 1;
    uint32_t prev = 0;
    size_t i = 0;
    while (i < s.size()) {
        uint32_t cp = utf8Next(s, i);
        if (prev) x += scale * stbtt_GetCodepointKernAdvance(&f.info, (int)prev, (int)cp);
        float shift = x - std::floor(x);
        int x0, y0, x1, y1;
        stbtt_GetCodepointBitmapBoxSubpixel(&f.info, (int)cp, scale, scale, shift, 0, &x0, &y0, &x1, &y1);
        int gw = x1 - x0, gh = y1 - y0;
        if (gw > 0 && gh > 0) {
            glyph.assign((size_t)gw * gh, 0);
            stbtt_MakeCodepointBitmapSubpixel(&f.info, glyph.data(), gw, gh, gw, scale, scale, shift, 0, (int)cp);
            int dx = (int)std::floor(x) + x0, dy = baseline + y0;
            for (int yy = 0; yy < gh; yy++) {
                int ty = dy + yy;
                if (ty < 0 || ty >= h) continue;
                for (int xx = 0; xx < gw; xx++) {
                    int tx = dx + xx;
                    if (tx < 0 || tx >= w) continue;
                    unsigned char& d = bmp[(size_t)ty * w + tx];
                    d = std::max(d, glyph[(size_t)yy * gw + xx]);
                }
            }
        }
        int adv, lsb;
        stbtt_GetCodepointHMetrics(&f.info, (int)cp, &adv, &lsb);
        x += adv * scale;
        prev = cp;
    }

    std::vector<uint32_t> argb((size_t)w * h);
    for (size_t k = 0; k < argb.size(); k++) argb[k] = ((uint32_t)bmp[k] << 24) | 0x00FFFFFF;
    out.tex = SDL_CreateTexture(g_r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
    if (!out.tex) return out;
    SDL_UpdateTexture(out.tex, nullptr, argb.data(), w * 4);
    SDL_SetTextureBlendMode(out.tex, SDL_BLENDMODE_BLEND);
    out.w = w;
    out.h = h;
    return out;
}

void clearTextCache() {
    for (auto& kv : g_textCache)
        if (kv.second.tex) SDL_DestroyTexture(kv.second.tex);
    g_textCache.clear();
}

std::string ellipsize(Font& f, bool bold, const std::string& s, int px, int maxW) {
    std::string key = std::to_string(bold) + "|" + std::to_string(px) + "|" + std::to_string(maxW) + "|" + s;
    auto it = g_ellipsisCache.find(key);
    if (it != g_ellipsisCache.end()) return it->second;
    if (g_ellipsisCache.size() > 2000) g_ellipsisCache.clear();

    std::string result = s;
    if (measureRaw(f, s, px) > maxW) {
        const std::string dots = "\xE2\x80\xA6";
        std::vector<size_t> cuts;
        size_t i = 0;
        while (i < s.size()) {
            utf8Next(s, i);
            cuts.push_back(i);
        }
        result = dots;
        for (int k = (int)cuts.size() - 1; k >= 0; --k) {
            std::string t = s.substr(0, cuts[k]);
            while (!t.empty() && t.back() == ' ') t.pop_back();
            t += dots;
            if (measureRaw(f, t, px) <= maxW) { result = t; break; }
        }
    }
    g_ellipsisCache.emplace(key, result);
    return result;
}

void setColor(Color c) {
    SDL_SetRenderDrawBlendMode(g_r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(g_r, c.r, c.g, c.b, c.a);
}

void hspan(int x1, int x2, int y) {
    if (x2 < x1) return;
    SDL_Rect r{x1, y, x2 - x1 + 1, 1};
    SDL_RenderFillRect(g_r, &r);
}

}  // namespace

bool init(SDL_Renderer* r, const char* regularFont, const char* boldFont) {
    g_r = r;
    bool ok = loadFont(g_fonts[0], regularFont);
    loadFont(g_fonts[1], boldFont);
    applog("font: normal=%d kalin=%d", (int)g_fonts[0].ok, (int)g_fonts[1].ok);
    return ok;
}

void fillRect(int x, int y, int w, int h, Color c) {
    setColor(c);
    SDL_Rect r{x, y, w, h};
    SDL_RenderFillRect(g_r, &r);
}

void fillRoundRect(int x, int y, int w, int h, int rad, Color c) {
    rad = std::min(rad, std::min(w, h) / 2);
    setColor(c);
    SDL_Rect mid{x, y + rad, w, h - 2 * rad};
    SDL_RenderFillRect(g_r, &mid);
    for (int i = 0; i < rad; i++) {
        float dy = rad - i - 0.5f;
        int inset = (int)std::lround(rad - std::sqrt((float)rad * rad - dy * dy));
        hspan(x + inset, x + w - 1 - inset, y + i);
        hspan(x + inset, x + w - 1 - inset, y + h - 1 - i);
    }
}

void strokeRect(int x, int y, int w, int h, int t, Color c) {
    fillRect(x, y, w, t, c);
    fillRect(x, y + h - t, w, t, c);
    fillRect(x, y + t, t, h - 2 * t, c);
    fillRect(x + w - t, y + t, t, h - 2 * t, c);
}

void fillCircle(int cx, int cy, int r, Color c) {
    setColor(c);
    for (int dy = -r; dy <= r; dy++) {
        int dx = (int)std::lround(std::sqrt((float)(r * r - dy * dy)));
        hspan(cx - dx, cx + dx, cy + dy);
    }
}

void ring(int cx, int cy, int r, int t, Color c) {
    setColor(c);
    int ri = r - t;
    for (int dy = -r; dy <= r; dy++) {
        int xo = (int)std::lround(std::sqrt((float)(r * r - dy * dy)));
        if (std::abs(dy) < ri) {
            int xi = (int)std::lround(std::sqrt((float)(ri * ri - dy * dy)));
            hspan(cx - xo, cx - xi, cy + dy);
            hspan(cx + xi, cx + xo, cy + dy);
        } else {
            hspan(cx - xo, cx + xo, cy + dy);
        }
    }
}

void line(int x1, int y1, int x2, int y2, int t, Color c) {
    setColor(c);
    for (int ox = -t / 2; ox <= t / 2; ox++)
        for (int oy = -t / 2; oy <= t / 2; oy++) SDL_RenderDrawLine(g_r, x1 + ox, y1 + oy, x2 + ox, y2 + oy);
}

int textWidth(const std::string& s, int px, bool bold) {
    if (s.empty() || !g_fonts[0].ok) return 0;
    return measureRaw(font(bold), s, px);
}

int text(const std::string& s0, int x, int y, int px, Color c, bool bold, int maxW, Align align) {
    if (s0.empty() || !g_fonts[0].ok) return 0;
    Font& f = font(bold);
    const std::string s = maxW > 0 ? ellipsize(f, bold, s0, px, maxW) : s0;
    std::string key = std::string(bold ? "B" : "R") + std::to_string(px) + "|" + s;
    auto it = g_textCache.find(key);
    if (it == g_textCache.end()) {
        if (g_textCache.size() > 600) clearTextCache();
        it = g_textCache.emplace(key, renderText(f, s, px)).first;
    }
    TextTex& t = it->second;
    if (!t.tex) return 0;
    int dx = x;
    if (align == Align::Center) dx = x - t.w / 2;
    else if (align == Align::Right) dx = x - t.w;
    SDL_SetTextureColorMod(t.tex, c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(t.tex, c.a);
    SDL_Rect d{dx, y, t.w, t.h};
    SDL_RenderCopy(g_r, t.tex, nullptr, &d);
    return t.w;
}

int wrapText(const std::string& s, int x, int y, int px, int maxW, int lineH, int maxLines, Color c) {
    if (!g_fonts[0].ok) return 0;
    std::string key = std::to_string(px) + "|" + std::to_string(maxW) + "|" + s;
    auto it = g_wrapCache.find(key);
    if (it == g_wrapCache.end()) {
        if (g_wrapCache.size() > 200) g_wrapCache.clear();
        Font& f = font(false);
        std::vector<std::string> lines;
        size_t start = 0;
        for (;;) {
            size_t nl = s.find('\n', start);
            std::string para = s.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
            std::string cur;
            size_t p = 0;
            for (;;) {
                size_t sp = para.find(' ', p);
                std::string word = para.substr(p, sp == std::string::npos ? std::string::npos : sp - p);
                std::string cand = cur.empty() ? word : cur + " " + word;
                if (!cur.empty() && measureRaw(f, cand, px) > maxW) {
                    lines.push_back(cur);
                    cur = word;
                } else {
                    cur = cand;
                }
                if (sp == std::string::npos) break;
                p = sp + 1;
            }
            lines.push_back(cur);
            if (nl == std::string::npos) break;
            start = nl + 1;
        }
        it = g_wrapCache.emplace(key, std::move(lines)).first;
    }
    const auto& lines = it->second;
    int n = std::min((int)lines.size(), maxLines);
    for (int k = 0; k < n; k++) {
        std::string l = lines[k];
        if (k == n - 1 && (int)lines.size() > n) l += " \xE2\x80\xA6";
        text(l, x, y + k * lineH, px, c, false, maxW);
    }
    return n * lineH;
}

SDL_Texture* textureFromRGBA(const unsigned char* px, int w, int h) {
    SDL_Texture* t = SDL_CreateTexture(g_r, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, w, h);
    if (!t) {
        applog("doku olusturulamadi %dx%d: %s", w, h, SDL_GetError());
        return nullptr;
    }
    SDL_UpdateTexture(t, nullptr, px, w * 4);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    return t;
}

SDL_Texture* verticalGradient(int h, Color top, Color bottom) {
    std::vector<unsigned char> px((size_t)h * 4);
    for (int y = 0; y < h; y++) {
        float f = h > 1 ? (float)y / (h - 1) : 0.f;
        px[y * 4 + 0] = (unsigned char)(top.r + (bottom.r - top.r) * f);
        px[y * 4 + 1] = (unsigned char)(top.g + (bottom.g - top.g) * f);
        px[y * 4 + 2] = (unsigned char)(top.b + (bottom.b - top.b) * f);
        px[y * 4 + 3] = (unsigned char)(top.a + (bottom.a - top.a) * f);
    }
    return textureFromRGBA(px.data(), 1, h);
}

void drawImage(SDL_Texture* t, int x, int y, int w, int h, uint8_t alpha) {
    if (!t) return;
    SDL_SetTextureAlphaMod(t, alpha);
    SDL_Rect d{x, y, w, h};
    SDL_RenderCopy(g_r, t, nullptr, &d);
}

void drawImageCover(SDL_Texture* t, int tw, int th, int x, int y, int w, int h, uint8_t alpha) {
    if (!t || tw <= 0 || th <= 0) return;
    float target = (float)w / h, img = (float)tw / th;
    SDL_Rect src{0, 0, tw, th};
    if (img > target) {
        src.w = (int)(th * target);
        src.x = (tw - src.w) / 2;
    } else {
        src.h = (int)(tw / target);
        src.y = (th - src.h) / 2;
    }
    SDL_SetTextureAlphaMod(t, alpha);
    SDL_Rect d{x, y, w, h};
    SDL_RenderCopy(g_r, t, &src, &d);
}

}  // namespace gfx

namespace images {
namespace {

struct Entry {
    int state = 0;  // 0 yok, 1 kuyrukta, 2 hazir, 3 hata
    SDL_Texture* tex = nullptr;
    int w = 0, h = 0;
    uint64_t lastUse = 0;
    size_t bytes = 0;
};

struct Done {
    std::string url;
    unsigned char* px;
    int w, h;
};

std::unordered_map<std::string, Entry> g_map;
pthread_mutex_t g_mx;
pthread_cond_t g_cv;
std::deque<std::string> g_jobs;
std::vector<Done> g_done;
pthread_t g_thread;
uint64_t g_frame = 0;
size_t g_total = 0;
const size_t kBudget = 300u * 1024 * 1024;

void* worker(void*) {
    for (;;) {
        pthread_mutex_lock(&g_mx);
        while (g_jobs.empty()) pthread_cond_wait(&g_cv, &g_mx);
        std::string url = g_jobs.back();  // en son istenen once (ekranda olan)
        g_jobs.pop_back();
        pthread_mutex_unlock(&g_mx);

        http::Result r = http::request("GET", url);
        unsigned char* px = nullptr;
        int w = 0, h = 0, n = 0;
        if (r.error.empty() && r.status == 200 && !r.body.empty())
            px = stbi_load_from_memory((const stbi_uc*)r.body.data(), (int)r.body.size(), &w, &h, &n, 4);
        if (!px) {
            const char* why = !r.error.empty() ? r.error.c_str()
                              : r.status != 200 ? "HTTP hatasi"
                                                : stbi_failure_reason();
            applog("gorsel yuklenemedi: %s (%s, durum %d)", url.c_str(), why ? why : "?", r.status);
        }

        pthread_mutex_lock(&g_mx);
        g_done.push_back({url, px, w, h});
        pthread_mutex_unlock(&g_mx);
    }
    return nullptr;
}

}  // namespace

void start() {
    pthread_mutex_init(&g_mx, nullptr);
    pthread_cond_init(&g_cv, nullptr);
    pthread_create(&g_thread, nullptr, worker, nullptr);
}

SDL_Texture* get(const std::string& url, int* w, int* h) {
    if (url.empty()) return nullptr;
    Entry& e = g_map[url];
    e.lastUse = g_frame;
    if (e.state == 0) {
        e.state = 1;
        pthread_mutex_lock(&g_mx);
        g_jobs.push_back(url);
        pthread_cond_signal(&g_cv);
        pthread_mutex_unlock(&g_mx);
    }
    if (w) *w = e.w;
    if (h) *h = e.h;
    return e.tex;
}

void pump() {
    g_frame++;
    std::vector<Done> done;
    pthread_mutex_lock(&g_mx);
    done.swap(g_done);
    pthread_mutex_unlock(&g_mx);

    for (auto& d : done) {
        auto it = g_map.find(d.url);
        if (it == g_map.end() || it->second.state != 1) {
            if (d.px) stbi_image_free(d.px);
            continue;
        }
        Entry& e = it->second;
        if (d.px) {
            e.tex = gfx::textureFromRGBA(d.px, d.w, d.h);
            stbi_image_free(d.px);
            if (e.tex) {
                e.w = d.w;
                e.h = d.h;
                e.bytes = (size_t)d.w * d.h * 4;
                g_total += e.bytes;
                e.state = 2;
                continue;
            }
        }
        e.state = 3;
    }

    if (g_total > kBudget) {
        std::vector<std::pair<uint64_t, std::string>> old;
        for (auto& kv : g_map)
            if (kv.second.tex && kv.second.lastUse + 2 < g_frame) old.push_back({kv.second.lastUse, kv.first});
        std::sort(old.begin(), old.end());
        for (auto& o : old) {
            if (g_total <= kBudget * 8 / 10) break;
            Entry& e = g_map[o.second];
            SDL_DestroyTexture(e.tex);
            g_total -= e.bytes;
            e = Entry();
        }
    }
}

void clear() {
    pthread_mutex_lock(&g_mx);
    g_jobs.clear();
    pthread_mutex_unlock(&g_mx);
    for (auto& kv : g_map)
        if (kv.second.tex) SDL_DestroyTexture(kv.second.tex);
    g_map.clear();
    g_total = 0;
}

}  // namespace images
