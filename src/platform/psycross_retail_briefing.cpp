#include "psycross_retail_briefing.hpp"
#include "psycross_font_texture.hpp"

#include "sf/assets/mission_briefing.hpp"
#include "sf/assets/tim_image.hpp"
#include "sf/core/error.hpp"
#include "sf/game/hud.hpp"
#include "sf/game/localization.hpp"
#include "sf/game/mission.hpp"
#include "sf/game/mission_start.hpp"

#include <PsyX/PsyX_render.h>
#include <psx/libgpu.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sf::platform::detail {
namespace {

constexpr int screen_width = 384;
constexpr int screen_height = 240;
constexpr int screen_center_x = screen_width / 2;
constexpr int screen_center_y = screen_height / 2;
constexpr int retail_line_height = 8;
constexpr std::uint32_t surround_transition_ticks = 12U;
constexpr std::uint32_t surround_frame_elements = 21U;
constexpr std::uint32_t surround_grid_lines = 43U;

// FUN_80084698 installs the authored 384x240 briefing surround before
// INIT.OVL creates its text region. The surround combines live-generated
// texture strips, line geometry and a scan-line gauge; it is not a static TIM
// in INTRFACE.HOG. Keep it in the same UI space as the retail text objects.
struct Point {
  int x{};
  int y{};
};

struct Rgb {
  std::uint8_t red{};
  std::uint8_t green{};
  std::uint8_t blue{};
};

struct LineSegment {
  Point first;
  Point second;
};

struct FrameStrip {
  Point points[4U];
  std::uint8_t u[4U];
  std::uint8_t v[4U];
};

constexpr Rgb panel_outline{90U, 100U, 180U};
constexpr Rgb panel_grid{48U, 44U, 92U};
constexpr Rgb panel_progress{87U, 175U, 230U};

// Convex native fallback for the concave briefing surround.  Retail's packet
// vertices below are preserved as the effect overlay, but their GP0 winding
// is not the ordering expected by PsyCross's setXY4 helper.  These spans are
// the already-proven presentation base and must remain visible independently
// of the animated texture path.
constexpr std::array outer_panel{
    Point{31, 7},    Point{353, 7},   Point{365, 20},  Point{365, 190},
    Point{353, 203}, Point{280, 203}, Point{270, 214}, Point{114, 214},
    Point{104, 203}, Point{31, 203},  Point{19, 190},  Point{19, 20},
};

constexpr std::array inner_panel{
    Point{40, 21},   Point{344, 21},  Point{355, 32},  Point{355, 181},
    Point{344, 193}, Point{276, 193}, Point{268, 201}, Point{116, 201},
    Point{108, 193}, Point{40, 193},  Point{29, 181},  Point{29, 32},
};

// Stable state-8 INIT.OVL captures publish these 21 textured strips. The
// coordinates are the authored 384x240 positions after E5 draw offset. Each
// strip samples the same live 10x32 effect surface, rotating its UVs at the
// frame's corners exactly as the retail packet stream does.
constexpr std::array<FrameStrip, surround_frame_elements> retail_frame_strips{{
    {{{188, 209}, {200, 197}, {179, 193}, {195, 185}}, {9, 9, 0, 0}, {0, 31, 0, 31}},
    {{{195, 185}, {200, 197}, {247, 185}, {252, 197}}, {0, 9, 0, 9}, {0, 0, 31, 31}},
    {{{247, 185}, {252, 197}, {299, 185}, {302, 197}}, {0, 9, 0, 9}, {0, 0, 31, 31}},
    {{{299, 185}, {302, 197}, {351, 185}, {352, 197}}, {0, 9, 0, 9}, {0, 0, 31, 31}},
    {{{352, 197}, {364, 185}, {351, 185}, {359, 177}}, {9, 9, 0, 0}, {0, 31, 0, 31}},
    {{{359, 177}, {364, 185}, {359, 110}, {364, 100}}, {0, 9, 0, 9}, {0, 0, 31, 31}},
    {{{359, 110}, {364, 100}, {359, 36}, {364, 25}}, {0, 9, 0, 9}, {0, 0, 31, 31}},
    {{{359, 36}, {364, 25}, {350, 27}, {354, 15}}, {0, 9, 0, 9}, {0, 0, 31, 31}},
    {{{354, 15}, {288, 15}, {350, 27}, {292, 27}}, {9, 9, 0, 0}, {0, 31, 0, 31}},
    {{{292, 27}, {288, 15}, {250, 27}, {240, 15}}, {0, 9, 0, 9}, {0, 0, 31, 31}},
    {{{250, 27}, {240, 15}, {192, 27}, {192, 15}}, {0, 9, 0, 9}, {0, 0, 31, 31}},
    {{{192, 15}, {144, 15}, {192, 27}, {134, 27}}, {9, 9, 0, 0}, {0, 31, 0, 31}},
    {{{144, 15}, {96, 15}, {134, 27}, {92, 27}}, {9, 9, 0, 0}, {0, 31, 0, 31}},
    {{{96, 15}, {30, 15}, {92, 27}, {34, 27}}, {9, 9, 0, 0}, {0, 31, 0, 31}},
    {{{30, 15}, {20, 25}, {34, 27}, {25, 36}}, {9, 9, 0, 0}, {0, 31, 0, 31}},
    {{{20, 25}, {20, 185}, {25, 36}, {25, 177}}, {9, 9, 0, 0}, {0, 31, 0, 31}},
    {{{25, 177}, {20, 185}, {33, 185}, {32, 197}}, {0, 9, 0, 9}, {0, 0, 31, 31}},
    {{{33, 185}, {32, 197}, {49, 193}, {56, 209}}, {0, 9, 0, 9}, {0, 0, 31, 31}},
    {{{49, 193}, {56, 209}, {89, 193}, {98, 209}}, {0, 9, 0, 9}, {0, 0, 31, 31}},
    {{{89, 193}, {98, 209}, {132, 193}, {138, 209}}, {0, 9, 0, 9}, {0, 0, 31, 31}},
    {{{132, 193}, {138, 209}, {179, 193}, {188, 209}}, {0, 9, 0, 9}, {0, 0, 31, 31}},
}};

constexpr Rgb briefing_color{
    assets::RetailBriefingLayout::red,
    assets::RetailBriefingLayout::green,
    assets::RetailBriefingLayout::blue,
};

struct BriefingTexture {
  std::string name;
  assets::TimImage image;
};

int texturePageMode(assets::TimPixelMode mode) {
  switch (mode) {
  case assets::TimPixelMode::indexed4:
    return 0;
  case assets::TimPixelMode::indexed8:
    return 1;
  case assets::TimPixelMode::direct16:
    return 2;
  case assets::TimPixelMode::direct24:
    return 3;
  }
  return 2;
}

void uploadTimBlock(const assets::TimBlock &block) {
  const auto checked = [](std::uint16_t value) {
    if (value > static_cast<std::uint16_t>(std::numeric_limits<short>::max())) {
      throw core::Error{core::ErrorCode::unsupported,
                        "Briefing TIM coordinate exceeds PsyCross range"};
    }
    return static_cast<short>(value);
  };
  RECT16 rect{checked(block.x), checked(block.y), checked(block.width_words),
              checked(block.height)};
  std::vector<u_long> packed((block.words.size() + 1U) / 2U);
  for (std::size_t index = 0U; index < block.words.size(); ++index) {
    packed[index / 2U] |= static_cast<u_long>(block.words[index])
                          << ((index & 1U) * 16U);
  }
  LoadImage(&rect, packed.data());
}

void drawSolidRect(int x, int y, int width, int height, Rgb color) {
  TILE tile{};
  setTile(&tile);
  setRGB0(&tile, color.red, color.green, color.blue);
  setXY0(&tile, static_cast<float>(x), static_cast<float>(y));
  setWH(&tile, static_cast<float>(width), static_cast<float>(height));
  DrawPrim(&tile);
}

void drawSolidQuad(Point top_left, Point top_right, Point bottom_left,
                   Point bottom_right, Rgb color) {
  POLY_F4 polygon{};
  setPolyF4(&polygon);
  setRGB0(&polygon, color.red, color.green, color.blue);
  setXY4(&polygon, static_cast<float>(top_left.x),
         static_cast<float>(top_left.y), static_cast<float>(top_right.x),
         static_cast<float>(top_right.y), static_cast<float>(bottom_left.x),
         static_cast<float>(bottom_left.y), static_cast<float>(bottom_right.x),
         static_cast<float>(bottom_right.y));
  DrawPrim(&polygon);
}

// Retail state 8 selects TPAGE 0x9c (8-bit, x=768, y=256), samples UV
// (0..9,160..191), and uses CLUT 0x7fc0 (x=0,y=511).  The corresponding A0
// transfer is therefore five 16-bit words by 32 rows at VRAM (768,416).
constexpr short frame_texture_x = 768;
constexpr short frame_texture_y = 416;
constexpr short frame_texture_pixel_width = 10;
constexpr short frame_texture_word_width = 5;
constexpr short frame_texture_height = 32;
constexpr short frame_palette_x = 0;
constexpr short frame_palette_y = 511;

std::uint16_t packFrameColor(int intensity) noexcept {
  intensity = std::clamp(intensity, 0, 255);
  int red{};
  int green{};
  int blue{};
  if (intensity <= 64) {
    red = intensity;
    green = intensity * 74 / 64;
    blue = intensity * 164 / 64;
  } else if (intensity <= 160) {
    red = 65;
    green = 74 + (intensity - 64) * 90 / 96;
    blue = 164 - (intensity - 64) * 90 / 96;
  } else if (intensity <= 192) {
    red = 65 + (intensity - 160) * 99 / 32;
    green = 164;
    blue = 74;
  } else {
    red = 164 + (intensity - 192) * 91 / 63;
    green = red;
    blue = 74 - (intensity - 192) * 74 / 63;
  }
  return static_cast<std::uint16_t>((red >> 3U) | ((green >> 3U) << 5U) |
                                    ((blue >> 3U) << 10U));
}

struct BriefingFrameTexture {
  int page{};
  int clut{};
};

BriefingFrameTexture uploadBriefingFrameTexture(std::uint32_t retail_tick) {
  std::array<std::uint16_t, 256U> palette{};
  for (auto index = std::size_t{}; index < palette.size(); ++index) {
    palette[index] = packFrameColor(static_cast<int>(index));
  }
  std::array<u_long, palette.size() / 2U> packed_palette{};
  for (std::size_t index = 0; index < palette.size(); ++index) {
    packed_palette[index / 2U] |=
        static_cast<u_long>(palette[index]) << ((index & 1U) * 16U);
  }
  RECT16 palette_rect{frame_palette_x, frame_palette_y, 256, 1};
  LoadImage(&palette_rect, packed_palette.data());

  std::array<std::uint16_t,
             static_cast<std::size_t>(frame_texture_word_width) *
                 frame_texture_height>
      indexed_words{};
  const auto time = static_cast<double>(retail_tick);
  for (auto y = 0; y < frame_texture_height; ++y) {
    for (auto x = 0; x < frame_texture_pixel_width; ++x) {
      // INIT.OVL refreshes a tiny indexed plasma/noise surface every frame and
      // bends it around the frame. Recreate that observed signal rather than
      // treating the surround as flat or vertex-shaded geometry.
      const auto first = std::sin(static_cast<double>(x) * 0.82 +
                                  static_cast<double>(y) * 0.18 + time * 0.17);
      const auto second = std::sin(static_cast<double>(x) * 1.91 -
                                   static_cast<double>(y) * 0.25 - time * 0.11);
      const auto third = std::sin(static_cast<double>(y) * 0.53 + time * 0.07);
      const auto intensity = static_cast<int>(
          std::lround(62.0 + first * 28.0 + second * 19.0 + third * 15.0));
      const auto palette_index = static_cast<std::uint8_t>(
          std::clamp(intensity * 2, 0, 255));
      auto &word = indexed_words[static_cast<std::size_t>(y) *
                                     frame_texture_word_width +
                                 static_cast<std::size_t>(x / 2)];
      word |= static_cast<std::uint16_t>(palette_index)
              << static_cast<unsigned int>((x & 1) * 8);
    }
  }
  RECT16 rect{frame_texture_x, frame_texture_y, frame_texture_word_width,
              frame_texture_height};
  std::array<u_long, indexed_words.size() / 2U> packed{};
  for (std::size_t index = 0; index < indexed_words.size(); ++index) {
    packed[index / 2U] |=
        static_cast<u_long>(indexed_words[index]) << ((index & 1U) * 16U);
  }
  LoadImage(&rect, packed.data());
  DrawSync(0);
  return {GetTPage(1, 0, frame_texture_x, 256),
          GetClut(frame_palette_x, frame_palette_y)};
}

void drawBriefingFrameStrip(const FrameStrip &strip,
                            BriefingFrameTexture texture,
                            bool semi_transparent = false,
                            Rgb color = {128U, 128U, 128U}) {
  POLY_FT4 polygon{};
  setPolyFT4(&polygon);
  setSemiTrans(&polygon, semi_transparent ? 1 : 0);
  setRGB0(&polygon, color.red, color.green, color.blue);
  polygon.tpage = static_cast<u_short>(texture.page);
  polygon.clut = static_cast<u_short>(texture.clut);
  setXY4(&polygon, static_cast<float>(strip.points[0].x),
         static_cast<float>(strip.points[0].y),
         static_cast<float>(strip.points[1].x),
         static_cast<float>(strip.points[1].y),
         static_cast<float>(strip.points[2].x),
         static_cast<float>(strip.points[2].y),
         static_cast<float>(strip.points[3].x),
         static_cast<float>(strip.points[3].y));
  setUV4(&polygon, strip.u[0], static_cast<u_char>(strip.v[0] + 160U),
         strip.u[1], static_cast<u_char>(strip.v[1] + 160U), strip.u[2],
         static_cast<u_char>(strip.v[2] + 160U), strip.u[3],
         static_cast<u_char>(strip.v[3] + 160U));
  DrawPrim(&polygon);
}

void drawLine(Point first, Point second, Rgb color) {
  LINE_F2 line{};
  setLineF2(&line);
  setRGB0(&line, color.red, color.green, color.blue);
  setXY2(&line, static_cast<float>(first.x), static_cast<float>(first.y),
         static_cast<float>(second.x), static_cast<float>(second.y));
  DrawPrim(&line);
}

template <std::size_t Size>
void drawOutlinePrefix(const std::array<Point, Size> &points,
                       std::size_t visible_segments, Rgb color) {
  visible_segments = std::min(visible_segments, points.size());
  for (std::size_t index = 0U; index < visible_segments; ++index) {
    drawLine(points[index], points[(index + 1U) % points.size()], color);
  }
}

void drawBriefingSurround(std::uint32_t retail_tick,
                          double animation_progress) {
  const auto transition_tick =
      std::min(retail_tick, surround_transition_ticks);
  const auto visible_frame = static_cast<std::size_t>(
      transition_tick * surround_frame_elements / surround_transition_ticks);
  const auto visible_grid = static_cast<std::size_t>(
      transition_tick * surround_grid_lines / surround_transition_ticks);

  if (visible_frame != 0U) {
    const auto shimmer = static_cast<int>(std::lround(
        10.0 * std::sin(static_cast<double>(retail_tick) * 0.13)));
    const auto fill = Rgb{
        static_cast<std::uint8_t>(std::clamp(14 + shimmer / 4, 0, 255)),
        static_cast<std::uint8_t>(std::clamp(22 + shimmer / 2, 0, 255)),
        static_cast<std::uint8_t>(std::clamp(70 + shimmer, 0, 255))};

    // Lay down the concave silhouette as four non-self-intersecting spans.
    drawSolidQuad({31, 7}, {353, 7}, {19, 20}, {365, 20}, fill);
    drawSolidRect(19, 20, 346, 170, fill);
    drawSolidQuad({19, 190}, {365, 190}, {31, 203}, {353, 203}, fill);
    drawSolidQuad({104, 203}, {280, 203}, {114, 214}, {270, 214}, fill);

    // Restore the display aperture after filling the surround.
    drawSolidQuad({40, 21}, {344, 21}, {29, 32}, {355, 32}, {});
    drawSolidRect(29, 32, 326, 149, {});
    drawSolidQuad({29, 181}, {355, 181}, {40, 193}, {344, 193}, {});
    drawSolidQuad({108, 193}, {276, 193}, {116, 201}, {268, 201}, {});

    const auto visible_outline =
        visible_frame * (outer_panel.size() + inner_panel.size()) /
        surround_frame_elements;
    const auto visible_outer = std::min(visible_outline, outer_panel.size());
    const auto visible_inner = visible_outline > outer_panel.size()
                                   ? visible_outline - outer_panel.size()
                                   : 0U;
    drawOutlinePrefix(outer_panel, visible_outer, panel_outline);
    drawOutlinePrefix(inner_panel, visible_inner, panel_outline);
  }

  // The settled retail packet stream owns 27 vertical and 17 horizontal
  // lines, followed by both edges of every textured frame strip.
  std::array<LineSegment, surround_grid_lines> grid{};
  auto grid_index = std::size_t{};
  for (auto index = 0; index < 27; ++index) {
    const auto x = 36 + index * 12;
    const auto bottom = index >= 9 && index < 18 ? 203 : 190;
    grid[grid_index++] = {{x, 27}, {x, bottom}};
  }
  for (auto index = 0; index < 16; ++index) {
    const auto y = 32 + index * 10;
    grid[grid_index++] = {{25, y}, {359, y}};
  }
  for (std::size_t index = 0U; index < visible_grid; ++index) {
    drawLine(grid[index].first, grid[index].second, panel_grid);
  }
  if (visible_grid < grid.size()) {
    drawLine(grid[visible_grid].first, grid[visible_grid].second,
             panel_progress);
  }

  // Draw the textured surround after the grid.  Retail OT ordering lets the
  // inner frame edge mask the ends of grid lines; reversing those layers is
  // what made the native grid visibly pierce the border.
  const auto frame_texture = BriefingFrameTexture{
      GetTPage(1, 0, frame_texture_x, 256),
      GetClut(frame_palette_x, frame_palette_y)};
  DR_TPAGE page{};
  SetDrawTPage(&page, 0, 0, frame_texture.page);
  DrawPrim(&page);
  for (std::size_t index = 0; index < visible_frame; ++index) {
    drawBriefingFrameStrip(retail_frame_strips[index], frame_texture);
  }

  for (std::size_t index = 0; index < visible_frame; ++index) {
    const auto &strip = retail_frame_strips[index];
    const auto color = index + 1U == visible_frame &&
                               visible_frame < retail_frame_strips.size()
                           ? panel_progress
                           : panel_outline;
    drawLine(strip.points[0], strip.points[2], color);
    drawLine(strip.points[1], strip.points[3], color);
  }

  // Retail's permanent lower gauge is another sample of the live frame
  // texture with five semi-transparent scan lines over it.
  constexpr FrameStrip gauge{{{63, 197}, {63, 206}, {170, 197}, {170, 206}},
                             {0, 9, 0, 9},
                             {0, 0, 31, 31}};
  drawSolidQuad(gauge.points[0], gauge.points[1], gauge.points[2],
                gauge.points[3], {18U, 31U, 72U});
  drawBriefingFrameStrip(gauge, frame_texture, true, {64U, 64U, 64U});
  const auto progress_width = static_cast<int>(std::lround(
      std::clamp(animation_progress, 0.0, 1.0) * 100.0));
  if (progress_width > 0) {
    constexpr std::array<Rgb, 5U> gauge_scanline_colors{{
        {20U, 60U, 100U}, {45U, 111U, 100U}, {44U, 108U, 100U},
        {40U, 100U, 100U}, {20U, 60U, 100U}}};
    for (auto row = 0; row < 5; ++row) {
      drawLine({66, 199 + row}, {66 + progress_width, 199 + row},
               gauge_scanline_colors[static_cast<std::size_t>(row)]);
    }
  }
}

void drawTextureRegion(const assets::TimImage &page_image, int x, int y,
                       std::uint8_t source_u, std::uint8_t source_v,
                       std::uint8_t width, std::uint8_t height, Rgb color) {
  const auto &pixels = page_image.pixels();
  const auto page_x =
      static_cast<int>(pixels.x & static_cast<std::uint16_t>(~63U));
  const auto page_y =
      static_cast<int>(pixels.y & static_cast<std::uint16_t>(~255U));
  const auto texture_page =
      GetTPage(texturePageMode(page_image.mode()), 0, page_x, page_y);

  DR_TPAGE page{};
  SetDrawTPage(&page, 1, 0, texture_page);
  DrawPrim(&page);

  POLY_FT4 polygon{};
  setPolyFT4(&polygon);
  polygon.tpage = texture_page;
  polygon.clut = GetClut(page_image.clut()->x, page_image.clut()->y);
  setRGB0(&polygon, color.red, color.green, color.blue);
  setXY4(&polygon, static_cast<float>(x), static_cast<float>(y),
         static_cast<float>(x + width), static_cast<float>(y),
         static_cast<float>(x), static_cast<float>(y + height),
         static_cast<float>(x + width), static_cast<float>(y + height));
  setUV4(&polygon, source_u, source_v, static_cast<u_char>(source_u + width),
         source_v, source_u, static_cast<u_char>(source_v + height),
         static_cast<u_char>(source_u + width),
         static_cast<u_char>(source_v + height));
  DrawPrim(&polygon);
}

int characterAdvance(char value) noexcept {
  if (value == ' ') {
    return 4;
  }
  const auto glyph = game::originalHudGlyph(value);
  return glyph ? glyph->advance() : 0;
}

int promptTextWidth(std::string_view source) noexcept {
  auto width = 0;
  for (std::size_t index = 0U; index < source.size(); ++index) {
    width += characterAdvance(source[index]);
  }
  return width == 0 ? 0 : width - 2;
}

struct PositionedGlyph {
  game::OriginalHudGlyph glyph;
  int x{};
  int y{};
};

struct TextLayout {
  std::vector<PositionedGlyph> glyphs;
  int lines{1};
};

bool isSf2OperativeBriefing(const assets::MissionBriefing &briefing) noexcept {
  constexpr std::string_view prefix = "operative:";
  const auto directive = briefing.directive();
  if (directive.size() < prefix.size()) {
    return false;
  }
  for (auto index = std::size_t{}; index < prefix.size(); ++index) {
    auto character = directive[index];
    if (character >= 'A' && character <= 'Z') {
      character = static_cast<char>(character - 'A' + 'a');
    }
    if (character != prefix[index]) {
      return false;
    }
  }
  return true;
}

TextLayout layoutTextObject(std::string_view text, int left, int top, int right,
                            int bottom) {
  TextLayout layout;
  auto x = left;
  auto y = top;
  for (std::size_t index = 0U; index < text.size();) {
    if (text[index] == '\r') {
      ++index;
      continue;
    }
    if (text[index] == '\n') {
      x = left;
      y += retail_line_height;
      ++layout.lines;
      ++index;
      continue;
    }
    if (text[index] == ' ') {
      x += 4;
      ++index;
      continue;
    }

    auto word_end = index;
    while (word_end < text.size() && text[word_end] != ' ' &&
           text[word_end] != '\n' && text[word_end] != '\r') {
      ++word_end;
    }
    const auto word = text.substr(index, word_end - index);
    if (x != left && x + game::originalHudTextWidth(word) > right) {
      x = left;
      y += retail_line_height;
      ++layout.lines;
    }

    for (const auto value : word) {
      const auto glyph = game::originalHudGlyph(value);
      if (!glyph) {
        continue;
      }
      if (x != left && x + glyph->width > right) {
        x = left;
        y += retail_line_height;
        ++layout.lines;
      }
      if (y + glyph->height() <= bottom) {
        layout.glyphs.push_back(PositionedGlyph{*glyph, x, y});
      }
      x += glyph->advance();
    }
    index = word_end;
  }
  return layout;
}

double briefingTextProgress(const assets::MissionBriefing &briefing,
                            int left, int top, int right, int bottom,
                            double retail_time) {
  auto y = top;
  auto animation_steps = std::size_t{};
  const auto include = [&](std::string_view text, int text_left,
                           bool advance_line = true) {
    const auto layout =
        layoutTextObject(text, text_left, y, right, bottom);
    if (advance_line) {
      y += layout.lines * retail_line_height;
    }
    if (layout.glyphs.empty()) {
      return;
    }
    const auto glyphs_per_tick =
        static_cast<std::size_t>(std::max(layout.lines, 1));
    animation_steps = std::max(
        animation_steps,
        (layout.glyphs.size() + glyphs_per_tick - 1U) / glyphs_per_tick);
  };
  if (isSf2OperativeBriefing(briefing)) {
    include(briefing.location(), left, false);
    include(briefing.missionTitle(),
            left + assets::RetailBriefingLayout::sf2_title_column, false);
    y += retail_line_height;
    include(briefing.dateTime(), left);
  } else {
    include(briefing.retailTitle(), left);
  }
  for (const auto directive : briefing.retailDirectives()) {
    if (!directive.empty()) {
      include(directive, left);
    }
  }
  if (animation_steps == 0U) {
    return 1.0;
  }
  return std::clamp(
      (std::max(retail_time, 0.0) + 1.0) /
          static_cast<double>(animation_steps),
      0.0, 1.0);
}

std::uint8_t settleChannel(std::uint8_t target,
                           std::uint32_t settle_ticks) noexcept {
  auto current = 255;
  for (auto tick = 0U; tick < std::min(settle_ticks, 64U); ++tick) {
    const auto change = (static_cast<int>(target) - current) / 4;
    if (change == 0) {
      break;
    }
    current += change;
  }
  return static_cast<std::uint8_t>(current);
}

int drawTextObject(const assets::TimImage &font, std::string_view text,
                   int left, int top, int right, int bottom,
                   std::uint32_t retail_tick,
                   const PsyCrossFontTexture *native_font,
                   bool *animation_complete = nullptr) {
  const ScopedPsyCrossFontTexture font_binding{native_font};
  const auto layout = layoutTextObject(text, left, top, right, bottom);
  const auto glyphs_per_tick =
      static_cast<std::size_t>(std::max(layout.lines, 1));
  const auto leading = static_cast<std::size_t>(retail_tick) * glyphs_per_tick;
  const auto visible = std::min(layout.glyphs.size(), leading + glyphs_per_tick);
  if (animation_complete != nullptr) {
    *animation_complete = *animation_complete && visible == layout.glyphs.size();
  }
  for (std::size_t index = 0U; index < visible; ++index) {
    const auto first_tick =
        static_cast<std::uint32_t>(index / glyphs_per_tick);
    const auto settle_ticks = retail_tick - first_tick;
    const auto color = settle_ticks == 0U
                           ? Rgb{255U, 255U, 255U}
                           : Rgb{settleChannel(briefing_color.red, settle_ticks),
                                 settleChannel(briefing_color.green, settle_ticks),
                                 settleChannel(briefing_color.blue, settle_ticks)};
    const auto &entry = layout.glyphs[index];
    drawTextureRegion(font, entry.x, entry.y, entry.glyph.u, entry.glyph.v,
                      entry.glyph.width, entry.glyph.height(), color);
  }
  return layout.lines;
}

void drawPrompt(const std::vector<BriefingTexture> &textures,
                std::uint32_t retail_tick,
                const PsyCrossFontTexture *native_font,
                const KeyboardMouseBindings &bindings) {
  const auto find = [&](std::string_view name) -> const assets::TimImage & {
    const auto match =
        std::find_if(textures.begin(), textures.end(),
                     [&](const auto &entry) { return entry.name == name; });
    if (match == textures.end()) {
      throw core::Error{core::ErrorCode::not_found,
                        "Retail briefing texture is missing: " +
                            std::string{name}};
    }
    return match->image;
  };

  const auto color = game::MissionStartGate::brightPrompt(retail_tick)
                         ? Rgb{200U, 200U, 255U}
                         : Rgb{};
  const auto right = screen_center_x + assets::RetailBriefingLayout::prompt_x;
  const auto source = keyboardMouseHintText(
      game::localizeTextCopy(assets::RetailBriefingLayout::prompt), bindings);
  const auto prompt_width = promptTextWidth(source);
  auto x = right - prompt_width;
  const auto y = screen_center_y + assets::RetailBriefingLayout::prompt_y;
  const auto &font = find("FONTA.TIM");
  const ScopedPsyCrossFontTexture font_binding{native_font};
  for (std::size_t index = 0U; index < source.size(); ++index) {
    if (source[index] == ' ') {
      x += 4;
      continue;
    }
    const auto glyph = game::originalHudGlyph(source[index]);
    if (!glyph) {
      continue;
    }
    drawTextureRegion(font, x, y, glyph->u, glyph->v, glyph->width,
                      glyph->height(), color);
    x += glyph->advance();
  }
}

} // namespace

struct PsyCrossRetailBriefing::Impl final {
  Impl(const game::MissionPackage &mission, KeyboardMouseBindings input)
      : input{input} {
    constexpr std::array required{
        std::string_view{"FONTA.TIM"},
        std::string_view{"FONTB.TIM"},
        std::string_view{"FONTC.TIM"},
        std::string_view{"SYMBOL.TIM"},
    };
    for (const auto name : required) {
      const auto localized = name != "SYMBOL.TIM"
                                 ? game::readLocalizedAsset(
                                       std::string{"fonts/"} + std::string{name})
                                 : std::nullopt;
      auto image = assets::TimImage::parse(
          localized ? std::span<const std::byte>{*localized}
                    : mission.interfaceAssets().file(name));
      if (image.mode() != assets::TimPixelMode::indexed8 || !image.clut()) {
        throw core::Error{core::ErrorCode::invalid_format,
                          "Retail briefing font is not indexed 8-bit TIM"};
      }
      if (!textures.empty() &&
          image.clut()->words != textures.front().image.clut()->words) {
        throw core::Error{core::ErrorCode::invalid_format,
                          "Retail briefing TIM palettes do not match"};
      }
      textures.push_back(BriefingTexture{std::string{name}, std::move(image)});
    }

    const auto &font = image("FONTA.TIM");
    if (game::russianLanguageActive()) {
      native_font = std::make_unique<PsyCrossFontTexture>(
          font, image("FONTB.TIM"), image("FONTC.TIM"));
    }
    const auto page_x = font.pixels().x & ~std::uint16_t{63U};
    const auto page_y = font.pixels().y & ~std::uint16_t{255U};
    if (native_font == nullptr) {
      for (const auto &texture : textures) {
        if ((texture.image.pixels().x & ~std::uint16_t{63U}) != page_x ||
            (texture.image.pixels().y & ~std::uint16_t{255U}) != page_y) {
          throw core::Error{core::ErrorCode::invalid_format,
                            "Retail briefing TIMs do not share one page"};
        }
      }
    }
    for (const auto &texture : textures) {
      if (native_font != nullptr && texture.name.starts_with("FONT")) {
        continue;
      }
      uploadTimBlock(texture.image.pixels());
    }
    uploadTimBlock(*textures.front().image.clut());
    DrawSync(0);
  }

  [[nodiscard]] const assets::TimImage &image(std::string_view name) const {
    const auto match =
        std::find_if(textures.begin(), textures.end(),
                     [&](const auto &entry) { return entry.name == name; });
    if (match == textures.end()) {
      throw core::Error{core::ErrorCode::not_found,
                        "Retail briefing texture is missing: " +
                            std::string{name}};
    }
    return match->image;
  }

  std::vector<BriefingTexture> textures;
  std::unique_ptr<PsyCrossFontTexture> native_font;
  KeyboardMouseBindings input;
};

PsyCrossRetailBriefing::PsyCrossRetailBriefing(
    const game::MissionPackage &mission, KeyboardMouseBindings bindings)
    : impl_(std::make_unique<Impl>(mission, bindings)) {}

PsyCrossRetailBriefing::~PsyCrossRetailBriefing() = default;

void PsyCrossRetailBriefing::prepare(double retail_time) const {
  const auto retail_tick = static_cast<std::uint32_t>(
      std::max(std::floor(retail_time), 0.0));
  static_cast<void>(uploadBriefingFrameTexture(retail_tick));
}

bool PsyCrossRetailBriefing::draw(const assets::MissionBriefing &briefing,
                                  double retail_time) const {
  const auto retail_tick = static_cast<std::uint32_t>(
      std::max(std::floor(retail_time), 0.0));
  GR_SetBlendMode(BM_NONE);
  GR_EnableDepth(0);
  DRAWENV native_environment{};
  SetDefDrawEnv(&native_environment, 0, 0, screen_width, screen_height);
  native_environment.dtd = 0;
  native_environment.dfe = 1;
  PutDrawEnv(&native_environment);
  drawSolidRect(0, 0, screen_width, screen_height, {});

  const auto left = screen_center_x + assets::RetailBriefingLayout::region_x;
  const auto top = screen_center_y + assets::RetailBriefingLayout::region_y;
  const auto right = left + assets::RetailBriefingLayout::region_width;
  const auto bottom = top + assets::RetailBriefingLayout::region_height;
  const auto &font = impl_->image("FONTA.TIM");
  drawBriefingSurround(
      retail_tick,
      briefingTextProgress(briefing, left, top, right, bottom, retail_time));

  auto text_animation_complete = true;
  auto y = top;
  if (isSf2OperativeBriefing(briefing)) {
    const auto location_lines = drawTextObject(
        font, briefing.location(), left, y, right, bottom, retail_tick,
        impl_->native_font.get(), &text_animation_complete);
    const auto title_lines = drawTextObject(
        font, briefing.missionTitle(),
        left + assets::RetailBriefingLayout::sf2_title_column, y, right,
        bottom, retail_tick, impl_->native_font.get(),
        &text_animation_complete);
    y += std::max(location_lines, title_lines) * retail_line_height;
    y += drawTextObject(font, briefing.dateTime(), left, y, right, bottom,
                        retail_tick, impl_->native_font.get(),
                        &text_animation_complete) *
         retail_line_height;
  } else {
    y += drawTextObject(font, briefing.retailTitle(), left, y, right, bottom,
                        retail_tick, impl_->native_font.get(),
                        &text_animation_complete) *
         retail_line_height;
  }
  for (const auto directive : briefing.retailDirectives()) {
    if (directive.empty()) {
      continue;
    }
    y += drawTextObject(font, directive, left, y, right, bottom, retail_tick,
                        impl_->native_font.get(), &text_animation_complete) *
         retail_line_height;
  }
  if (text_animation_complete) {
    drawPrompt(impl_->textures, retail_tick, impl_->native_font.get(),
               impl_->input);
  }

  DrawSync(0);
  GR_EnableDepth(1);
  return text_animation_complete;
}

} // namespace sf::platform::detail
