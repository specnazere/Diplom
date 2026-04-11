//
// Created by A on 10.02.2026.
//

#ifndef LOCALISATION_MANAGER_H
#define LOCALISATION_MANAGER_H



#include <map>
#include <set>
#include <string>
#include <vector>

#include "graphics/font.h"

class localisation_manager {
private:
    static std::map<std::string, std::map<std::string, std::u32string>> localisation_map;
    static std::map<std::string, std::set<char32_t>> charset_per_lang;
    static std::map<std::string, std::unique_ptr<font>> fonts;
    static const std::set<char32_t> default_charset;
    static std::set<std::string> locales;

    localisation_manager();
    ~localisation_manager();

    static std::string current_locale;

    static std::u32string utf8_to_codepoints(const std::string& utf8);

public:
    static void init_localiz();
    static std::u32string get_unicode(const std::string &descriptor);
    static const std::set<std::string> &get_locales();
    static void set_locale(const std::string &locale);

    static const std::set<char32_t>& get_charset();
};



#endif //LOCALISATION_MANAGER_H
