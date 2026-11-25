// Leitura/escrita de PNG RGBA8 (libpng).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

bool png_write_rgba(const std::string& utf8_path, const uint8_t* rgba, int w, int h);
bool png_read_rgba(const std::string& utf8_path, std::vector<uint8_t>& out, int& w, int& h);
