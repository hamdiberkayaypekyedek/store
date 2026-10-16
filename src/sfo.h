#pragma once
// param.sfo okuyucu: anahtar -> deger (tamsayilar ondalik metin olarak)
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

inline std::map<std::string, std::string> parseSfo(const std::vector<uint8_t>& b) {
    std::map<std::string, std::string> out;
    if (b.size() < 20 || memcmp(b.data(), "\0PSF", 4) != 0) return out;
    auto le16 = [&](size_t o) { return (uint32_t)b[o] | ((uint32_t)b[o + 1] << 8); };
    auto le32 = [&](size_t o) { return le16(o) | (le16(o + 2) << 16); };

    uint32_t keyTab = le32(8), dataTab = le32(12), count = le32(16);
    for (uint32_t i = 0; i < count; i++) {
        size_t e = 20 + (size_t)i * 16;
        if (e + 16 > b.size()) break;
        uint32_t ko = le16(e), fmt = le16(e + 2), len = le32(e + 4), doff = le32(e + 12);
        size_t ks = (size_t)keyTab + ko;
        if (ks >= b.size()) break;
        size_t ke = ks;
        while (ke < b.size() && b[ke]) ke++;
        std::string key((const char*)&b[ks], ke - ks);
        size_t ds = (size_t)dataTab + doff;
        if (ds + len > b.size()) continue;
        if (fmt == 0x0404 && len >= 4) {
            out[key] = std::to_string(le32(ds));
        } else {
            std::string v((const char*)&b[ds], len);
            while (!v.empty() && v.back() == '\0') v.pop_back();
            out[key] = v;
        }
    }
    return out;
}
