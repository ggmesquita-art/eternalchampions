// Esquema declarativo das opcoes (PLAN §4): fonte unica para telas, leitura/escrita do ini e argv.
//
// Cada Opt descreve UMA opcao. Se `section` == "launcher.args" a opcao NAO vai para o ini do runner/jogo:
// vive na secao [launcher.args] (ignorada por runner/jogo) e vira argv no JOGAR (`cli_flag`).
// Valores sao guardados como texto; `Config::get` devolve o valor do ini ou o default do esquema.
#pragma once
#include "ini.h"
#include <string>
#include <utility>
#include <vector>

class App;

enum class OType { Bool, Int, Float, Enum, Path, Folder, SaveFile };
enum class Dyn { None, Models, ShaderPresets, HdManifest };

struct Choice { std::string value, label; };

struct Opt {
    std::string id;          // "<secao>.<chave>" (ou "args.<nome>")
    std::string tab;         // video | enh | audio | game | adv
    std::string label, help; // PT-BR
    OType type = OType::Bool;
    std::string section, key;   // secao do ini ("launcher.args" = so argv)
    std::string def;            // default (texto), igual ao do runner/jogo
    double min = 0, max = 0, step = 1;
    std::vector<Choice> choices;
    Dyn dyn = Dyn::None;
    std::string dep_id, dep_val;   // habilitada quando valor de dep_id == dep_val (dep_val "!0" = diferente de "0")
    bool restart = false;          // ↻ exige reiniciar o jogo
    bool dev = false;              // area "Desenvolvedor" (oculta)
    std::string cli_flag;          // so launcher.args: flag do runner
    int precision = 2;             // casas decimais (Float)
};

const std::vector<Opt>& schema();
const Opt* schema_find(const std::string& id);

class Config {
public:
    bool load(const std::filesystem::path& settings_ini);
    bool save() { return ini_.save(); }
    Ini& ini() { return ini_; }

    std::string get(const Opt& o) const;                    // valor efetivo (ini ou default)
    std::string get(const std::string& id) const;
    bool is_modified(const Opt& o) const;                   // valor efetivo != default
    void set(const Opt& o, const std::string& v);           // == default => remove a chave do ini (volta ao default do jogo)
    void set(const std::string& id, const std::string& v);
    void reset(const Opt& o) { set(o, o.def); }
    bool enabled(const Opt& o) const;                       // dependencias
    // dinamico: lista de escolhas efetiva (inclui valor atual desconhecido, p.ex. caminho custom)
    std::vector<Choice> choices(const Opt& o, const App& app) const;
    // argv extra a partir de [launcher.args] (so valores != default)
    std::vector<std::string> build_args() const;
    // [launcher] skip_launcher = 1
    void ensure_skip_launcher();

private:
    Ini ini_;
};

// ---- Perfis de qualidade (§4.4)
struct Profile {
    std::string id, label, summary, cost;
    std::vector<std::pair<std::string, std::string>> values;   // id da opcao -> valor
};
const std::vector<Profile>& profiles();
bool apply_profile(Config& cfg, const std::string& id);
std::string active_profile_id(const Config& cfg);   // "" se nenhum bate exatamente

std::vector<Choice> dyn_choices(const App& app, Dyn d);
