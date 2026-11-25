// Leitor/escritor de .ini que PRESERVA comentarios, ordem, linhas em branco, chaves e secoes desconhecidas.
// Edicao in-place: mudar um valor reescreve apenas aquela linha (mantendo indentacao, "chave =" e comentario final).
// Gravacao atomica (tmp + MoveFileExW) com o mesmo estilo de fim de linha do arquivo original.
#pragma once
#include <filesystem>
#include <string>
#include <vector>

class Ini {
public:
    bool load(const std::filesystem::path& p);       // arquivo ausente = ini vazio (retorna true)
    bool save();                                     // so grava se houve mudanca; atomico
    bool dirty() const { return dirty_; }
    const std::filesystem::path& path() const { return path_; }

    bool has(const std::string& section, const std::string& key) const;
    bool get(const std::string& section, const std::string& key, std::string& out) const;   // valor "limpo"
    // Retorna true se alterou o arquivo em memoria. Chave existente: edita a linha; senao insere ao fim da secao
    // (cria a secao se preciso).
    bool set(const std::string& section, const std::string& key, const std::string& value);
    bool remove(const std::string& section, const std::string& key);   // apaga a linha da chave

    // para o selftest
    std::string serialize() const;
    size_t line_count() const { return lines_.size(); }

private:
    struct Line {
        std::string raw;        // linha sem EOL
        enum Kind { Other, Section, Kv } kind = Other;
        std::string section;    // secao a que pertence (Kv) / nome (Section)
        std::string key, value; // value = limpo (sem aspas / comentario final)
        size_t vbeg = 0, vend = 0;   // intervalo do texto do valor dentro de raw (Kv)
    };
    int find(const std::string& section, const std::string& key) const;
    void parse_line(Line& l, const std::string& cur_section);
    std::vector<Line> lines_;
    std::filesystem::path path_;
    bool crlf_ = false, final_eol_ = true, dirty_ = false;
};

// Teste embutido: EternalChampions.exe --selftest-ini <arq>. Retorna 0 se OK.
int ini_selftest(const std::string& utf8_path);
