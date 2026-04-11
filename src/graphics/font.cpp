//
// Created by A on 05.02.2026.
//

#include "font.h"

#include <iostream>
#include <utility>

#include "texture_manager.h"

font::font(const std::string &font_file_path, const int& font_size) :
    path(font_file_path), size(font_size) {
}

font::~font() {}

const font::glyph_info &font::get_glyph(const char32_t &ch) {
    return glyphs_[ch].second;
}

unsigned int font::get_handler(const char32_t& ch) {
    return glyphs_[ch].first;
}

const std::set<unsigned int>& font::get_handlers() {
    return textures_;
}

void font::add_glyph(char32_t ch, const glyph_info &glyph, unsigned int handler) {
    glyphs_.emplace(ch, std::pair(handler, glyph));
    textures_.emplace(handler);
    if (glyph.height > size_) {
        size_ = glyph.height;
    }
}

const int& font::get_size() const {
    return size_;
}

const texture_atlas::uv_rect& font::get_uv(const char32_t& ch) const {
    return used_atlas->get_uv(glyphs_.at(ch).first);
}

void font::set_atlas(texture_atlas* atlas) {
    used_atlas = atlas;
}

const texture_atlas * font::get_atlas() const {
    return used_atlas;
}
