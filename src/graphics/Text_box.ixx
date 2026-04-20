//
// Created by A on 05.02.2026.
//

module;

#include <iostream>
#include <vector>

export module text_box;

import font;
import sprite;
import sprite_repository;

export
enum text_alignment {
    left,
    center,
    justify,
    right
};

export
class Text_box {
private:
    int width = 0;
    int pos_x = 0;
    int pos_y = 0;

    Font* font = nullptr;

    std::vector<size_t> indices_;
    std::u32string text;

    std::vector<int> words_wideness;

    text_alignment alignment = left;

    SpritesRepository& sprites_;

public:
    Text_box(SpritesRepository& spritesRepo) :
        sprites_(spritesRepo) {}

    void set_pos(const int &x, const int &y) {
        pos_x = x;
        pos_y = y;
        reload_positions();
    }
    void set_width(const int &box_width) {
        width = box_width;
        reload_positions();
    }
    void set_text(const std::u32string str) {
        text = str;
    }
    void set_font(Font* new_font) {
        font = new_font;
        reload_positions();
    }

    void bake_text() {
        clear_sprites();
        if (text.size() == 0) return;

        int i = 0;
        for (char32_t c : text) {
            if (c != U' ' && c != U'\t' && c != U'\r' && c != U'\n') {
                indices_.push_back(sprites_.addSprite(SpriteData(), 0));
                auto& spriteData = sprites_.dataFromSpriteIdx(indices_[i++]);

                spriteData.width = font->get_glyph(c).width;
                spriteData.height = font->get_glyph(c).height;

                spriteData.uvRect = sprites_.atlas.get_uv(font->get_glyph(c).textureIDesc);
                words_wideness.back() -= font->get_glyph(c).width;
            } else {
                if (c == U' ') words_wideness.push_back(0);
            }
        }

        reload_positions();
    }

    void clear_sprites()
    {
        for (auto spriteIdx : indices_) {
            sprites_.removeSprite(spriteIdx);
        }
        indices_.clear();

        words_wideness.clear();
        words_wideness.push_back(0);
    }

    void reload_positions() const {
        if (words_wideness.empty()) return;
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
                    if (font->get_glyph(text[i]).kern.contains(prev_char))
                        kern = font->get_glyph(text[i]).kern.at(prev_char);

                auto& spriteData = sprites_.dataFromSpriteIdx(indices_[i - words_counter]);
                spriteData.position_x = pos_x + offset_x + font->get_glyph(text[i]).width / 2 + font->get_glyph(text[i]).offset_x + kern;
                spriteData.position_y = pos_y + offset_y + font->get_glyph(text[i]).height / 2 - font->get_glyph(text[i]).offset_y +
                    (font->get_size() - font->get_glyph(text[i]).height);
                prev_char = text[i];
            }
            offset_x += font->get_glyph(text[i]).advance;
        }
    }
};
