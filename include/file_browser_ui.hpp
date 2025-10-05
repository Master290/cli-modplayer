#pragma once

#include "file_browser.hpp"
#include <optional>
#include <string>

namespace tracker {


std::optional<std::filesystem::path> run_file_browser_ui(const std::filesystem::path& start_dir);

}
