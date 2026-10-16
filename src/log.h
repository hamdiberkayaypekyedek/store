#pragma once
#include <cstdarg>
#include <cstdio>

// Hata ayiklama icin: /data/pkgstore/log.txt (FTP ile okunabilir)
inline void applog(const char* fmt, ...) {
    FILE* f = fopen("/data/pkgstore/log.txt", "a");
    if (!f) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}
