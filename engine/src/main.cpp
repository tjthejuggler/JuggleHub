#include "Engine.hpp"
#include <iostream>

int main(int argc, char* argv[]) {
    try {
        Engine engine("config.json");
        engine.run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}