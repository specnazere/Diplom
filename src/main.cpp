#include <exception>
#include <iostream>
#include <ostream>

#include "game.h"

int main() {
    int return_value = 0;
    try {
        return_value = game::run();
    }
    catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        throw e;
    }
    return return_value;
}
