/**
 * @file render_phrase_list_test.cpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief Render phrase list tests
 * @version 0.1
 * @date 2026-10-23
 *
 * @copyright Copyright (c) 2026
 *
 */
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>
#include <vector>

#include "cyberverse/render_core/render_phrase_list.hpp"
#include "cyberverse/runtime/frame_budget.hpp"
#include "cyberverse/runtime/frame_clock.hpp"

using namespace cyberverse::render_core;

namespace {

struct recording_phrase final : render_phrase {
  recording_phrase(std::string phrase_name, phrase_sort phrase_order, std::vector<std::string>* log = nullptr)
      : phrase_name_{ std::move(phrase_name) }, phrase_order_{ phrase_order }, log_{ log } {}

  std::string_view name() const noexcept override { return phrase_name_; }

  phrase_sort sort_mode() const noexcept override { return phrase_order_; }

  bool enabled() const noexcept override { return enabled_; }

  void begin(const frame_context&) noexcept override {
    ++begin_calls;
    if (log_ != nullptr) {
      log_->push_back(phrase_name_ + ":begin");
    }
  }

  void execute(const frame_context&, std::span<const render_packet> items) noexcept override {
    ++execute_calls;
    if (log_ != nullptr) {
      log_->push_back(phrase_name_ + ":execute");
    }
    keys.clear();
    for (const render_packet& item : items) {
      keys.push_back(item.sort_key);
    }
  }

  void end(const frame_context&) noexcept override {
    ++end_calls;
    if (log_ != nullptr) {
      log_->push_back(phrase_name_ + ":end");
    }
  }

  std::string phrase_name_;
  phrase_sort phrase_order_ = phrase_sort::none;
  std::vector<std::string>* log_ = nullptr;
  bool enabled_ = true;
  int begin_calls = 0;
  int execute_calls = 0;
  int end_calls = 0;
  std::vector<float> keys;
};

render_packet make_item(float key) {
  render_packet item;
  item.sort_key = key;
  item.index_buffer = reinterpret_cast<Diligent::IBuffer*>(0x1000);
  item.index_count = 3;
  return item;
}

} // namespace

TEST_CASE("render_phrase_list registers phrases in order", "[render-core][phrase-list]") {
  recording_phrase a{ "a", phrase_sort::none };
  recording_phrase b{ "b", phrase_sort::none };

  render_phrase_list list;
  REQUIRE(list.empty());
  REQUIRE(list.add(&a));
  REQUIRE(list.add(&b));
  REQUIRE_FALSE(list.add(&a));
  REQUIRE_FALSE(list.add(nullptr));
  REQUIRE(list.size() == 2);
  REQUIRE(list.contains(&a));
  REQUIRE(list.phrases()[0] == &a);
  REQUIRE(list.phrases()[1] == &b);

  REQUIRE(list.remove(&a));
  REQUIRE_FALSE(list.remove(&a));
  REQUIRE_FALSE(list.contains(&a));
  REQUIRE(list.size() == 1);
}

TEST_CASE("render_phrase_list constructs from a span and skips nulls", "[render-core][phrase-list]") {
  recording_phrase a{ "a", phrase_sort::none };
  recording_phrase b{ "b", phrase_sort::none };
  render_phrase* phrases[] = { &a, nullptr, &b };
  render_phrase_list list{ phrases };
  REQUIRE(list.size() == 2);
  REQUIRE(list.contains(&a));
  REQUIRE(list.contains(&b));
}

TEST_CASE("render_phrase_list runs begin, execute, end in order", "[render-core][phrase-list]") {
  std::vector<std::string> log;
  recording_phrase a{ "a", phrase_sort::none, &log };
  recording_phrase b{ "b", phrase_sort::none, &log };

  render_phrase_list list;
  list.add(&a);
  list.add(&b);

  const std::vector<render_packet> items{ make_item(1.0f) };
  const render_phrase_list_stats stats = list.execute(frame_context{}, items);

  REQUIRE(stats.phrase_count == 2);
  REQUIRE(stats.executed_phrases == 2);
  REQUIRE(stats.items_submitted == 2);
  REQUIRE(a.begin_calls == 1);
  REQUIRE(a.execute_calls == 1);
  REQUIRE(a.end_calls == 1);
  REQUIRE(b.begin_calls == 1);
  REQUIRE(log == std::vector<std::string>{ "a:begin", "a:execute", "a:end", "b:begin", "b:execute", "b:end" });
}

TEST_CASE("render_phrase_list sorts per phrase without mutating the input", "[render-core][phrase-list]") {
  recording_phrase opaque{ "opaque", phrase_sort::front_to_back };
  recording_phrase transparent{ "transparent", phrase_sort::back_to_front };
  recording_phrase submission{ "submission", phrase_sort::none };

  render_phrase_list list;
  list.add(&opaque);
  list.add(&transparent);
  list.add(&submission);

  const std::vector<render_packet> items{ make_item(3.0f), make_item(1.0f), make_item(2.0f) };
  list.execute(frame_context{}, items);

  REQUIRE(opaque.keys == std::vector<float>{ 1.0f, 2.0f, 3.0f });
  REQUIRE(transparent.keys == std::vector<float>{ 3.0f, 2.0f, 1.0f });
  REQUIRE(submission.keys == std::vector<float>{ 3.0f, 1.0f, 2.0f });

  REQUIRE(items.at(0).sort_key == 3.0f);
  REQUIRE(items.at(1).sort_key == 1.0f);
  REQUIRE(items.at(2).sort_key == 2.0f);
}

TEST_CASE("render_phrase_list skips disabled phrases", "[render-core][phrase-list]") {
  recording_phrase enabled{ "enabled", phrase_sort::none };
  recording_phrase disabled{ "disabled", phrase_sort::none };
  disabled.enabled_ = false;

  render_phrase_list list;
  list.add(&enabled);
  list.add(&disabled);

  const std::vector<render_packet> items{ make_item(1.0f) };
  const render_phrase_list_stats stats = list.execute(frame_context{}, items);

  REQUIRE(stats.phrase_count == 2);
  REQUIRE(stats.executed_phrases == 1);
  REQUIRE(stats.items_submitted == 1);
  REQUIRE(disabled.begin_calls == 0);
  REQUIRE(disabled.execute_calls == 0);
  REQUIRE(disabled.end_calls == 0);
}

class phrase_list_clock final : public cyberverse::runtime::frame_clock {
public:
  double now() noexcept override { return current; }
  void sleep_until(double) override {}
  double current = 0.0;
};

TEST_CASE("render_phrase_list stops before the next phrase when the budget is exhausted",
          "[render-core][phrase-list]") {
  recording_phrase first{ "first", phrase_sort::none };
  recording_phrase second{ "second", phrase_sort::none };
  render_phrase_list list;
  list.add(&first);
  list.add(&second);

  phrase_list_clock clock;
  const cyberverse::runtime::frame_budget exhausted{ 0.0, clock };
  const std::vector<render_packet> items{ make_item(1.0f) };
  const render_phrase_list_stats stats = list.execute(frame_context{}, items, exhausted);

  REQUIRE(stats.phrase_count == 2);
  REQUIRE(stats.executed_phrases == 0);
  REQUIRE(stats.budget_skipped_phrases == 2);
  REQUIRE(stats.budget_exhausted);
  REQUIRE(first.begin_calls == 0);
  REQUIRE(second.begin_calls == 0);
}

TEST_CASE("render_phrase_list handles empty input", "[render-core][phrase-list]") {
  recording_phrase phrase{ "phrase", phrase_sort::front_to_back };
  render_phrase_list list;
  list.add(&phrase);

  const render_phrase_list_stats stats = list.execute(frame_context{}, {});
  REQUIRE(stats.executed_phrases == 1);
  REQUIRE(stats.items_submitted == 0);
  REQUIRE(phrase.execute_calls == 1);
  REQUIRE(phrase.keys.empty());
}

TEST_CASE("render_phrase_list reuses its scratch buffer", "[render-core][phrase-list]") {
  recording_phrase phrase{ "phrase", phrase_sort::front_to_back };
  render_phrase_list list;
  list.add(&phrase);
  list.reserve(1, 16);

  for (int index = 0; index < 3; ++index) {
    const std::vector<render_packet> items{ make_item(3.0f), make_item(1.0f), make_item(2.0f) };
    list.execute(frame_context{}, items);
    REQUIRE(phrase.keys == std::vector<float>{ 1.0f, 2.0f, 3.0f });
  }
  REQUIRE(phrase.execute_calls == 3);
}

TEST_CASE("render_phrase_list clear removes all phrases", "[render-core][phrase-list]") {
  recording_phrase phrase{ "phrase", phrase_sort::none };
  render_phrase_list list;
  list.add(&phrase);
  list.clear();
  REQUIRE(list.empty());
  REQUIRE(list.phrases().empty());

  const render_phrase_list_stats stats = list.execute(frame_context{}, {});
  REQUIRE(stats.phrase_count == 0);
  REQUIRE(phrase.begin_calls == 0);
}
