#include "catalog.h"

#include <cstdio>

#include "http.h"
#include "json.h"

namespace catalog {
namespace {

bool hasScheme(const std::string& s) {
    return s.compare(0, 7, "http://") == 0 || s.compare(0, 8, "https://") == 0;
}

void parseExtras(const JVal& arr, const std::string& base, const char* defType, std::vector<Extra>& out) {
    for (size_t i = 0; i < arr.size(); i++) {
        const JVal& e = arr.at(i);
        Extra x;
        x.pkg = resolve(base, e["pkg"].str());
        if (x.pkg.empty()) continue;
        x.title = e["title"].str();
        x.packageType = e["package_type"].str(defType);
        double sz = e["size"].num(0);
        x.size = sz > 0 ? (uint64_t)sz : 0;
        out.push_back(std::move(x));
    }
}

Item parseItem(const JVal& j, const std::string& base) {
    Item it;
    it.id = j["id"].str();
    it.title = j["title"].str(it.id.empty() ? "Isimsiz" : it.id);
    it.publisher = j["publisher"].str();
    it.version = j["version"].str();
    it.description = j["description"].str();
    it.cover = resolve(base, j["cover"].str());
    it.background = resolve(base, j["background"].str());
    it.pkg = resolve(base, j["pkg"].str());
    it.packageType = j["package_type"].str();
    double sz = j["size"].num(0);
    it.size = sz > 0 ? (uint64_t)sz : 0;
    parseExtras(j["updates"], base, "PS4DP", it.updates);
    parseExtras(j["dlcs"], base, "PS4AC", it.dlcs);
    return it;
}

void addItems(const JVal& arr, const std::string& base, Category& c) {
    for (size_t i = 0; i < arr.size(); i++) {
        Item it = parseItem(arr.at(i), base);
        if (!it.pkg.empty()) c.items.push_back(std::move(it));
    }
}

}  // namespace

std::string resolve(const std::string& base, const std::string& ref) {
    if (ref.empty() || hasScheme(ref)) return ref;
    if (hasScheme(base)) {
        size_t schemeEnd = base.find("://") + 3;
        size_t pathStart = base.find('/', schemeEnd);
        std::string origin = pathStart == std::string::npos ? base : base.substr(0, pathStart);
        if (ref[0] == '/') return origin + ref;
        std::string path = pathStart == std::string::npos ? "/" : base.substr(pathStart);
        size_t q = path.find_first_of("?#");
        if (q != std::string::npos) path = path.substr(0, q);
        return origin + path.substr(0, path.rfind('/') + 1) + ref;
    }
    if (ref[0] == '/') return ref;
    size_t slash = base.rfind('/');
    return (slash == std::string::npos ? std::string() : base.substr(0, slash + 1)) + ref;
}

bool load(const std::string& source, Catalog& out, std::string& err) {
    http::Result r = http::request("GET", source);
    if (!r.error.empty()) { err = r.error; return false; }
    if (r.status != 200) { err = "Sunucu HTTP " + std::to_string(r.status) + " dondu: " + source; return false; }

    JVal root;
    std::string jerr;
    if (!jsonParse(r.body, root, jerr)) { err = "Katalog JSON hatasi: " + jerr; return false; }

    out = Catalog();
    out.name = root["store_name"].str("PKG Ma\xC4\x9F" "aza");

    const JVal& cats = root["categories"];
    for (size_t i = 0; i < cats.size(); i++) {
        Category c;
        c.name = cats.at(i)["name"].str("Di\xC4\x9F" "er");
        addItems(cats.at(i)["items"], source, c);
        if (!c.items.empty()) out.categories.push_back(std::move(c));
    }
    if (root["items"].size()) {
        Category c;
        c.name = "T\xC3\xBCm\xC3\xBC";
        addItems(root["items"], source, c);
        if (!c.items.empty()) out.categories.push_back(std::move(c));
    }
    if (out.categories.empty()) { err = "Katalogda pkg adresi olan icerik yok."; return false; }
    return true;
}

std::string formatSize(uint64_t b) {
    if (b == 0) return "";
    char buf[32];
    double v = (double)b;
    if (v >= 1024.0 * 1024 * 1024) snprintf(buf, sizeof buf, "%.2f GB", v / (1024.0 * 1024 * 1024));
    else if (v >= 1024.0 * 1024) snprintf(buf, sizeof buf, "%.1f MB", v / (1024.0 * 1024));
    else snprintf(buf, sizeof buf, "%.0f KB", v / 1024.0);
    return buf;
}

std::string hostOf(const std::string& url) {
    size_t s = url.find("://");
    if (s == std::string::npos) return url.empty() || url[0] != '/' ? url : "Yerel dosya";
    s += 3;
    size_t e = url.find('/', s);
    return url.substr(s, e == std::string::npos ? std::string::npos : e - s);
}

}  // namespace catalog
