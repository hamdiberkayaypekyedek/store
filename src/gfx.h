#pragma once
#include <SDL2/SDL.h>

#include <cstdint>
#include <string>

struct Color {
    uint8_t r, g, b, a;
};
inline Color withAlpha(Color c, uint8_t a) {
    c.a = a;
    return c;
}

namespace gfx {

enum class Align { Left, Center, Right };

bool init(SDL_Renderer* r, const char* regularFont, const char* boldFont);

void fillRect(int x, int y, int w, int h, Color c);
void fillRoundRect(int x, int y, int w, int h, int radius, Color c);
void strokeRect(int x, int y, int w, int h, int thickness, Color c);
void fillCircle(int cx, int cy, int radius, Color c);
void ring(int cx, int cy, int radius, int thickness, Color c);
void line(int x1, int y1, int x2, int y2, int thickness, Color c);

// y = metnin ust kenari. Donus: cizilen genislik.
int text(const std::string& s, int x, int y, int px, Color c, bool bold = false, int maxW = 0,
         Align align = Align::Left);
int textWidth(const std::string& s, int px, bool bold = false);
int wrapText(const std::string& s, int x, int y, int px, int maxW, int lineH, int maxLines, Color c);

SDL_Texture* textureFromRGBA(const unsigned char* px, int w, int h);
SDL_Texture* verticalGradient(int h, Color top, Color bottom);
void drawImage(SDL_Texture* t, int x, int y, int w, int h, uint8_t alpha = 255);
// Gorseli kirparak alani tamamen doldurur (object-fit: cover)
void drawImageCover(SDL_Texture* t, int texW, int texH, int x, int y, int w, int h, uint8_t alpha = 255);

}  // namespace gfx

// Arka planda indirilip cozulen kapak / arka plan gorselleri
namespace images {
void start();
SDL_Texture* get(const std::string& url, int* w = nullptr, int* h = nullptr);
void pump();  // her karede ana is parcaciginda cagrilir
void clear();
}  // namespace images
