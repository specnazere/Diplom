
    // thirdparty/stb/stb_realisations.cpp

    // Реализация stb_image
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

    // Реализация stb_truetype (внутри него уже есть stb_rect_pack!)

#define STB_RECT_PACK_IMPLEMENTATION
#include "stb_rect_pack.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

    // НЕ включайте сюда stb_rect_pack.h отдельно!
