//
// Created by A on 05.02.2026.
//

#include "text_box.h"

#include <iostream>

#include "misc/localisation_manager.h"

text_box::text_box() {
}

void text_box::set_pos(const int &x, const int &y) {
    pos_x = x;
    pos_y = y;
    reload_positions();
}

void text_box::set_width(const int &box_width) {
    width = box_width;
    reload_positions();
}

void text_box::set_text(const std::string &descriptor) {
    text = localisation_manager::get_unicode(descriptor);
    reload_text();
}

void text_box::set_font(::font *new_font) {
    font = new_font;
    reload_positions();
}

void text_box::reload_positions() const {
    int offset_x = -width / 2;
    int offset_y = 0;
    int words_counter = 0;
    bool writing_word = false;
    bool first_letter = true;
    char32_t prev_char = 0;

    for (int i = 0; i < text.size(); i++) {
        if (!writing_word) {
            writing_word = true;
            if (offset_x + words_wideness[words_counter] > width / 2) {
                offset_x = -width / 2;
                offset_y -= font->get_size();
                first_letter = true;
                prev_char = 0;
            }
        }
        if (first_letter) {
            first_letter = false;
            offset_x -= font->get_glyph(text[i]).offset_x;
        }

        if (text[i] == ' ') {
            if (offset_x > -width / 2) offset_x += font->get_size() / 2;
            writing_word = false;
            prev_char = 0;
            words_counter++;
        } else {
            int kern = 0;
            if (prev_char != 0)
                kern = font->get_glyph(text[i]).kern.at(prev_char);
            glyphs_[i - words_counter]->set_position(
                pos_x + offset_x + font->get_glyph(text[i]).width / 2 + font->get_glyph(text[i]).offset_x + kern,
                pos_y + offset_y + font->get_glyph(text[i]).height / 2 - font->get_glyph(text[i]).offset_y +
                (font->get_size() - font->get_glyph(text[i]).height));
            prev_char = text[i];
        }
        offset_x += font->get_glyph(text[i]).advance;
    }
}

void text_box::reload_text() {
    for (auto sprite : glyphs_) {
        sprite->markDead();
    }
    glyphs_.clear();
    words_wideness.clear();
    words_wideness.push_back(0);

    if (text.size() == 0) return;

    glyphs_.reserve(text.size());
    for (char32_t c : text) {
        if (c != U' ') {
            glyphs_.push_back(new sprite(font->get_handler(c)));
            glyphs_.back()->load_uv_rect(font->get_atlas());
            words_wideness.back() += font->get_glyph(c).width;
        } else {
            words_wideness.push_back(0);
        }
    }

    reload_positions();
}
