#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Uygulamadan baslatilan indirmelerin listesi. RPI (Remote Package Installer)
// uzerinden gidenlerin ilerlemesi yerel API'den (port 12800) okunabilir; BGFT
// (sistem servisi) uzerinden gidenlerin ilerlemesi PS4 Bildirimler'de gorunur,
// burada "sistem kuyrugunda" diye listelenir.
namespace downloads {

enum State { Queued, Downloading, Done, Error };

struct Entry {
    std::string title;
    std::string cover;     // kapak adresi (images::get ile cizilir)
    uint64_t size = 0;
    int method = 0;        // 0 BGFT, 1 RPI
    int taskId = -1;
    int percent = -1;      // -1 = bilinmiyor
    State state = Queued;
    std::string note;
};

void add(const std::string& title, const std::string& cover, uint64_t size, int method, int taskId);
void poll();                 // ~1 sn'de bir RPI gorevlerinin ilerlemesini gunceller
std::vector<Entry> list();   // en yeni ustte
int activeCount();

}  // namespace downloads
