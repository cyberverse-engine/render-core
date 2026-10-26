/**
 * @file render_phrase_list.hpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief Ordered render phrase list
 * @version 0.1
 * @date 2026-10-23
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "cyberverse/render_core/render_phrase.hpp"
#include "cyberverse/runtime/frame_budget.hpp"

namespace cyberverse {
namespace render_core {

/**
 * @brief Result of one render_phrase_list execution
 *
 */
struct render_phrase_list_stats {
  std::size_t phrase_count = 0;           ///< Registered phrases, including disabled
  std::size_t executed_phrases = 0;       ///< Enabled phrases that ran
  std::size_t items_submitted = 0;        ///< Sum of item counts across executed phrases
  std::size_t budget_skipped_phrases = 0; ///< Enabled phrases skipped because the budget was exhausted
  bool budget_exhausted = false;          ///< The budget stopped at least one phrase from starting
};

/**
 * @brief Runs render phrases in registration order
 *
 * The list owns no phrases. Phrases are registered once and reused every frame.
 * Before each phrase executes, the list sorts a reusable scratch copy of the
 * submitted items according to the phrase's sort mode, so a shared draw list can
 * feed an opaque pass and a transparent pass with different orderings.
 *
 * The scratch buffer grows to the largest submitted batch and is reused after
 * that. The list is single-threaded and is not safe for concurrent execute()
 * calls.
 *
 */
class render_phrase_list {
public:
  render_phrase_list() = default;

  /**
   * @brief Construct a list from phrases
   *
   * Null entries are ignored.
   *
   * @param phrases Phrases in execution order
   */
  explicit render_phrase_list(std::span<render_phrase* const> phrases);

  /**
   * @brief Register a phrase
   *
   * @param phrase Phrase. The list does not take ownership.
   * @return true if added, false if null or already registered
   */
  bool add(render_phrase* phrase);

  /**
   * @brief Remove a phrase
   *
   * @param phrase Phrase
   * @return true if found
   */
  bool remove(render_phrase* phrase) noexcept;

  /**
   * @brief Remove all phrases
   *
   */
  void clear() noexcept { phrases_.clear(); }

  /**
   * @brief Whether a phrase is registered
   *
   * @param phrase Phrase
   * @return true
   * @return false
   */
  [[nodiscard]] bool contains(const render_phrase* phrase) const noexcept;

  /**
   * @brief Registered phrases in execution order
   *
   * @return std::span<render_phrase* const>
   */
  [[nodiscard]] std::span<render_phrase* const> phrases() const noexcept { return phrases_; }

  /**
   * @brief Number of registered phrases
   *
   * @return std::size_t
   */
  [[nodiscard]] std::size_t size() const noexcept { return phrases_.size(); }

  /**
   * @brief Whether no phrases are registered
   *
   * @return true
   * @return false
   */
  [[nodiscard]] bool empty() const noexcept { return phrases_.empty(); }

  /**
   * @brief Reserve room for phrases and a maximum batch
   *
   * @param phrase_capacity Number of phrases
   * @param batch_capacity Maximum items per phrase
   */
  void reserve(std::size_t phrase_capacity, std::size_t batch_capacity = 0);

  /**
   * @brief Execute all enabled phrases
   *
   * For each phrase: begin(), sorted execute(), end(). Disabled phrases are
   * skipped without any call. The input order is never modified. The item span
   * passed to a phrase is valid only for the duration of that call; a phrase
   * must consume or copy what it needs before returning.
   *
   * @param ctx Frame context
   * @param items Render packets
   * @return render_phrase_list_stats
   */
  render_phrase_list_stats execute(const frame_context& ctx, std::span<const render_packet> items);

  /**
   * @brief Execute enabled phrases while the frame budget has time
   *
   * Each phrase is executed atomically; the budget is checked only before the
   * next phrase starts. When the budget is exhausted, remaining phrases are
   * skipped and reported in the stats.
   *
   * @param ctx Frame context
   * @param items Render packets
   * @param budget Advisory frame budget
   * @return render_phrase_list_stats
   */
  render_phrase_list_stats
  execute(const frame_context& ctx, std::span<const render_packet> items, const runtime::frame_budget& budget);

private:
  std::vector<render_phrase*> phrases_;
  std::vector<render_packet> scratch_;
};

} // namespace render_core
} // namespace cyberverse
