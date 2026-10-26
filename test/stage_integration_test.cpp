#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string_view>
#include <vector>

#include "cyberverse/runtime/clock_tree.hpp"
#include "cyberverse/runtime/frame_pipeline.hpp"
#include "cyberverse/runtime/frame_schedule.hpp"

using cyberverse::runtime::clock_node;
using cyberverse::runtime::frame_budget;
using cyberverse::runtime::frame_pipeline;
using cyberverse::runtime::frame_schedule;
using cyberverse::runtime::frame_stage;
using cyberverse::runtime::frame_stage_options;

namespace {

class recording_stage final : public frame_stage {
public:
  explicit recording_stage(std::string_view label, bool fixed_step, std::vector<std::string_view>* render_order)
      : frame_stage{ fixed_step }, label_{ label }, render_order_{ render_order } {}

  void on_update(double dt, std::uint64_t, const frame_budget&) override {
    ++updates;
    last_dt = dt;
  }

  void on_render(double dt, double alpha, const frame_budget&) override {
    ++renders;
    last_render_dt = dt;
    last_alpha = alpha;
    if (render_order_ != nullptr) {
      render_order_->push_back(label_);
    }
  }

  std::string_view label_;
  std::vector<std::string_view>* render_order_ = nullptr;
  int updates = 0;
  int renders = 0;
  double last_dt = 0.0;
  double last_render_dt = 0.0;
  double last_alpha = 0.0;
};

class test_pipeline final : public frame_pipeline {
public:
  test_pipeline() : frame_pipeline{ frame_schedule{} } {}

  double tick_clock() noexcept override { return 0.0; }

  void on_poll() noexcept override {
    if (toggle_game) {
      game_clock->set_paused(!game_clock->is_paused());
      toggle_game = false;
    }
  }

  void on_present() noexcept override { ++present_count; }

  bool should_quit_requested() noexcept override { return false; }

  void on_throttle_wait(double) override {}

  std::shared_ptr<clock_node> game_clock;
  bool toggle_game = false;
  int present_count = 0;
};

} // namespace

TEST_CASE("three stages can pause game while ui and debug clocks keep running", "[render-core][stage-integration]") {
  test_pipeline pipeline;
  pipeline.game_clock = pipeline.clocks().ensure("game");
  auto ui_clock = pipeline.clocks().ensure("ui");
  auto debug_clock = pipeline.clocks().ensure("debug");
  REQUIRE(pipeline.game_clock != nullptr);
  REQUIRE(ui_clock != nullptr);
  REQUIRE(debug_clock != nullptr);

  std::vector<std::string_view> render_order;
  auto game = std::make_unique<recording_stage>("game", true, &render_order);
  auto ui = std::make_unique<recording_stage>("ui", false, &render_order);
  auto debug = std::make_unique<recording_stage>("debug", false, &render_order);
  recording_stage* game_ptr = game.get();
  recording_stage* ui_ptr = ui.get();
  recording_stage* debug_ptr = debug.get();

  REQUIRE(
      pipeline
          .add_stage(
              "game", std::move(game), pipeline.game_clock, frame_stage_options{ .update_order = 0, .render_order = 0 })
          .has_value());
  REQUIRE(
      pipeline.add_stage("ui", std::move(ui), ui_clock, frame_stage_options{ .update_order = 10, .render_order = 10 })
          .has_value());
  REQUIRE(pipeline
              .add_stage(
                  "debug", std::move(debug), debug_clock, frame_stage_options{ .update_order = 20, .render_order = 20 })
              .has_value());

  REQUIRE(pipeline.step_frame(0.1));
  REQUIRE(game_ptr->updates > 0);
  REQUIRE(ui_ptr->updates == 1);
  REQUIRE(debug_ptr->updates == 1);
  REQUIRE(pipeline.present_count == 1);
  REQUIRE(render_order == std::vector<std::string_view>{ "game", "ui", "debug" });

  pipeline.toggle_game = true; // ESC opens the UI
  REQUIRE(pipeline.step_frame(0.1));

  const int game_updates_before_pause = game_ptr->updates;
  REQUIRE(pipeline.game_clock->is_paused());
  REQUIRE(ui_ptr->updates == 2);
  REQUIRE(debug_ptr->updates == 2);
  REQUIRE(game_ptr->renders == 2);
  REQUIRE(ui_ptr->renders == 2);
  REQUIRE(debug_ptr->renders == 2);
  REQUIRE(pipeline.present_count == 2);
  REQUIRE(game_ptr->last_render_dt == 0.0);
  REQUIRE(render_order == std::vector<std::string_view>{ "game", "ui", "debug", "game", "ui", "debug" });

  REQUIRE(pipeline.step_frame(0.1));
  REQUIRE(game_ptr->updates == game_updates_before_pause);
  REQUIRE(ui_ptr->updates == 3);
  REQUIRE(debug_ptr->updates == 3);

  pipeline.toggle_game = true; // ESC closes the UI
  REQUIRE(pipeline.step_frame(0.1));
  REQUIRE_FALSE(pipeline.game_clock->is_paused());
  REQUIRE(game_ptr->updates > game_updates_before_pause);
}
