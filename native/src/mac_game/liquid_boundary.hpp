#pragma once
#include "eos.hpp"
#include <exception>
#include <span>

namespace ma::experimental::game_kernel {
// Call-scoped owner boundary input, never core state or a saved core option.
struct LiquidBoundary {
  std::span<const std::uint8_t> blocked;
  std::span<const Species> species;
};
// No part of the failed candidate may be committed. The caller can close this
// face on the original interval source and repeat the pressure/work solve.
struct LiquidBoundaryFace {
  bool x;
  std::size_t face, donor, target;
};
struct LiquidBoundaryConflict final : std::exception {
  enum class Stage { transport, wet_tick_entry };
  std::vector<LiquidBoundaryFace> faces;
  std::size_t microtick;
  Stage stage;
  LiquidBoundaryConflict(bool axis, std::size_t f, std::size_t from,
                         std::size_t to, std::size_t tick,
                         Stage origin = Stage::transport)
      : faces{{axis, f, from, to}}, microtick(tick), stage(origin) {}
  explicit LiquidBoundaryConflict(std::vector<LiquidBoundaryFace> conflicts)
      : faces(std::move(conflicts)), microtick(0), stage(Stage::wet_tick_entry) {}
  const char *what() const noexcept override {
    return "Liquid crossing an owner membrane";
  }
};
} // namespace ma::experimental::game_kernel
