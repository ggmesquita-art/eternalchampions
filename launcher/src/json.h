// JSON minimo (so leitura) para o manifest.json / font_menu.json da arte extraida.
#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

struct JVal {
    enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
    double num = 0;
    bool b = false;
    std::string str;
    std::vector<JVal> arr;
    std::map<std::string, JVal> obj;

    const JVal* get(const std::string& k) const {
        if (type != Obj) return nullptr;
        auto it = obj.find(k);
        return it == obj.end() ? nullptr : &it->second;
    }
    std::string s(const std::string& k, const std::string& d = "") const { auto* v = get(k); return v && v->type == Str ? v->str : d; }
    double n(const std::string& k, double d = 0) const { auto* v = get(k); return v && v->type == Num ? v->num : d; }
    double at(size_t i, double d = 0) const { return type == Arr && i < arr.size() && arr[i].type == Num ? arr[i].num : d; }
};

// Retorna false em erro de sintaxe.
bool json_parse(const std::string& text, JVal& out);
bool json_parse_file(const std::string& utf8_path, JVal& out);
