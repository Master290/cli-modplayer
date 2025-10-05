#include "simple_ui.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <iomanip>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>

namespace tracker {

namespace {
int getch() {
    termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    int ch = getchar();
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    return ch;
}

bool kbhit() {
    termios oldt, newt;
    int ch;
    int oldf;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);
    ch = getchar();
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    fcntl(STDIN_FILENO, F_SETFL, oldf);
    if(ch != EOF) {
        ungetc(ch, stdin);
        return true;
    }
    return false;
}
}

void SimpleUi::run() {
    std::cout << "cli-modplayer v1.3.0 | github.com/Master290/cli-modplayer\n";
    std::cout << "─────────────────────────────────────────────────────────────\n";
    std::cout << "Title:   " << player_.title() << "\n";
    if (!player_.artist().empty() && player_.artist() != "Unknown") {
        std::cout << "Artist:  " << player_.artist() << "\n";
    }
    std::cout << "Type:    " << player_.module_type() << " | " << player_.num_channels() << " channels\n";
    std::cout << "Tracker: " << player_.tracker_name() << "\n";
    if (!player_.date().empty()) {
        std::cout << "Date:    " << player_.date() << "\n";
    }
    std::cout << "Patterns: " << player_.num_patterns() << " | Orders: " << player_.num_orders() << "\n";
    std::cout << "Instruments: " << player_.num_instruments() << " | Samples: " << player_.num_samples() << "\n";
    std::cout << "─────────────────────────────────────────────────────────────\n";
    std::cout << "[Space] pause  [←/→] skip order  [Q] quit\n\n";
    
    while (running_) {
        auto state = player_.snapshot();
        double pos = state.position_seconds;
        double dur = player_.duration_seconds();
        int percent = dur > 0.0 ? static_cast<int>(pos / dur * 100.0) : 0;
        int barw = 50;
        int filled = dur > 0.0 ? static_cast<int>(barw * pos / dur) : 0;
        std::cout << "\r[";
        for (int i = 0; i < barw; ++i) std::cout << (i < filled ? "█" : "─");
        std::cout << "] ";
        std::cout << std::setw(3) << percent << "%  ";
        int min = static_cast<int>(pos) / 60;
        int sec = static_cast<int>(pos) % 60;
        int mind = static_cast<int>(dur) / 60;
        int secd = static_cast<int>(dur) % 60;
        std::cout << std::setw(2) << std::setfill('0') << min << ":" << std::setw(2) << sec;
        std::cout << " / " << std::setw(2) << mind << ":" << std::setw(2) << secd;
        std::cout << "  Order: " << std::setw(2) << std::setfill('0') << state.order 
                  << "/" << std::setw(2) << (player_.num_orders() - 1);
        std::cout << "  Pattern: " << std::setw(2) << state.pattern 
                  << "  Row: " << std::setw(2) << state.row;
        if (state.paused) std::cout << "  [PAUSED]";
        std::cout << "    " << std::flush;
        
        if (kbhit()) {
            int c = getch();
            if (c == ' ')
                player_.toggle_pause();
            else if (c == 'q' || c == 'Q')
                break;
            else if (c == 27) {
                getch();
                int arrow = getch();
                if (arrow == 'C' || arrow == 'l')
                    player_.jump_to_order(1);
                else if (arrow == 'D' || arrow == 'h')
                    player_.jump_to_order(-1);
            }
            else if (c == 'l')
                player_.jump_to_order(1);
            else if (c == 'h')
                player_.jump_to_order(-1);
        }
        if (state.finished) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::cout << "\n\nPlayback finished.\n";
}

}

tracker::SimpleUi::SimpleUi(Player& player) : player_(player) {}
