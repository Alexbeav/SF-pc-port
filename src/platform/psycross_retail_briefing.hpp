#pragma once

#include "sf/platform/player_input.hpp"

#include <cstdint>
#include <memory>

namespace sf::assets {
class MissionBriefing;
}

namespace sf::game {
class MissionPackage;
}

namespace sf::platform::detail {

class PsyCrossRetailBriefing final {
public:
  PsyCrossRetailBriefing(const game::MissionPackage &mission,
                         KeyboardMouseBindings bindings);
  ~PsyCrossRetailBriefing();

  PsyCrossRetailBriefing(const PsyCrossRetailBriefing &) = delete;
  PsyCrossRetailBriefing &operator=(const PsyCrossRetailBriefing &) = delete;

  // Dynamic VRAM writes must complete before PsyX_BeginScene so the scene's
  // first textured split samples the newly authoritative VRAM texture.
  void prepare(double retail_time) const;

  [[nodiscard]] bool draw(const assets::MissionBriefing &briefing,
                          double retail_time) const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace sf::platform::detail
