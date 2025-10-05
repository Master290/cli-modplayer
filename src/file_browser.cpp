#include "file_browser.hpp"

#include <algorithm>
#include <cctype>
#include <system_error>

namespace tracker {

const std::vector<std::string> FileBrowser::module_extensions_ = {
    ".mod", ".xm", ".s3m", ".it", ".mptm", ".stm", ".nst", ".m15", ".stk",
    ".wow", ".ult", ".669", ".mtm", ".med", ".far", ".mdl", ".ams", ".dsm",
    ".amf", ".okt", ".dmf", ".ptm", ".psm", ".mt2", ".dbm", ".digi", ".imf",
    ".j2b", ".gdm", ".umx", ".plm", ".mo3", ".xpk", ".ppm", ".mmcmp"
};

FileBrowser::FileBrowser() : FileBrowser(std::filesystem::current_path()) {}

FileBrowser::FileBrowser(const std::filesystem::path& start_path) 
    : current_path_(start_path) {
    refresh();
}

void FileBrowser::navigate_to(const std::filesystem::path& path) {
    clear_error();
    
    try {
        if (!std::filesystem::exists(path)) {
            error_message_ = "Path does not exist: " + path.string();
            return;
        }
        
        if (!std::filesystem::is_directory(path)) {
            error_message_ = "Not a directory: " + path.string();
            return;
        }
        
        current_path_ = std::filesystem::canonical(path);
        selected_index_ = 0;
        refresh();
    } catch (const std::filesystem::filesystem_error& e) {
        error_message_ = "Navigation error: " + std::string(e.what());
    }
}

void FileBrowser::navigate_up() {
    if (current_path_.has_parent_path() && current_path_ != current_path_.root_path()) {
        navigate_to(current_path_.parent_path());
    }
}

void FileBrowser::navigate_into(std::size_t index) {
    if (index >= entries_.size()) {
        return;
    }
    
    const auto& entry = entries_[index];
    if (entry.is_directory) {
        navigate_to(entry.path);
    }
}

void FileBrowser::refresh() {
    entries_.clear();
    clear_error();
    
    try {
        if (current_path_.has_parent_path() && current_path_ != current_path_.root_path()) {
            entries_.emplace_back(current_path_.parent_path(), "..", true, 0);
        }
        
        std::vector<FileEntry> directories;
        std::vector<FileEntry> files;
        
        for (const auto& entry : std::filesystem::directory_iterator(current_path_)) {
            try {
                const auto& path = entry.path();
                std::string name = path.filename().string();
                bool is_dir = entry.is_directory();
                std::size_t size = is_dir ? 0 : (entry.is_regular_file() ? entry.file_size() : 0);
                
                if (is_dir) {
                    directories.emplace_back(path, name, true, 0);
                } else if (is_module_file(path)) {
                    files.emplace_back(path, name, false, size);
                }
            } catch (const std::filesystem::filesystem_error&) {
                continue;
            }
        }
        
        auto sort_by_name = [](const FileEntry& a, const FileEntry& b) {
            std::string a_lower = a.display_name;
            std::string b_lower = b.display_name;
            std::transform(a_lower.begin(), a_lower.end(), a_lower.begin(), ::tolower);
            std::transform(b_lower.begin(), b_lower.end(), b_lower.begin(), ::tolower);
            return a_lower < b_lower;
        };
        
        std::sort(directories.begin(), directories.end(), sort_by_name);
        std::sort(files.begin(), files.end(), sort_by_name);
        
        entries_.insert(entries_.end(), directories.begin(), directories.end());
        entries_.insert(entries_.end(), files.begin(), files.end());
        
        if (selected_index_ >= entries_.size() && !entries_.empty()) {
            selected_index_ = entries_.size() - 1;
        }
        
    } catch (const std::filesystem::filesystem_error& e) {
        error_message_ = "Error reading directory: " + std::string(e.what());
    }
}

void FileBrowser::select_next() {
    if (!entries_.empty() && selected_index_ < entries_.size() - 1) {
        ++selected_index_;
    }
}

void FileBrowser::select_previous() {
    if (selected_index_ > 0) {
        --selected_index_;
    }
}

void FileBrowser::select_first() {
    selected_index_ = 0;
}

void FileBrowser::select_last() {
    if (!entries_.empty()) {
        selected_index_ = entries_.size() - 1;
    }
}

void FileBrowser::set_selected_index(std::size_t index) {
    if (index < entries_.size()) {
        selected_index_ = index;
    }
}

std::filesystem::path FileBrowser::get_selected_file() const {
    if (selected_index_ >= entries_.size()) {
        return {};
    }
    
    const auto& entry = entries_[selected_index_];
    if (entry.is_directory) {
        return {};
    }
    
    return entry.path;
}

bool FileBrowser::is_module_file(const std::filesystem::path& path) {
    if (!path.has_extension()) {
        return false;
    }
    
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    
    return std::find(module_extensions_.begin(), module_extensions_.end(), ext) 
           != module_extensions_.end();
}

void FileBrowser::clear_error() {
    error_message_.clear();
}

} 
