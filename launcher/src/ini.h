// Leitor/escritor de .ini que PRESERVA comentarios, ordem, linhas em branco, chaves e secoes desconhecidas.
// Edicao in-place: mudar um valor reescreve apenas aquela linha (mantendo indentacao, "chave =" e comentario final).
// Gravacao atomica (settings.ini.launcher.tmp + MoveFileEx), com estas garantias:
//  - BOM UTF-8 removido ao ler e preservado ao gravar; estilo de fim de linha preservado;
//  - valores com " ;", " #", espacos nas pontas sao gravados entre aspas (como o runner/jogo leem);
//  - se o arquivo mudou no disco entre load() e save() (ex.: o runner regravou), recarrega e reaplica SO as chaves
//    que o launcher alterou (nada que o runner escreveu e perdido);
//  - <arquivo>.bak do estado anterior na primeira gravacao da sessao.
#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

class Ini {
public:
    bool load(const std::filesystem::path& p);       // arquivo ausente = ini vazio (retorna true)
    bool save();                                     // so grava se houve mudanca; em lote (begin_batch) adia ate end_batch
    bool dirty() const { return dirty_; }
    const std::filesystem::path& path() const { return path_; }
    const std::string& error() const { return error_; }   // ultimo erro de save()

    bool has(const std::string& section, const std::string& key) const;
    bool get(const std::string& section, const std::string& key, std::string& out) const;   // valor "limpo"
    // Retorna true se alterou o arquivo em memoria. Chave existente: edita a linha; senao insere ao fim da secao
    // (cria a secao se preciso).
    bool set(const std::string& section, const std::string& key, const std::string& value);
    bool remove(const std::string& section, const std::string& key);   // apaga a linha da chave
    // Remove linhas cuja chave so difere de `key` na caixa (ex.: autofire.x x autofire.X); devolve quantas.
    int remove_variants(const std::string& section, const std::string& key);

    // Lote: varias alteracoes com um unico save() (end_batch grava e devolve o resultado).
    void begin_batch() { ++batch_; }
    bool end_batch();

    // para o selftest
    std::string serialize() const;
    size_t line_count() const { return lines_.size(); }
    bool has_bom() const { return bom_; }

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
    void parse_text(const std::string& data);
    bool read_disk(std::string& data) const;
    bool disk_changed() const;
    void remember_disk();
    std::vector<Line> lines_;
    std::filesystem::path path_;
    bool crlf_ = false, final_eol_ = true, dirty_ = false, bom_ = false;
    bool bak_done_ = false, disk_known_ = false, disk_exists_ = false;
    uintmax_t disk_size_ = 0;
    int64_t disk_mtime_ = 0;
    int batch_ = 0;
    std::string error_;
    std::map<std::pair<std::string, std::string>, std::pair<bool, std::string>> touched_;   // chaves alteradas: (existe, valor)
};

// Teste embutido: EternalChampions.exe --selftest-ini <arq>. Retorna 0 se OK.
int ini_selftest(const std::string& utf8_path);
