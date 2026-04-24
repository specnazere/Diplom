#include <exception>
#include <iostream>
#include <csignal>

import game;

void on_assert(int signal) {
    __debugbreak();
    throw std::runtime_error("Assertion failed");
}

int main() {
    srand(time(NULL));

    // int return_value = 0;
    // try {
    //     return_value = game::run();
    // }
    // catch (const std::exception& e) {
    //     std::cerr << e.what() << std::endl;
    //     throw e;
    // }
    // return return_value;

    std::signal(SIGABRT, on_assert);
    std::set_terminate([]() {
        try {
            std::rethrow_exception(std::current_exception());
        }
        catch (const std::exception& e) {
            std::cerr << "Unhandled: " << e.what() << std::endl;
        }
        __debugbreak();
        abort();
    });

    game::run();
}
