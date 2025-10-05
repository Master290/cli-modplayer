#pragma once

#include <string>
#include <map>
#include <fstream>
#include <filesystem>
#include <iostream>

namespace tracker {

enum class ThemeType;

class Config {
public:
    Config();
    
    void load();
    void save();
    
    double get_volume() const { return volume_; }
    std::string get_theme() const { return theme_; }
    
    void set_volume(double volume) { volume_ = volume; }
    void set_theme(const std::string& theme) { theme_ = theme; }
    
private:
    std::filesystem::path get_config_path() const;
    void parse_line(const std::string& line);
    
    double volume_{1.0};
    std::string theme_{"dark"};
};

} 
