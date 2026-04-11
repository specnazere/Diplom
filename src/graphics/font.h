//
// Created by A on 05.02.2026.
//

#ifndef FONT_H
#define FONT_H



#include <memory>
#include <set>
#include <unordered_map>

#include "texture.h"
#include "texture_atlas.h"

class font {
public:
    struct glyph_info {
        int width = 0;
        int height = 0;
        int offset_x = 0;
        int offset_y = 0;
        int advance = 0;
        char32_t ch = 0;

        std::unordered_map<char32_t, int> kern;
    };

    font(const std::string& font_file_path, const int& size);
    ~font();

    const std::string path;
    const int size;

    const font::glyph_info &get_glyph(const char32_t &ch);

    const int &get_height(const char32_t &ch);

    unsigned int get_handler(const char32_t &ch);

    const std::set<unsigned int>& get_handlers();

    void add_glyph(char32_t ch, const glyph_info &glyph, unsigned int handler);

    const int& get_size() const;

    const texture_atlas::uv_rect& get_uv(const char32_t& ch) const;

    void set_atlas(texture_atlas* atlas);

    const texture_atlas * get_atlas() const;

private:
    texture_atlas* used_atlas;
    int size_ = 0;
    std::unordered_map<char32_t, std::pair<unsigned int, glyph_info>> glyphs_;
    std::set<unsigned int> textures_;
};



#endif //FONT_H
