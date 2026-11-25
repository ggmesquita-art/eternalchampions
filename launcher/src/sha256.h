#pragma once
#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <string>

// SHA-256 proprio (FIPS 180-4). Hex minusculo.
std::string sha256_hex(const uint8_t* data, size_t n);
// Retorna "" se nao conseguir ler o arquivo.
std::string sha256_file_hex(const std::filesystem::path& p);

constexpr const char* EC_US_ROM_SHA256 = "6d8bdaf8c994d9f718c8f991900c58cdf1af84e030524b57f6dda263cc4ea247";
