#pragma once
// Kucuk, bagimsiz JSON okuyucu (katalog ve RPI yanitlari icin yeterli).
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

struct JVal {
    enum Type { Null, Bool, Num, Str, Arr, Obj };
    Type t = Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<JVal> a;
    std::vector<std::pair<std::string, JVal>> o;

    const JVal& operator[](const char* key) const {
        static const JVal empty;
        if (t != Obj) return empty;
        for (auto& kv : o)
            if (kv.first == key) return kv.second;
        return empty;
    }
    const JVal& at(size_t i) const {
        static const JVal empty;
        return (t == Arr && i < a.size()) ? a[i] : empty;
    }
    size_t size() const { return t == Arr ? a.size() : t == Obj ? o.size() : 0; }
    std::string str(const std::string& def = "") const {
        if (t == Str) return s;
        if (t == Num) {
            char buf[64];
            if (n == std::floor(n) && std::fabs(n) < 1e15) snprintf(buf, sizeof buf, "%.0f", n);
            else snprintf(buf, sizeof buf, "%g", n);
            return buf;
        }
        return def;
    }
    double num(double def = 0) const {
        if (t == Num) return n;
        if (t == Str && !s.empty()) return strtod(s.c_str(), nullptr);
        return def;
    }
};

class JParser {
public:
    explicit JParser(const std::string& s) : in(s) {}
    const std::string& in;
    size_t p = 0;
    std::string err;

    bool value(JVal& v, int depth) {
        if (depth > 64) return fail("cok derin yapi");
        ws();
        if (p >= in.size()) return fail("beklenmeyen son");
        char c = in[p];
        if (c == '{') {
            v.t = JVal::Obj;
            p++;
            ws();
            if (p < in.size() && in[p] == '}') { p++; return true; }
            for (;;) {
                ws();
                if (p >= in.size() || in[p] != '"') return fail("anahtar bekleniyor");
                std::string key;
                if (!str(key)) return false;
                ws();
                if (p >= in.size() || in[p] != ':') return fail("':' bekleniyor");
                p++;
                JVal child;
                if (!value(child, depth + 1)) return false;
                v.o.emplace_back(std::move(key), std::move(child));
                ws();
                if (p < in.size() && in[p] == ',') { p++; continue; }
                if (p < in.size() && in[p] == '}') { p++; return true; }
                return fail("',' veya '}' bekleniyor");
            }
        }
        if (c == '[') {
            v.t = JVal::Arr;
            p++;
            ws();
            if (p < in.size() && in[p] == ']') { p++; return true; }
            for (;;) {
                JVal child;
                if (!value(child, depth + 1)) return false;
                v.a.push_back(std::move(child));
                ws();
                if (p < in.size() && in[p] == ',') { p++; continue; }
                if (p < in.size() && in[p] == ']') { p++; return true; }
                return fail("',' veya ']' bekleniyor");
            }
        }
        if (c == '"') { v.t = JVal::Str; return str(v.s); }
        if (in.compare(p, 4, "true") == 0) { v.t = JVal::Bool; v.b = true; p += 4; return true; }
        if (in.compare(p, 5, "false") == 0) { v.t = JVal::Bool; v.b = false; p += 5; return true; }
        if (in.compare(p, 4, "null") == 0) { v.t = JVal::Null; p += 4; return true; }
        if (c == '-' || (c >= '0' && c <= '9')) {
            const char* start = in.c_str() + p;
            char* end = nullptr;
            v.n = strtod(start, &end);
            if (end == start) return fail("gecersiz sayi");
            p += (size_t)(end - start);
            v.t = JVal::Num;
            return true;
        }
        return fail("beklenmeyen karakter");
    }

private:
    void ws() {
        while (p < in.size() && (in[p] == ' ' || in[p] == '\n' || in[p] == '\r' || in[p] == '\t')) p++;
    }
    bool fail(const char* m) {
        if (err.empty()) err = std::string(m) + " (konum " + std::to_string(p) + ")";
        return false;
    }
    bool hex4(uint32_t& v) {
        if (p + 4 > in.size()) return false;
        v = 0;
        for (int i = 0; i < 4; i++) {
            char h = in[p++];
            v <<= 4;
            if (h >= '0' && h <= '9') v |= (uint32_t)(h - '0');
            else if (h >= 'a' && h <= 'f') v |= (uint32_t)(h - 'a' + 10);
            else if (h >= 'A' && h <= 'F') v |= (uint32_t)(h - 'A' + 10);
            else return false;
        }
        return true;
    }
    static void putUtf8(std::string& o, uint32_t cp) {
        if (cp < 0x80) o += (char)cp;
        else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) {
            o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F));
        } else {
            o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 0x3F));
            o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F));
        }
    }
    bool str(std::string& out) {
        p++;  // acilis tirnagi
        while (p < in.size()) {
            char c = in[p++];
            if (c == '"') return true;
            if (c != '\\') { out += c; continue; }
            if (p >= in.size()) break;
            char e = in[p++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    uint32_t cp;
                    if (!hex4(cp)) return fail("gecersiz \\u");
                    if (cp >= 0xD800 && cp < 0xDC00 && p + 1 < in.size() && in[p] == '\\' && in[p + 1] == 'u') {
                        p += 2;
                        uint32_t lo;
                        if (!hex4(lo)) return fail("gecersiz \\u");
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    putUtf8(out, cp);
                    break;
                }
                default: return fail("gecersiz kacis");
            }
        }
        return fail("bitmemis metin");
    }
};

inline bool jsonParse(const std::string& in, JVal& out, std::string& err) {
    JParser ps(in);
    if (in.size() >= 3 && (unsigned char)in[0] == 0xEF && (unsigned char)in[1] == 0xBB && (unsigned char)in[2] == 0xBF)
        ps.p = 3;  // UTF-8 BOM
    if (!ps.value(out, 0)) { err = ps.err; return false; }
    return true;
}

inline std::string jsonEscape(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        if (c == '"') o += "\\\"";
        else if (c == '\\') o += "\\\\";
        else if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; }
        else o += (char)c;
    }
    return o;
}
