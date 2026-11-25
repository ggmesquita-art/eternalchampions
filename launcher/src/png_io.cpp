#include "png_io.h"
#include <png.h>
#include <cstdio>
#include <windows.h>

static FILE* open_utf8(const std::string& p, const wchar_t* mode) {
    int n = MultiByteToWideChar(CP_UTF8, 0, p.c_str(), -1, nullptr, 0);
    std::wstring w(n ? n - 1 : 0, L'\0');
    if (n) MultiByteToWideChar(CP_UTF8, 0, p.c_str(), -1, &w[0], n);
    FILE* f = nullptr;
    _wfopen_s(&f, w.c_str(), mode);
    return f;
}

bool png_write_rgba(const std::string& path, const uint8_t* rgba, int w, int h) {
    FILE* f = open_utf8(path, L"wb");
    if (!f) return false;
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    png_infop info = png ? png_create_info_struct(png) : nullptr;
    if (!png || !info) { fclose(f); return false; }
    if (setjmp(png_jmpbuf(png))) { png_destroy_write_struct(&png, &info); fclose(f); return false; }
    png_init_io(png, f);
    png_set_IHDR(png, info, w, h, 8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    for (int y = 0; y < h; ++y) png_write_row(png, const_cast<png_bytep>(rgba + (size_t)y * w * 4));
    png_write_end(png, nullptr);
    png_destroy_write_struct(&png, &info);
    fclose(f);
    return true;
}

bool png_read_rgba(const std::string& path, std::vector<uint8_t>& out, int& w, int& h) {
    FILE* f = open_utf8(path, L"rb");
    if (!f) return false;
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    png_infop info = png ? png_create_info_struct(png) : nullptr;
    if (!png || !info) { fclose(f); return false; }
    if (setjmp(png_jmpbuf(png))) { png_destroy_read_struct(&png, &info, nullptr); fclose(f); return false; }
    png_init_io(png, f);
    png_read_info(png, info);
    w = (int)png_get_image_width(png, info);
    h = (int)png_get_image_height(png, info);
    png_set_expand(png);
    png_set_strip_16(png);
    png_set_gray_to_rgb(png);
    png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    png_read_update_info(png, info);
    out.assign((size_t)w * h * 4, 0);
    std::vector<png_bytep> rows(h);
    for (int y = 0; y < h; ++y) rows[y] = out.data() + (size_t)y * w * 4;
    png_read_image(png, rows.data());
    png_destroy_read_struct(&png, &info, nullptr);
    fclose(f);
    return true;
}
