#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace tracker {

struct FileEntry {
    std::filesystem::path path;
    std::string display_name;
    bool is_directory;
    std::size_t size;
    
    FileEntry(const std::filesystem::path& p, const std::string& name, bool is_dir, std::size_t sz = 0)
        : path(p), display_name(name), is_directory(is_dir), size(sz) {}
};

class FileBrowser {
public:
    FileBrowser();
    explicit FileBrowser(const std::filesystem::path& start_path);
    
    void navigate_to(const std::filesystem::path& path);
    void navigate_up();
    void navigate_into(std::size_t index);
    
    const std::filesystem::path& current_path() const { return current_path_; }
    const std::vector<FileEntry>& entries() const { return entries_; }
    std::size_t selected_index() const { return selected_index_; }
    const std::string& error_message() const { return error_message_; }
    bool has_error() const { return !error_message_.empty(); }
    
    void select_next();
    void select_previous();
    void select_first();
    void select_last();
    void set_selected_index(std::size_t index);
    
    std::filesystem::path get_selected_file() const;
    
    static bool is_module_file(const std::filesystem::path& path);
    
private:
    void refresh();
    void clear_error();
    
    std::filesystem::path current_path_;
    std::vector<FileEntry> entries_;
    std::size_t selected_index_{0};
    std::string error_message_;
    
    static const std::vector<std::string> module_extensions_;
};

} 
