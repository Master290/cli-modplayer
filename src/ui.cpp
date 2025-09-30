#include "ui.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cctype>
#include <iomanip>
#include <iostream>
#include <ncurses.h>
#include <sstream>
#include <string>
#include <thread>

namespace tracker {

namespace {
std::string format_time(double seconds) {
    if (seconds < 0.0) {
        return "--:--";
    }
    int total = static_cast<int>(seconds);
    int minutes = total / 60;
    int secs = total % 60;
    std::ostringstream oss;
    oss << std::setw(2) << std::setfill('0') << minutes << ':'
        << std::setw(2) << std::setfill('0') << secs;
    return oss.str();
}

}

Ui::Ui(Player &player) : player_(player) {}

Ui::~Ui() {
    teardown_screen();
}

void Ui::run() {
    setup_screen();

    while (running_) {
        auto now = std::chrono::steady_clock::now();
        if (last_frame_time_.time_since_epoch().count() == 0) {
            last_frame_time_ = now;
        }
        last_frame_seconds_ = std::chrono::duration<double>(now - last_frame_time_).count();
        if (last_frame_seconds_ < 0.0) {
            last_frame_seconds_ = 0.0;
        }
        if (last_frame_seconds_ > 0.5) {
            last_frame_seconds_ = 0.5;
        }
        last_frame_time_ = now;

        TransportState state = player_.snapshot();
        update_history(state);
        draw(state);

        int ch = getch();
        switch (ch) {
        case 'q':
        case 'Q':
            running_ = false;
            break;
        case ' ':
            player_.toggle_pause();
            break;
        case KEY_RIGHT:
        case 'l':
            player_.jump_to_order(1);
            break;
        case KEY_LEFT:
        case 'h':
            player_.jump_to_order(-1);
            break;
        case KEY_NPAGE:
        case 'd':
            channel_offset_ += std::max(1, page_columns_);
            break;
        case KEY_PPAGE:
        case 'u':
            channel_offset_ = std::max(0, channel_offset_ - std::max(1, page_columns_));
            break;
        case 'n':
        case 'N':
            info_overlay_ = !info_overlay_;
            set_status_message(info_overlay_ ? "Info overlay opened" : "Info overlay closed");
            break;
        case '[': {
            player_.jump_rows(-8);
            state = player_.snapshot();
            std::ostringstream oss;
            oss << "Jumped -8 rows → " << std::setw(2) << std::setfill('0') << state.order
                << ':' << std::setw(2) << state.row;
            set_status_message(oss.str());
            break;
        }
        case ']': {
            player_.jump_rows(8);
            state = player_.snapshot();
            std::ostringstream oss;
            oss << "Jumped +8 rows → " << std::setw(2) << std::setfill('0') << state.order
                << ':' << std::setw(2) << state.row;
            set_status_message(oss.str());
            break;
        }
        default:
            break;
        }

        if (state.finished) {
            running_ = false;
        }

        int total_channels = static_cast<int>(state.channels.size());
        if (total_channels == 0 && !history_.empty()) {
            total_channels = static_cast<int>(history_.back().channels.size());
        }
        int max_offset = std::max(0, total_channels - std::max(1, page_columns_));
        if (channel_offset_ > max_offset) {
            channel_offset_ = max_offset;
        }
        if (channel_offset_ < 0) {
            channel_offset_ = 0;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

void Ui::setup_screen() {
    initscr();
    cbreak();
    noecho();
    curs_set(0);
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);
    history_.clear();
    last_order_ = -1;
    last_row_ = -1;
    channel_offset_ = 0;
    page_columns_ = 4;
    info_overlay_ = false;
    channel_peaks_.clear();
    status_message_.clear();
    status_message_until_ = std::chrono::steady_clock::now();
    last_frame_time_ = std::chrono::steady_clock::now();
    last_frame_seconds_ = 0.0;
    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(1, COLOR_CYAN, -1);
        init_pair(2, COLOR_BLACK, COLOR_CYAN);
        init_pair(3, COLOR_GREEN, -1);
        init_pair(4, COLOR_YELLOW, -1);
        init_pair(5, COLOR_WHITE, -1);
        init_pair(6, COLOR_MAGENTA, -1);
        init_pair(7, COLOR_BLUE, -1);
        init_pair(8, COLOR_GREEN, -1);
        init_pair(9, COLOR_RED, -1);
        init_pair(10, COLOR_YELLOW, -1);
        init_pair(11, COLOR_CYAN, -1);
    }
    history_capacity_ = std::max(8, LINES - 8);
}

void Ui::teardown_screen() {
    endwin();
}

void Ui::update_history(const TransportState &state) {
    if (state.order < 0 || state.row < 0 || state.channels.empty()) {
        return;
    }

    bool advanced = state.order != last_order_ || state.row != last_row_;
    if (!advanced && !history_.empty()) {
        return;
    }

    if (!history_.empty()) {
        const RowRender &prev = history_.back();
        if (state.order == prev.order && state.row == prev.row) {
            return;
        }
        if (state.order < prev.order || (state.order == prev.order && state.row < prev.row)) {
            history_.clear();
        }
    }

    RowRender row;
    row.order = state.order;
    row.pattern = state.pattern;
    row.row = state.row;
    row.channels.reserve(state.channels.size());
    for (const ChannelStatus &ch : state.channels) {
        row.channels.push_back(ch.line);
    }

    history_.push_back(std::move(row));
    while (static_cast<int>(history_.size()) > history_capacity_) {
        history_.pop_front();
    }

    last_order_ = state.order;
    last_row_ = state.row;
}

void Ui::draw(const TransportState &state) {
    erase();
    draw_header(state);
    draw_pattern_grid(state);
    draw_status_bar();
    draw_footer();
    if (info_overlay_) {
        draw_info_overlay(state);
    }
    refresh();
}

void Ui::draw_header(const TransportState &state) {
    attron(COLOR_PAIR(1) | A_BOLD);
    mvprintw(0, 0, "cli-tracker");
    attroff(A_BOLD);
    attron(A_DIM);
    mvprintw(0, 13, "github.com/Master290/cli-tracker");
    attroff(A_DIM);
    attroff(COLOR_PAIR(1));

    mvprintw(1, 0, "Title: %s", player_.title().c_str());
    mvprintw(2, 0, "Order %02d  Pattern %02d  Row %02d  Speed %02d",
             state.order, state.pattern, state.row, state.speed);

    mvprintw(3, 0, "Time: %s / %s%s",
             format_time(state.position_seconds).c_str(),
             format_time(player_.duration_seconds()).c_str(),
             state.paused ? "  [PAUSED]" : "");
    mvprintw(4, 0, "Tracker: %s  Scrub: [ / ] ±8 rows", player_.tracker_name().c_str());
}

void Ui::draw_channel_visualizers(const TransportState &state, int visualizer_row, int left_margin,
                                  int column_width, int columns) {
    if (visualizer_row < 0 || visualizer_row >= LINES - 1 || column_width <= 0) {
        return;
    }

    int max_width = columns * (column_width + 1) - 1;
    max_width = std::min(max_width, COLS - left_margin - 1);
    if (max_width <= 0) {
        return;
    }

    mvhline(visualizer_row, left_margin, ' ', max_width + 1);

    auto amplitude_to_color = [](double amplitude) {
        if (amplitude > 0.75) {
            return 9;
        }
        if (amplitude > 0.4) {
            return 10;
        }
        if (amplitude > 0.15) {
            return 11;
        }
        return 5;
    };

    for (int col = 0; col < columns; ++col) {
        int channel_index = channel_offset_ + col;
        double amplitude = 0.0;
        if (channel_index < static_cast<int>(state.channels.size())) {
            const ChannelStatus &status = state.channels[static_cast<std::size_t>(channel_index)];
            amplitude = std::max(std::abs(status.vu_left), std::abs(status.vu_right));
        }
        amplitude = std::clamp(amplitude, 0.0, 1.0);

        double peak = amplitude;
        if (channel_index < static_cast<int>(channel_peaks_.size())) {
            peak = std::max(peak, channel_peaks_[static_cast<std::size_t>(channel_index)]);
        }
        peak = std::clamp(peak, 0.0, 1.0);

        int filled = static_cast<int>(std::round(amplitude * column_width));
        filled = std::clamp(filled, 0, column_width);
        std::string bar(static_cast<std::size_t>(column_width), ' ');
        if (filled > 0) {
            std::fill_n(bar.begin(), filled, '=');
        }

        if (column_width > 0) {
            int peak_index = static_cast<int>(std::round(peak * column_width)) - 1;
            peak_index = std::clamp(peak_index, 0, column_width - 1);
            if (peak > amplitude + 0.01 && peak_index >= 0 && peak_index < column_width) {
                bar[static_cast<std::size_t>(peak_index)] = '|';
            } else if (peak_index >= 0 && peak_index < column_width && bar[static_cast<std::size_t>(peak_index)] == ' ') {
                bar[static_cast<std::size_t>(peak_index)] = '=';
            }
        }

        int color_pair = amplitude_to_color(amplitude);
        if (has_colors() && color_pair > 0) {
            attron(COLOR_PAIR(color_pair));
        }
        int attrs = 0;
        if (amplitude > 0.0) {
            attrs |= A_BOLD;
        }
        if (attrs != 0) {
            attron(attrs);
        }
        mvprintw(visualizer_row, left_margin + col * (column_width + 1), "%s", bar.c_str());
        if (attrs != 0) {
            attroff(attrs);
        }
        if (has_colors() && color_pair > 0) {
            attroff(COLOR_PAIR(color_pair));
        }
    }
}

void Ui::draw_status_bar() {
    int row = LINES - 3;
    if (row < 0) {
        return;
    }
    mvhline(row, 0, ' ', COLS);

    auto now = std::chrono::steady_clock::now();
    if (!status_message_.empty() && now >= status_message_until_) {
        status_message_.clear();
    }

    if (!status_message_.empty()) {
        std::string message = status_message_;
        if (static_cast<int>(message.size()) >= COLS) {
            if (COLS > 1) {
                message = message.substr(0, COLS - 1);
            } else {
                message.clear();
            }
        }
        if (!message.empty()) {
            attron(A_DIM);
            mvprintw(row, 0, "%s", message.c_str());
            attroff(A_DIM);
        }
    }
}

void Ui::update_visualizer_peaks(const TransportState &state, int total_channels) {
    double decay = std::clamp(last_frame_seconds_ * 1.5, 0.0, 1.0);

    if (total_channels <= 0) {
        for (double &peak : channel_peaks_) {
            peak = std::max(0.0, peak - decay);
        }
        return;
    }

    if (static_cast<int>(channel_peaks_.size()) < total_channels) {
        channel_peaks_.resize(static_cast<std::size_t>(total_channels), 0.0);
    }

    for (int ch = 0; ch < total_channels; ++ch) {
        double amplitude = 0.0;
        if (ch < static_cast<int>(state.channels.size())) {
            const ChannelStatus &status = state.channels[static_cast<std::size_t>(ch)];
            amplitude = std::max(std::abs(status.vu_left), std::abs(status.vu_right));
        }
        amplitude = std::clamp(amplitude, 0.0, 1.0);
        double &peak = channel_peaks_[static_cast<std::size_t>(ch)];
        peak = std::max(amplitude, peak - decay);
        if (peak < 0.0) {
            peak = 0.0;
        }
    }

    for (std::size_t i = static_cast<std::size_t>(total_channels); i < channel_peaks_.size(); ++i) {
        channel_peaks_[i] = std::max(0.0, channel_peaks_[i] - decay);
    }
}

void Ui::set_status_message(const std::string &message, std::chrono::milliseconds duration) {
    status_message_ = message;
    status_message_until_ = std::chrono::steady_clock::now() + duration;
}

void Ui::draw_footer() {
    int row = LINES - 2;
    mvhline(row, 0, '-', COLS);
    mvprintw(row + 1, 0,
             "Space: Play/Pause  [ / ]: Scrub Rows  Left/Right: Order  PgUp/PgDn (u/d): Channels  N: Info  Q: Quit");
}

void Ui::draw_info_overlay(const TransportState &state) {
    int margin = 4;
    int width = std::min(std::max(32, COLS - margin * 2), COLS - 2);
    int height = std::min(std::max(10, LINES - margin * 2), LINES - 2);
    if (width <= 16 || height <= 6) {
        return;
    }

    int top = (LINES - height) / 2;
    int left = (COLS - width) / 2;
    int bottom = top + height - 1;
    int right = left + width - 1;

    for (int y = top; y <= bottom; ++y) {
        if (y < 0 || y >= LINES) {
            continue;
        }
        attron(A_REVERSE);
        mvhline(y, left, ' ', width);
        attroff(A_REVERSE);
    }

    mvaddch(top, left, '+');
    mvaddch(top, right, '+');
    mvaddch(bottom, left, '+');
    mvaddch(bottom, right, '+');
    if (width > 2) {
        mvhline(top, left + 1, '-', width - 2);
        mvhline(bottom, left + 1, '-', width - 2);
    }
    if (height > 2) {
        mvvline(top + 1, left, '|', height - 2);
        mvvline(top + 1, right, '|', height - 2);
    }

    attron(COLOR_PAIR(1) | A_BOLD);
    mvprintw(top, left + 2, " Module Info ");
    attroff(COLOR_PAIR(1) | A_BOLD);

    int info_row = top + 2;
    mvprintw(info_row++, left + 2, "Title: %s", player_.title().c_str());
    mvprintw(info_row++, left + 2, "Duration: %s", format_time(player_.duration_seconds()).c_str());
    mvprintw(info_row++, left + 2, "Channels: %d", static_cast<int>(state.channels.size()));
    mvprintw(info_row++, left + 2, "Tracker: %s", player_.tracker_name().c_str());

    const auto &instruments = player_.instrument_names();
    const auto &samples = player_.sample_names();
    mvprintw(info_row++, left + 2, "Instruments: %zu  Samples: %zu", instruments.size(), samples.size());

    const auto &message_lines = player_.module_message_lines();
    if (!message_lines.empty() && info_row < bottom - 2) {
        int available = std::max(0, bottom - info_row - 2);
        int max_message_lines = std::min({available, 6, static_cast<int>(message_lines.size())});
        if (max_message_lines > 0) {
            attron(A_UNDERLINE);
            mvprintw(info_row++, left + 2, "Message:");
            attroff(A_UNDERLINE);
            int max_line_len = std::max(8, width - 6);
            for (int i = 0; i < max_message_lines && info_row < bottom - 2; ++i) {
                std::string line = message_lines[static_cast<std::size_t>(i)];
                if (static_cast<int>(line.size()) > max_line_len) {
                    if (max_line_len > 1) {
                        line = line.substr(0, max_line_len - 1) + "…";
                    } else {
                        line = line.substr(0, max_line_len);
                    }
                }
                mvprintw(info_row++, left + 4, "%s", line.c_str());
            }
            if (static_cast<int>(message_lines.size()) > max_message_lines && info_row < bottom - 2) {
                mvprintw(info_row++, left + 4, "... (%zu more lines)", message_lines.size() - static_cast<std::size_t>(max_message_lines));
            }
            if (info_row < bottom - 2) {
                ++info_row;
            }
        }
    }

    int remaining_rows = bottom - info_row - 1;
    if (remaining_rows <= 0) {
        mvprintw(bottom - 1, left + 2, "Press N to close");
        return;
    }

    int columns = (width > 60 && remaining_rows > 4) ? 2 : 1;
    int column_width = (width - 4) / columns;
    int rows_per_column = remaining_rows;
    if (columns > 1) {
        rows_per_column = std::max(1, remaining_rows / columns);
    }

    int max_items = rows_per_column * columns;
    int displayed = std::min(static_cast<int>(instruments.size()), max_items);
    for (int i = 0; i < displayed; ++i) {
        int column = (columns > 1) ? i / rows_per_column : 0;
        int row = (columns > 1) ? i % rows_per_column : i;
        int y = info_row + row;
        if (y >= bottom - 1) {
            break;
        }
        int x = left + 2 + column * column_width;
        int max_name_len = std::max(3, column_width - 6);
        std::string name = instruments[static_cast<std::size_t>(i)];
        if (static_cast<int>(name.size()) > max_name_len) {
            if (max_name_len > 1) {
                name = name.substr(0, max_name_len - 1) + "…";
            } else {
                name = name.substr(0, max_name_len);
            }
        }
        mvprintw(y, x, "%2d %s", i + 1, name.c_str());
    }

    if (static_cast<int>(instruments.size()) > displayed) {
        mvprintw(bottom - 2, left + 2, "(+%zu more)", instruments.size() - displayed);
    }

    mvprintw(bottom - 1, left + 2, "Press N to close");
}

void Ui::draw_pattern_grid(const TransportState &state) {
    int grid_top = 6;
    int grid_bottom = LINES - 3;
    if (grid_bottom <= grid_top) {
        return;
    }

    int left_margin = 8;
    int min_column_width = 16;
    int available_width = std::max(20, COLS - left_margin - 1);

    int total_channels = static_cast<int>(state.channels.size());
    if (total_channels == 0 && !history_.empty()) {
        total_channels = static_cast<int>(history_.back().channels.size());
    }

    int max_possible = std::max(1, available_width / (min_column_width + 1));
    int base_columns = max_possible;
    if (total_channels > 0) {
        base_columns = std::min(total_channels, max_possible);
    }
    base_columns = std::max(1, base_columns);
    page_columns_ = base_columns;

    if (total_channels > 0 && channel_offset_ >= total_channels) {
        channel_offset_ = std::max(0, total_channels - page_columns_);
    }

    int columns = page_columns_;
    if (total_channels > 0) {
        columns = std::min(page_columns_, total_channels - channel_offset_);
    }
    columns = std::max(1, columns);

    int column_width = std::max(min_column_width, available_width / columns - 1);

    update_visualizer_peaks(state, static_cast<int>(state.channels.size()));

    attron(COLOR_PAIR(3) | A_BOLD);
    for (int col = 0; col < columns; ++col) {
        mvprintw(grid_top, left_margin + col * (column_width + 1), "CH%-2d", channel_offset_ + col + 1);
    }
    if (channel_offset_ > 0) {
        mvaddch(grid_top, left_margin - 2, '<');
    }
    if (total_channels > 0 && channel_offset_ + columns < total_channels) {
        int right_x = left_margin + columns * (column_width + 1);
        if (right_x < COLS) {
            mvaddch(grid_top, right_x, '>');
        }
    }
    attroff(COLOR_PAIR(3) | A_BOLD);

    int visualizer_row = grid_top + 1;
    bool have_visualizer_space = visualizer_row < grid_bottom;
    if (have_visualizer_space) {
        draw_channel_visualizers(state, visualizer_row, left_margin, column_width, columns);
    }

    int data_top = have_visualizer_space ? visualizer_row + 1 : grid_top + 1;
    int available_lines = grid_bottom - data_top;
    if (available_lines <= 0) {
        return;
    }

    const RowRender *current_row = nullptr;
    RowRender fallback_row;
    if (!history_.empty()) {
        current_row = &history_.back();
    } else if (!state.channels.empty()) {
        fallback_row.order = state.order;
        fallback_row.pattern = state.pattern;
        fallback_row.row = state.row;
        fallback_row.channels.reserve(state.channels.size());
        for (const auto &ch : state.channels) {
            fallback_row.channels.push_back(ch.line);
        }
        current_row = &fallback_row;
    }

    int history_available = history_.empty() ? 0 : static_cast<int>(history_.size()) - 1;
    int future_available = static_cast<int>(state.preview_rows.size());
    int max_lines = available_lines;

    int max_history_display = std::min(history_available, max_lines - 1);
    int history_anchor = std::clamp(max_lines / 2, 0, max_lines - 1);
    int history_lines = std::min(max_history_display, history_anchor);
    if (history_lines < 0) {
        history_lines = 0;
    }

    int future_capacity = std::max(0, max_lines - history_lines - 1);
    int future_lines = std::min(future_available, future_capacity);
    if (future_lines < 0) {
        future_lines = 0;
    }

    int screen_row = data_top;

    int history_start = history_available - history_lines;
    if (history_start < 0) {
        history_start = 0;
    }
    for (int i = 0; i < history_lines && screen_row <= grid_bottom; ++i, ++screen_row) {
        const RowRender &row = history_[static_cast<std::size_t>(history_start + i)];
        int offset_from_center = -(history_lines - i);
        int attr = color_for_row_index(offset_from_center, history_lines + future_lines + 1);
        bool attr_active = attr != 0 && attr != A_NORMAL;
        if (attr_active) {
            attron(attr);
        }
        attron(COLOR_PAIR(4));
        mvprintw(screen_row, 0, "%02d:%02d", row.order, row.row);
        attroff(COLOR_PAIR(4));

        int x = left_margin;
        for (int col = 0; col < columns; ++col) {
            std::string text;
            int channel_index = channel_offset_ + col;
            if (channel_index < static_cast<int>(row.channels.size())) {
                text = row.channels[static_cast<std::size_t>(channel_index)];
            }
            if (text.empty()) {
                text = "--- .. .. ...";
            }
            int color = color_for_note(text);
            if (color > 0) {
                attron(COLOR_PAIR(color));
            }
            mvprintw(screen_row, x, "%-*s", column_width, text.c_str());
            if (color > 0) {
                attroff(COLOR_PAIR(color));
            }
            x += column_width + 1;
        }
        if (attr_active) {
            attroff(attr);
        }
    }

    if (current_row && screen_row <= grid_bottom) {
        attron(COLOR_PAIR(2) | A_BOLD);
        mvprintw(screen_row, 0, "%02d:%02d", current_row->order, current_row->row);
        int x = left_margin;
        for (int col = 0; col < columns; ++col) {
            std::string text;
            int channel_index = channel_offset_ + col;
            if (channel_index < static_cast<int>(current_row->channels.size())) {
                text = current_row->channels[static_cast<std::size_t>(channel_index)];
            }
            if (text.empty()) {
                text = "--- .. .. ...";
            }
            mvprintw(screen_row, x, "%-*s", column_width, text.c_str());
            x += column_width + 1;
        }
        attroff(COLOR_PAIR(2) | A_BOLD);
        ++screen_row;
    }

    for (int i = 0; i < future_lines && screen_row <= grid_bottom; ++i, ++screen_row) {
        const PatternRowPreview &preview = state.preview_rows[static_cast<std::size_t>(i)];
        int offset_from_center = i + 1;
        int attr = color_for_row_index(offset_from_center, history_lines + future_lines + 1);
        bool attr_active = attr != 0 && attr != A_NORMAL;
        if (attr_active) {
            attron(attr);
        }
        attron(COLOR_PAIR(4));
        mvprintw(screen_row, 0, "%02d:%02d", preview.order, preview.row);
        attroff(COLOR_PAIR(4));

        int x = left_margin;
        for (int col = 0; col < columns; ++col) {
            std::string text;
            int channel_index = channel_offset_ + col;
            if (channel_index < static_cast<int>(preview.channels.size())) {
                text = preview.channels[static_cast<std::size_t>(channel_index)];
            }
            if (text.empty()) {
                text = "--- .. .. ...";
            }
            int color = color_for_note(text);
            if (color > 0) {
                attron(COLOR_PAIR(color));
            }
            mvprintw(screen_row, x, "%-*s", column_width, text.c_str());
            if (color > 0) {
                attroff(COLOR_PAIR(color));
            }
            x += column_width + 1;
        }
        if (attr_active) {
            attroff(attr);
        }
    }
}

int Ui::color_for_note(const std::string &cell) const {
    if (!has_colors()) {
        return 0;
    }
    if (cell.size() < 3) {
        return 5;
    }
    char n0 = static_cast<char>(std::toupper(static_cast<unsigned char>(cell[0])));
    char n1 = cell.size() > 1 ? cell[1] : '-';
    char n2 = cell.size() > 2 ? cell[2] : '-';
    if ((n0 == '-' && n1 == '-' && n2 == '-') || n0 == ' ') {
        return 5;
    }

    int note_index = -1;
    switch (n0) {
    case 'C': note_index = 0; break;
    case 'D': note_index = 2; break;
    case 'E': note_index = 4; break;
    case 'F': note_index = 5; break;
    case 'G': note_index = 7; break;
    case 'A': note_index = 9; break;
    case 'B': note_index = 11; break;
    default: break;
    }
    if (note_index == -1) {
        return 5;
    }
    if (n1 == '#') {
        note_index = (note_index + 1) % 12;
    }

    static const std::array<int, 12> PALETTE = {9, 10, 11, 6, 8, 9, 10, 11, 6, 7, 8, 5};
    return PALETTE[static_cast<std::size_t>(note_index)];
}

int Ui::color_for_row_index(int offset_from_center, int max_distance) const {
    int distance = std::abs(offset_from_center);
    if (distance == 0) {
        return A_NORMAL;
    }
    int bright_band = 2;
    int soft_falloff = 5;
    if (max_distance > 0) {
        bright_band = std::max(2, max_distance / 6);
        soft_falloff = std::max(bright_band + 1, max_distance / 3);
    }

    if (distance <= bright_band) {
        return offset_from_center > 0 ? A_BOLD : A_NORMAL;
    }
    if (distance <= soft_falloff) {
        return A_NORMAL;
    }

    if (distance <= soft_falloff + 2) {
        return A_DIM;
    }
    return A_DIM;
}

}
