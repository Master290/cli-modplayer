#pragma once

#include "player.hpp"

#include <deque>
#include <string>
#include <vector>
#include <chrono>

namespace tracker {

struct RowRender {
    int order{0};
    int pattern{0};
    int row{0};
    std::vector<std::string> channels;
};

class Ui {
public:
    explicit Ui(Player &player);
    ~Ui();

    void run();

private:
    void setup_screen();
    void teardown_screen();
    void update_history(const TransportState &state);
    void draw(const TransportState &state);
    void draw_header(const TransportState &state);
    void draw_pattern_grid(const TransportState &state);
    void draw_footer();
    void draw_status_bar();
    void draw_channel_visualizers(const TransportState &state, int visualizer_row, int left_margin,
                                  int column_width, int columns);
    void draw_info_overlay(const TransportState &state);
    int color_for_note(const std::string &cell) const;
    int color_for_row_index(int offset_from_center, int max_distance) const;
    void update_visualizer_peaks(const TransportState &state, int total_channels);
    void set_status_message(const std::string &message,
                            std::chrono::milliseconds duration = std::chrono::milliseconds(2000));

private:
    Player &player_;
    bool running_{true};
    bool info_overlay_{false};
    std::string status_message_;
    std::chrono::steady_clock::time_point status_message_until_{};
    std::deque<RowRender> history_;
    int history_capacity_{32};
    int last_order_{-1};
    int last_row_{-1};
    std::vector<double> channel_peaks_;
    std::chrono::steady_clock::time_point last_frame_time_{};
    double last_frame_seconds_{0.0};
    int channel_offset_{0};
    int page_columns_{4};
};

}
