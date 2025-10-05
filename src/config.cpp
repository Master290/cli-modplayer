#include "config.hpp"
#include <sstream>
#include <cstdlib>
#include <cstring>
#include <algorithm>

namespace tracker {

Config::Config() {
    load();
}

std::filesystem::path Config::get_config_path() const {
    const char* xdg_config = std::getenv("XDG_CONFIG_HOME");
    std::filesystem::path config_dir;
    
    if (xdg_config && strlen(xdg_config) > 0) {
        config_dir = xdg_config;
    } else {
        const char* home = std::getenv("HOME");
        if (!home || strlen(home) == 0) {
            return "config.ini";
        }
        config_dir = std::filesystem::path(home) / ".config";
    }
    
    config_dir /= "cli-tracker";
    
    std::error_code ec;
    std::filesystem::create_directories(config_dir, ec);
    
    return config_dir / "config.ini";
}

void Config::load() {
    auto config_path = get_config_path();
    
    if (!std::filesystem::exists(config_path)) {
        return;
    }
    
    std::ifstream file(config_path);
    if (!file) {
        return;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        parse_line(line);
    }
}

void Config::parse_line(const std::string& line) {
    if (line.empty() || line[0] == '#' || line[0] == ';') {
        return;
    }
    
    auto pos = line.find('=');
    if (pos == std::string::npos) {
        return;
    }
    
    std::string key = line.substr(0, pos);
    std::string value = line.substr(pos + 1);
    
    key.erase(0, key.find_first_not_of(" \t\r\n"));
    key.erase(key.find_last_not_of(" \t\r\n") + 1);
    value.erase(0, value.find_first_not_of(" \t\r\n"));
    value.erase(value.find_last_not_of(" \t\r\n") + 1);
    
    if (key == "volume") {
        try {
            volume_ = std::stod(value);
            volume_ = std::clamp(volume_, 0.0, 1.0);
        } catch (...) {}
    } else if (key == "theme") {
        theme_ = value;
    }
}

void Config::save() {
    auto config_path = get_config_path();
    
    std::ofstream file(config_path);
    if (!file) {
        std::cerr << "Failed to save config to: " << config_path << std::endl;
        return;
    }
    
    file << "# cli-tracker configuration\n";
    file << "# Volume (0.0 - 1.0)\n";
    file << "volume=" << volume_ << "\n";
    file << "\n";
    file << "# Theme (dark, light, cyberpunk, retro)\n";
    file << "theme=" << theme_ << "\n";
}

} 
