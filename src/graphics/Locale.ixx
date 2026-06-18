//
// Created by A on 10.02.2026.
//

module;

#include <filesystem>
#include <set>
#include <ranges>
#include <string>
#include <unordered_map>

export module locale;

namespace {
    const std::u32string universal_charset = U"!\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}";
}

export
class Locale {
private:
    std::set<char32_t> charset;
    std::unordered_map<std::string, std::u32string> localisation_map;
    bool isCharsetDirty = false;

public:
    void add(const std::string& desc, const std::u32string& string)
    {
        localisation_map[desc] = string;
        isCharsetDirty = true;
    }

    void update_charset()
    {
        isCharsetDirty = false;
        charset.clear();

        for (auto string : localisation_map | std::views::values) {
            charset.insert(string.begin(), string.end());
        }
    }

    [[nodiscard]] const std::set<char32_t>& get_charset()
    {
        if (isCharsetDirty)
            update_charset();

        return charset;
    }

    [[nodiscard]] const std::u32string& operator[](const std::string& key)
    {
        return localisation_map[key];
    }
};
