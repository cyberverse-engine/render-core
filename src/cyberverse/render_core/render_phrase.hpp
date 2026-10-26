/**
 * @file render_phrase.hpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief Render phrase
 * @version 0.1
 * @date 2026-10-23
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include <span>
#include <string_view>

#include "cyberverse/render_core/frame_context.hpp"
#include "cyberverse/render_core/render_packet.hpp"

namespace cyberverse {
namespace render_core {

/**
 * @brief Sort order applied to the render packets of a phrase
 *
 */
enum class phrase_sort {
  none,          ///< Keep submission order
  front_to_back, ///< Opaque geometry, early-z friendly
  back_to_front, ///< Transparent geometry
};

/**
 * @brief A render phrase (a batched rendering pass)
 *
 * A phrase binds a render target set and a pipeline state, then submits a batch
 * of render packets. The phrase owns no resources: the pipeline state, shader
 * resource bindings, buffers, and targets are supplied by the caller. This
 * keeps the phrase reusable across frames and makes it cheap to test the
 * scheduling layer with fake phrases.
 *
 * begin() / execute() / end() must be called in order, and all phrase calls for
 * a frame are single-threaded.
 *
 */
class render_phrase {
public:
  render_phrase() = default;

  virtual ~render_phrase() = default;

  render_phrase(const render_phrase&) = delete;
  render_phrase(render_phrase&&) = delete;

  render_phrase& operator=(const render_phrase&) = delete;
  render_phrase& operator=(render_phrase&&) = delete;

  /**
   * @brief Stable phrase name
   *
   * @return std::string_view
   */
  [[nodiscard]] virtual std::string_view name() const noexcept = 0;

  /**
   * @brief Sort order applied before execute()
   *
   * @return phrase_sort
   */
  [[nodiscard]] virtual phrase_sort sort_mode() const noexcept { return phrase_sort::none; }

  /**
   * @brief Whether the phrase should run this frame
   *
   * A disabled phrase is skipped entirely: no begin, execute, or end call.
   *
   * @return true
   * @return false
   */
  [[nodiscard]] virtual bool enabled() const noexcept { return true; }

  /**
   * @brief Bind render targets and viewport
   *
   * @param ctx Frame context
   */
  virtual void begin(const frame_context& ctx) noexcept = 0;

  /**
   * @brief Bind pipeline state and submit render packets
   *
   * Items are already sorted according to sort_mode().
   *
   * @param ctx Frame context
   * @param items Render packets
   */
  virtual void execute(const frame_context& ctx, std::span<const render_packet> items) noexcept = 0;

  /**
   * @brief Finish the phrase
   *
   * @param ctx Frame context
   */
  virtual void end(const frame_context& ctx) noexcept = 0;
};

/**
 * @brief Human-readable phrase sort name
 *
 * @param sort Sort order
 * @return std::string_view
 */
[[nodiscard]] constexpr std::string_view to_string(phrase_sort sort) noexcept {
  switch (sort) {
  case phrase_sort::front_to_back:
    return "front_to_back";
  case phrase_sort::back_to_front:
    return "back_to_front";
  case phrase_sort::none:
  default:
    return "none";
  }
}

} // namespace render_core
} // namespace cyberverse
