/**
 * @file render_phrase_list.cpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief Ordered render phrase list implementation
 * @version 0.1
 * @date 2026-10-23
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "cyberverse/render_core/render_phrase_list.hpp"

#include <algorithm>
#include <utility>

namespace cyberverse {
namespace render_core {

render_phrase_list::render_phrase_list(std::span<render_phrase* const> phrases) {
  phrases_.reserve(phrases.size());
  for (render_phrase* phrase : phrases) {
    if (phrase != nullptr) {
      phrases_.push_back(phrase);
    }
  }
}

bool render_phrase_list::add(render_phrase* phrase) {
  if (phrase == nullptr || contains(phrase)) {
    return false;
  }
  phrases_.push_back(phrase);
  return true;
}

bool render_phrase_list::remove(render_phrase* phrase) noexcept {
  const auto it = std::find(phrases_.begin(), phrases_.end(), phrase);
  if (it == phrases_.end()) {
    return false;
  }
  phrases_.erase(it);
  return true;
}

bool render_phrase_list::contains(const render_phrase* phrase) const noexcept {
  return std::find(phrases_.begin(), phrases_.end(), phrase) != phrases_.end();
}

void render_phrase_list::reserve(std::size_t phrase_capacity, std::size_t batch_capacity) {
  phrases_.reserve(phrase_capacity);
  scratch_.reserve(batch_capacity);
}

render_phrase_list_stats render_phrase_list::execute(const frame_context& ctx, std::span<const render_packet> items) {
  return execute(ctx, items, runtime::frame_budget::unlimited());
}

render_phrase_list_stats render_phrase_list::execute(const frame_context& ctx,
                                                     std::span<const render_packet> items,
                                                     const runtime::frame_budget& budget) {
  render_phrase_list_stats stats;
  stats.phrase_count = phrases_.size();

  for (render_phrase* phrase : phrases_) {
    if (phrase == nullptr || !phrase->enabled()) {
      continue;
    }
    if (!budget.has_remaining()) {
      stats.budget_exhausted = true;
      ++stats.budget_skipped_phrases;
      continue;
    }

    std::span<const render_packet> batch = items;
    const phrase_sort sort = phrase->sort_mode();
    if (sort != phrase_sort::none && items.size() > 1) {
      scratch_.assign(items.begin(), items.end());
      if (sort == phrase_sort::front_to_back) {
        std::sort(scratch_.begin(), scratch_.end(), render_packet_front_to_back{});
      } else {
        std::sort(scratch_.begin(), scratch_.end(), render_packet_back_to_front{});
      }
      batch = scratch_;
    }

    phrase->begin(ctx);
    phrase->execute(ctx, batch);
    phrase->end(ctx);

    ++stats.executed_phrases;
    stats.items_submitted += batch.size();
  }

  return stats;
}

} // namespace render_core
} // namespace cyberverse
