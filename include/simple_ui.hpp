#pragma once
#include "player.hpp"
#include <atomic>

namespace tracker {

class SimpleUi {
public:
    SimpleUi(Player& player);
    void run();
private:
    Player& player_;
    std::atomic<bool> running_{true};
};

}
