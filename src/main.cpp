#include "player.hpp"
#include "ui.hpp"

#include <exception>
#include <filesystem>
#include <iostream>
#include <ncurses.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "Usage: modtracker <module-file>" << std::endl;
        return 1;
    }

    std::filesystem::path module_path = argv[1];
    if (!std::filesystem::exists(module_path)) {
        std::cerr << "File not found: " << module_path << std::endl;
        return 1;
    }

    try {
        tracker::Player player(module_path.string());
        player.start();

        tracker::Ui ui(player);
        ui.run();

        player.stop();
        return 0;
    } catch (const std::exception &ex) {
        endwin();
        std::cerr << "Fatal error: " << ex.what() << std::endl;
    } catch (...) {
        endwin();
        std::cerr << "Unknown error occurred." << std::endl;
    }

    return 1;
}
