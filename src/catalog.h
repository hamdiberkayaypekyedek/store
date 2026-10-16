#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace catalog {

// Ana pkg'nin yaninda indirilebilen ek paket (guncelleme ya da DLC).
struct Extra {
    std::string title, pkg, packageType;  // pkg adresi katalog konumuna gore cozulmus
    uint64_t size = 0;
};

struct Item {
    std::string id, title, publisher, version, description;
    std::string cover, background, pkg, packageType;  // adresler katalog konumuna gore cozulmus halde
    uint64_t size = 0;
    std::vector<Extra> updates;  // guncelleme pkg'leri
    std::vector<Extra> dlcs;     // DLC pkg'leri
};

struct Category {
    std::string name;
    std::vector<Item> items;
};

struct Catalog {
    std::string name;
    std::vector<Category> categories;
};

bool load(const std::string& source, Catalog& out, std::string& err);
std::string resolve(const std::string& base, const std::string& ref);
std::string formatSize(uint64_t bytes);
std::string hostOf(const std::string& url);

}  // namespace catalog
