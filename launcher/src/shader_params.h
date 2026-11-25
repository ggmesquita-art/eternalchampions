// Le os `#pragma parameter` de um preset .glslp (e dos .glsl referenciados, com #include) para o editor de parametros.
#pragma once
#include <filesystem>
#include <map>
#include <string>
#include <vector>

struct ShaderParam {
    std::string name, desc;
    double def = 0, min = 0, max = 1, step = 0.05;   // def ja considera override do proprio .glslp
};

// `preset`: "off" | nome em assets/shaders/presets | caminho de arquivo. Devolve false se nao achar.
bool load_shader_params(const std::filesystem::path& root, const std::string& preset, std::vector<ShaderParam>& out);

// "a=1,b=0.5" <-> mapa
std::map<std::string, double> parse_param_string(const std::string& s);
std::string format_param_string(const std::map<std::string, double>& m);
