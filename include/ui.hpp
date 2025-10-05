#pragma once

#include "player.hpp"
#include "config.hpp"

#include <chrono>
#include <deque>
#include <string>
#include <vector>

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

namespace tracker {

struct RowRender {
    int order{0};
    int pattern{0};
    int row{0};
    std::vector<std::string> channels;
};

class Ui {
public:
    explicit Ui(Player &player, Config &config, const std::string& module_filename = "output");
    ~Ui();

    void run();

private:
    void reset_ui_state();
    void update_history(const TransportState &state);
    ftxui::Element render(const TransportState &state);
    ftxui::Element render_playback_info(const TransportState &state) const;
    ftxui::Element render_header_visualizer(const TransportState &state) const;
    ftxui::Element render_oscilloscope(const TransportState &state) const;
    ftxui::Element render_active_instruments(const TransportState &state) const;
    ftxui::Element render_pattern_grid(const TransportState &state);
    ftxui::Element render_status_bar();
    ftxui::Element render_footer() const;
    ftxui::Element render_info_overlay(const TransportState &state);
    ftxui::Element render_about_overlay();
    ftxui::Element render_export_dialog();
    ftxui::Elements render_history_rows(const TransportState &state, int columns, int column_width);
    ftxui::Element render_visualizers(const TransportState &state, int columns, int column_width);
    ftxui::Color color_for_note(const std::string &cell) const;
    std::vector<ftxui::Decorator> decorators_for_row_index(int offset_from_center, int max_distance) const;
    void update_visualizer_peaks(const TransportState &state, int total_channels);
    void set_status_message(const std::string &message,
                            std::chrono::milliseconds duration = std::chrono::milliseconds(2000));

private:
    Player &player_;
    Config &config_;
    bool running_{true};
    bool info_overlay_{false};
    int info_scroll_position_{0};
    bool about_overlay_{false};
    bool export_dialog_{false};
    int export_format_selection_{0};
    std::string export_filename_{"output"};
    bool export_in_progress_{false};
    std::size_t export_current_{0};
    std::size_t export_total_{0};
    std::string export_error_;
    std::string status_message_;
    std::chrono::steady_clock::time_point status_message_until_{};
    std::deque<RowRender> history_;
    int history_capacity_{32};
    int last_order_{-1};
    int last_row_{-1};
    TransportState last_state_{};
    std::vector<double> channel_peaks_;
    std::vector<double> master_levels_;
    std::vector<double> master_peaks_;
    double master_overall_level_{0.0};
    std::chrono::steady_clock::time_point last_frame_time_{};
    double last_frame_seconds_{0.0};
    int channel_offset_{0};
    int page_columns_{4};
    double last_volume_{1.0};
};

}
