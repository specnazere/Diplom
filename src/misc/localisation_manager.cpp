//
// Created by A on 10.02.2026.
//

#include <filesystem>
#include <fstream>

#include "localisation_manager.h"

#include <iostream>
#include <set>
#include <vector>

#include "graphics/texture_manager.h"

namespace fs = std::filesystem;

namespace {
    const std::u32string universal_charset = U"!\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}";
}

std::map<std::string, std::map<std::string, std::u32string>> localisation_manager::localisation_map;
std::map<std::string, std::set<char32_t>> localisation_manager::charset_per_lang;
std::string localisation_manager::current_locale = "english";
const std::set<char32_t> localisation_manager::default_charset = std::set(universal_charset.begin(), universal_charset.end());
std::set<std::string> localisation_manager::locales;
std::map<std::string, std::unique_ptr<font>> localisation_manager::fonts;

std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> conv_utf8_utf32;

std::u32string localisation_manager::utf8_to_codepoints(const std::string& utf8) {
    // std::u32string result;
    // size_t i = 0;
    // while (i < utf8.size()) {
    //     uint8_t byte = static_cast<uint8_t>(utf8[i]);
    //     char32_t cp;
    //
    //     if ((byte & 0x80) == 0x00) {           // 0xxxxxxx
    //         cp = byte;
    //         i += 1;
    //     } else if ((byte & 0xE0) == 0xC0) {    // 110xxxxx 10xxxxxx
    //         cp = ((byte & 0x1F) << 6) | (utf8[i+1] & 0x3F);
    //         i += 2;
    //     } else if ((byte & 0xF0) == 0xE0) {    // 1110xxxx 10xxxxxx 10xxxxxx
    //         cp = ((byte & 0x0F) << 12) | ((utf8[i+1] & 0x3F) << 6) | (utf8[i+2] & 0x3F);
    //         i += 3;
    //     } else if ((byte & 0xF8) == 0xF0) {    // 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
    //         cp = ((byte & 0x07) << 18) | ((utf8[i+1] & 0x3F) << 12) |
    //              ((utf8[i+2] & 0x3F) << 6) | (utf8[i+3] & 0x3F);
    //         i += 4;
    //     } else {
    //         std::cerr << "Invalid UTF-8 character: " << utf8 << std::endl;
    //         i += 1;
    //         continue;
    //     }
    //
    //     result.push_back(cp);
    // }
    // return result;

    return conv_utf8_utf32.from_bytes(utf8);
}

void localisation_manager::init_localiz() {
    for (const auto& entry : fs::recursive_directory_iterator("res\\localiz")) {
        if (!entry.is_regular_file()) continue;

        std::fstream file(entry.path(), std::ios::in | std::ios::binary);

        std::string line;

        std::getline(file, line);
        if (line[line.size() - 1] == '\r') line.pop_back();
        if (line[0] != '|') {
            std::cerr << "First line in localisation .csv files must be languages: " << entry.path() << std::endl;
            std::cerr << "Style: \"|<language1>[|<language2>]...\"" << entry.path() << std::endl;
            continue;
        }
        std::vector<int> separators;
        auto load_separators = [&](const std::string& separated_line){
            separators.clear();
            for (int pos = 0; pos < separated_line.size(); pos++) {
                if (separated_line[pos] == '|') {
                    separators.push_back(pos);
                }
            }
            separators.push_back(line.size());
        };
        load_separators(line);

        std::vector<std::string> locales;
        locales.reserve(separators.size());
        for (int i = 0; i < separators.size() - 1; i++) {
            locales.emplace_back(line.substr(separators[i] + 1, separators[i + 1] - 1));
        }

        while (std::getline(file, line)) {
            load_separators(line);
            std::string descriptor = line.substr(0, separators[0]);

            for (int i = 0; i < separators.size() - 1; i++) {
                localisation_map[locales[i]][descriptor] = utf8_to_codepoints(line.substr(separators[i] + 1, separators[i + 1] - separators[i] - 1));
                if (charset_per_lang.find(locales[i]) == charset_per_lang.end()) {
                    charset_per_lang.emplace(locales[i], default_charset);
                }
                charset_per_lang[locales[i]].insert(localisation_map[locales[i]][descriptor].begin(), localisation_map[locales[i]][descriptor].end());
            }
        }
    }
    return;
}

std::u32string localisation_manager::get_unicode(const std::string &descriptor) {
    if (charset_per_lang[current_locale].empty()) {
        std::cerr << "Locale \"" << current_locale << "\" is empty" << std::endl;
        return {};
    }
    if (localisation_map[current_locale].find(descriptor) == localisation_map[current_locale].end()) {
        std::cerr << "Locale \"" << descriptor << "\" have no locale for \"" << current_locale << "\" descriptor" << std::endl;
        return {};
    }

    return localisation_map[current_locale][descriptor];
}

const std::set<std::string>& localisation_manager::get_locales() {
    return locales;
}

void localisation_manager::set_locale(const std::string &locale) {
    if (charset_per_lang.find(locale) != charset_per_lang.end()) {
        if (current_locale != locale) {
            texture_manager::reload_charset(charset_per_lang[locale]);
            current_locale = locale;
        }
    }
    else {
        std::cerr << "Locale \"" << locale << "\" wasn't found" << std::endl;
    }
}

const std::set<char32_t> & localisation_manager::get_charset() {
    return charset_per_lang.find(current_locale)->second;
}
