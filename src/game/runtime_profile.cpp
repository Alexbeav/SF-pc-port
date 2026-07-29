#include "sf/game/runtime_profile.hpp"

#include "sf/core/error.hpp"

namespace sf::game {

const GameRuntimeProfile &runtimeProfile(GameId game) {
  switch (game) {
  case GameId::syphon_filter:
    return sf1RuntimeProfile();
  case GameId::syphon_filter_2:
    return sf2RuntimeProfile();
  case GameId::syphon_filter_3:
    return sf3RuntimeProfile();
  }
  throw core::Error{core::ErrorCode::unsupported,
                    "No gameplay runtime is registered for this game"};
}

} // namespace sf::game
