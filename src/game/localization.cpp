#include "sf/game/localization.hpp"

#include <atomic>
#include <utility>

namespace sf::game {
namespace {

std::atomic language{GameLanguage::english};
std::filesystem::path pack_root;

} // namespace

void setGameLanguage(GameLanguage) noexcept {
  // The public source-only build intentionally ships English support only.
  language.store(GameLanguage::english);
}

GameLanguage gameLanguage() noexcept { return language.load(); }

bool russianLanguageActive() noexcept { return false; }

void setLocalizationRoot(std::filesystem::path root) {
  pack_root = std::move(root);
}

const std::filesystem::path &localizationRoot() noexcept { return pack_root; }

bool localizationPackAvailable(GameLanguage value) noexcept {
  return value == GameLanguage::english;
}

std::optional<std::vector<std::byte>>
readLocalizedAsset(std::string_view) noexcept {
  return std::nullopt;
}

std::string_view localizeText(std::string_view english) noexcept {
  return english;
}

std::string localizeTextCopy(std::string_view english) {
  return std::string{english};
}

std::optional<std::string_view>
completeGameplayTextSource(std::string_view) noexcept {
  return std::nullopt;
}

std::optional<LocalizedMissionBriefing>
localizedMissionBriefing(std::uint32_t) noexcept {
  return std::nullopt;
}

std::optional<LocalizedMissionMenuTexts>
localizedMissionMenuTexts(std::uint32_t, std::span<const std::string>,
                          std::span<const std::string>) noexcept {
  return std::nullopt;
}

} // namespace sf::game
