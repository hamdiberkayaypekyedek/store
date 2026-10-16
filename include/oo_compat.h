// OpenOrbis libc++ <atomic>/<thread> basliklari nanosleep'i cagiriyor ama
// toolchain libc'si bunu gorunur bicimde bildirmiyor. Bu baslik, standart
// basliklardan ONCE (-include ile) dahil edilip nanosleep'i bildirir.
// Fonksiyon bizim kodumuzda cagrilmadigi icin link asamasinda bir sembol
// gerektirmez; yalnizca derleyicinin basligi ayristirmasini saglar.
#pragma once
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif
int nanosleep(const struct timespec *req, struct timespec *rem);
#ifdef __cplusplus
}
#endif
