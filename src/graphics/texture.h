//
// Created by A on 28.01.2026.
//

#ifndef TEXTURE_H
#define TEXTURE_H



#include <string>

struct texture {
    int width;
    int height;
    std::string path;
    unsigned char *data;

    texture(int width, int height, std::string path, unsigned char *data);
    ~texture();
};



#endif //TEXTURE_H
