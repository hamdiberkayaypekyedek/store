#include "downloads.h"

#include <SDL2/SDL.h>

#include <algorithm>

#include "http.h"
#include "json.h"
#include "log.h"

namespace downloads {
namespace {

std::vector<Entry> g_list;  // en yeni sona eklenir; list() ters cevirir
uint32_t g_lastPoll = 0;

// RPI ilerleme sorgusu: GET /api/get_task_progress?task_id=N
// Yanit alanlari surumden surume degisebilir; transferred/length varsa yuzde
// hesaplanir, yoksa "progress"/"percent" alani denenir.
void pollRpi(Entry& e) {
    std::string url = "http://127.0.0.1:12800/api/get_task_progress?task_id=" + std::to_string(e.taskId);
    http::Result r = http::request("GET", url);
    if (!r.error.empty()) return;  // RPI kapali olabilir; durumu koru
    JVal j;
    std::string err;
    if (!jsonParse(r.body, j, err)) return;

    double length = j["length"].num(0);
    double transferred = j["transferred"].num(0);
    int error = (int)j["error"].num(0);
    if (error != 0) {
        e.state = Error;
        e.note = "Hata (" + std::to_string(error) + ")";
        return;
    }
    if (length > 0) {
        e.percent = (int)std::min(100.0, transferred * 100.0 / length);
        if (e.size == 0) e.size = (uint64_t)length;
    } else {
        double p = j["progress"].num(-1);
        if (p < 0) p = j["percent"].num(-1);
        if (p >= 0) e.percent = (int)std::min(100.0, p);
    }
    if (e.percent >= 100) {
        e.state = Done;
        e.note = "Tamamland\xC4\xB1";
    } else if (e.percent >= 0) {
        e.state = Downloading;
        e.note.clear();
    }
}

}  // namespace

void add(const std::string& title, const std::string& cover, uint64_t size, int method, int taskId) {
    Entry e;
    e.title = title;
    e.cover = cover;
    e.size = size;
    e.method = method;
    e.taskId = taskId;
    if (method == 1 && taskId >= 0) {
        e.state = Queued;
        e.note = "Ba\xC5\x9Flat\xC4\xB1l\xC4\xB1yor\xE2\x80\xA6";
    } else {
        // BGFT: ilerlemeyi okuyamiyoruz; sistem kuyruguna gitti.
        e.state = Downloading;
        e.percent = -1;
        e.note = "PS4 ana ekran\xC4\xB1nda / Bildirimler'de";
    }
    g_list.push_back(std::move(e));
    applog("indirme eklendi: '%s' yontem=%d task=%d", title.c_str(), method, taskId);
}

void poll() {
    uint32_t now = SDL_GetTicks();
    if (now - g_lastPoll < 1000) return;
    g_lastPoll = now;
    for (auto& e : g_list) {
        if (e.method == 1 && e.taskId >= 0 && e.state != Done && e.state != Error) pollRpi(e);
    }
}

std::vector<Entry> list() {
    std::vector<Entry> out(g_list.rbegin(), g_list.rend());
    return out;
}

int activeCount() {
    int n = 0;
    for (auto& e : g_list)
        if (e.state == Queued || e.state == Downloading) n++;
    return n;
}

}  // namespace downloads
