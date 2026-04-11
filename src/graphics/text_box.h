//
// Created by A on 05.02.2026.
//

#ifndef TEXT_BOX_H
#define TEXT_BOX_H



#include <vector>

#include "font.h"
#include "sprite.h"

enum text_alignment {
    left,
    center,
    justify,
    right
};


class text_box {
public:
    text_box();

    void set_pos(const int& x, const int& y);
    void set_width(const int& box_width);
    void set_text(const std::string& text);
    void set_font(font* new_font);

private:
    void reload_positions() const;
    void reload_text();

    int width = 0;
    int pos_x = 0;
    int pos_y = 0;

    font* font = nullptr;

    std::vector<sprite*> glyphs_;
    std::u32string text;

    std::vector<int> words_wideness;

    text_alignment alignment = left;
};



#endif //TEXT_BOX_H
