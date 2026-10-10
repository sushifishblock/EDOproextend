#include "rarity.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace ygo {

namespace {
using namespace irr;

enum class Pattern { FLAT, SHEEN, GLITTER, LINES, DOTS, CLOUD, BEAM, RAYS, HATCH, VDASH, GLOW, HATCHBAND, VDASHBAND, VIGNETTE, WEAVEBAND, WEAVE, COUNT };
enum class Region { CARD, ART, NAME, BORDER };

//one moving layer of an effect
struct Layer {
	Pattern pattern;
	uint32_t color; //ARGB, multiplies the pattern (alpha = strength)
	float tile; //size of one pattern tile in card widths
	float vx, vy; //tiles per second
	Region region;
	float hue_center = -1; //>= 0: the color slowly moves around this hue
	float hue_amp = 0; //how far it moves (hue is 0-1)
	float hue_rate = 0; //moves per second
	bool rainbow = false; //the hue runs through everything (hue_rate = turns per second)
	float pulse_amp = 0; //the strength goes up and down by this part of itself
	float pulse_rate = 0; //times per second
	float sat = 0.45f; //saturation of moving colors
};

constexpr uint32_t GOLD = 0xffffd24a;

uint32_t WithAlpha(uint32_t color, uint32_t alpha) {
	return (alpha << 24) | (color & 0xffffff);
}

//what the effects of YGO Omega look like (watched one by one, on several cards), each built from several moving layers:
// SR   soft white-blue light sweeping over the picture, a thin bright streak, pale glow in the corners, a few sparkles
// UR   pastel rainbow haze drifting both ways, iridescent clouds, gold and white glitter, a glare sweeping over the whole card
// ScR  bands of fine gold/green/pink diagonal lines crossing each other over fine static hatching, silver glitter, rainbow wash on the frame
// GR   gold wash, crossing bands of fine gold lines, gold rays, golden glow at the edges, glitter, a glare over the whole card
// PR   a fine rainbow warp-and-weft weave over the whole card with a bright band of it sweeping across, pink wash
// Gst  the picture fades into a bright ghost that pulses between pale cyan, lavender and blue, with mist and a glow
// foils: smoke in two layers, a glowing edge, embers/glitter and a beam in the attribute color
static std::vector<Layer> BaseLayers(Rarity rarity) {
	switch(rarity) {
	case Rarity::SR:
		return { { Pattern::BEAM, 0x80e8f0ff, 3.0f, 0.2f, 0, Region::ART }, { Pattern::RAYS, 0x30ffffff, 2.4f, 0.28f, 0, Region::ART },
				 { Pattern::VIGNETTE, 0x50b0d0ff, 0.77f, 0, 0, Region::ART, -1, 0, 0, false, 0.3f, 0.35f }, { Pattern::GLITTER, 0x70ffffff, 0.9f, 0.02f, -0.04f, Region::ART },
				 { Pattern::BEAM, 0x28ffffff, 2.0f, 0.3f, 0, Region::CARD } };
	case Rarity::UR:
		return { { Pattern::BEAM, 0x70ffffff, 3.0f, 0.15f, 0, Region::ART, -1, 0, 0.07f, true }, { Pattern::BEAM, 0x60ffffff, 2.2f, -0.1f, 0.05f, Region::ART, -1, 0, 0.05f, true },
				 { Pattern::CLOUD, 0x60ffffff, 1.2f, 0.04f, 0.02f, Region::ART, -1, 0, 0.09f, true }, { Pattern::FLAT, 0x16ffffff, 1, 0, 0, Region::ART, -1, 0, 0.06f, true },
				 { Pattern::GLITTER, WithAlpha(GOLD, 0x90), 0.7f, 0.03f, -0.06f, Region::ART }, { Pattern::GLITTER, 0x90ffffff, 0.45f, -0.02f, -0.05f, Region::ART },
				 { Pattern::BEAM, 0x40fff0c0, 2.2f, 0.25f, 0, Region::CARD }, { Pattern::FLAT, 0x0cffe080, 1, 0, 0, Region::CARD } };
	case Rarity::SCR:
		return { { Pattern::HATCH, 0x30ffffff, 0.35f, 0.0f, 0.0f, Region::ART }, { Pattern::HATCHBAND, 0xa0ffffff, 0.8f, 0.14f, 0.05f, Region::ART, -1, 0, 0.2f, true },
				 { Pattern::HATCHBAND, 0x78ffffff, 0.6f, -0.1f, 0.07f, Region::ART, -1, 0, 0.15f, true }, { Pattern::BEAM, 0x30ffffff, 3.0f, 0.2f, 0, Region::ART },
				 { Pattern::GLITTER, 0x70ffffff, 0.6f, 0.02f, -0.03f, Region::ART }, { Pattern::FLAT, 0x12ffffff, 1, 0, 0, Region::CARD, -1, 0, 0.25f, true } };
	case Rarity::GR:
		return { { Pattern::FLAT, WithAlpha(GOLD, 0x1c), 1, 0, 0, Region::ART }, { Pattern::HATCHBAND, WithAlpha(0xffffd860, 0xd0), 0.8f, 0.14f, 0.05f, Region::ART },
				 { Pattern::HATCHBAND, WithAlpha(0xffffc040, 0x90), 0.6f, -0.1f, 0.07f, Region::ART }, { Pattern::RAYS, WithAlpha(0xffffe070, 0x50), 1.8f, 0.10f, 0, Region::ART },
				 { Pattern::VIGNETTE, WithAlpha(GOLD, 0x70), 0.77f, 0, 0, Region::ART, -1, 0, 0, false, 0.2f, 0.3f }, { Pattern::GLITTER, WithAlpha(GOLD, 0xa0), 0.6f, 0.02f, -0.04f, Region::ART },
				 { Pattern::FLAT, WithAlpha(GOLD, 0x16), 1, 0, 0, Region::CARD }, { Pattern::BEAM, WithAlpha(0xffffe890, 0x34), 3.0f, 0.2f, 0, Region::CARD } };
	case Rarity::PR:
		//warp and weft: a fine rainbow weave over the whole card, a brighter band of it sweeping across, rainbow washes
		return { { Pattern::WEAVE, 0x50ffffff, 0.5f, 0.02f, 0.01f, Region::CARD }, { Pattern::WEAVEBAND, 0xb0ffffff, 0.5f, 0.12f, 0.05f, Region::CARD },
				 { Pattern::FLAT, 0x14ffffff, 1, 0, 0, Region::CARD, -1, 0, 0.18f, true, 0, 0, 0.85f }, { Pattern::BEAM, 0x34ffffff, 2.2f, 0.2f, 0, Region::CARD, -1, 0, 0.12f, true, 0, 0, 0.8f },
				 { Pattern::BEAM, 0x2cffffff, 1.8f, -0.14f, 0.06f, Region::ART, -1, 0, 0.1f, true, 0, 0, 0.9f }, { Pattern::CLOUD, 0x2cffffff, 1.0f, 0.04f, 0.03f, Region::ART, -1, 0, 0.15f, true, 0, 0, 0.8f },
				 { Pattern::GLITTER, 0x90ffffff, 0.8f, 0.03f, -0.05f, Region::ART } };
	case Rarity::GST:
		return { { Pattern::FLAT, 0xc8b8d8ff, 1, 0, 0, Region::ART, 0.58f, 0.08f, 0.12f, false, 0.22f, 0.2f }, { Pattern::CLOUD, 0x70ffffff, 1.0f, 0.05f, 0.03f, Region::ART },
				 { Pattern::CLOUD, 0x40c0f0ff, 0.6f, -0.04f, 0.05f, Region::ART }, { Pattern::GLOW, 0x60ffffff, 1.4f, 0.02f, 0.01f, Region::ART, -1, 0, 0, false, 0.5f, 0.25f },
				 { Pattern::BEAM, 0x40ffffff, 3.0f, 0.18f, 0, Region::ART }, { Pattern::VIGNETTE, 0x70e0f0ff, 0.77f, 0, 0, Region::ART, -1, 0, 0, false, 0.3f, 0.3f },
				 { Pattern::GLITTER, 0x60d8f4ff, 0.7f, 0.01f, -0.05f, Region::ART } };
	default: return {};
	}
}

//where the picture is on a card (fractions of the card)
//how fast everything moves (1 = the speed of the first version)
constexpr double SPEED = 1.8;
constexpr float ART_X0 = 0.120f, ART_X1 = 0.880f, ART_Y0 = 0.180f, ART_Y1 = 0.705f;
//the name bar
constexpr float NAME_X0 = 0.06f, NAME_X1 = 0.80f, NAME_Y0 = 0.030f, NAME_Y1 = 0.110f;

void RegionFractions(Region region, float& x0, float& x1, float& y0, float& y1) {
	switch(region) {
	case Region::ART: x0 = ART_X0; x1 = ART_X1; y0 = ART_Y0; y1 = ART_Y1; break;
	case Region::NAME: x0 = NAME_X0; x1 = NAME_X1; y0 = NAME_Y0; y1 = NAME_Y1; break;
	default: x0 = 0; x1 = 1; y0 = 0; y1 = 1; break;
	}
}

//the parts of the card a region covers (x0, x1, y0, y1 as fractions); the border is four thin strips
struct StripList {
	std::array<std::array<float, 4>, 4> items;
	int count = 0;
	const std::array<float, 4>* begin() const { return items.data(); }
	const std::array<float, 4>* end() const { return items.data() + count; }
};
StripList RegionStrips(Region region) {
	StripList list;
	if(region == Region::BORDER) {
		constexpr float t = 0.032f, sd = 0.045f;
		list.items = { { { 0, 1, 0, t }, { 0, 1, 1 - t, 1 }, { 0, sd, t, 1 - t }, { 1 - sd, 1, t, 1 - t } } };
		list.count = 4;
		return list;
	}
	float x0, x1, y0, y1;
	RegionFractions(region, x0, x1, y0, y1);
	list.items[0] = { x0, x1, y0, y1 };
	list.count = 1;
	return list;
}

video::ITexture* patterns[static_cast<int>(Pattern::COUNT)] = {};
video::IImage* pattern_images[static_cast<int>(Pattern::COUNT)] = {};
video::IVideoDriver* patterns_driver = nullptr;
std::unordered_map<uint64_t, video::ITexture*> scaled_patterns;

uint32_t Hash(uint32_t x) {
	x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
	return x;
}

video::SColor Hsv(float h, float s, float v, int alpha) {
	h = h - std::floor(h);
	const float c = v * s;
	const float x = c * (1 - std::fabs(std::fmod(h * 6, 2.0f) - 1));
	const float m = v - c;
	float r = 0, g = 0, b = 0;
	const int sector = static_cast<int>(h * 6);
	switch(sector) {
	case 0: r = c; g = x; break;
	case 1: r = x; g = c; break;
	case 2: g = c; b = x; break;
	case 3: g = x; b = c; break;
	case 4: r = x; b = c; break;
	default: r = c; b = x; break;
	}
	return video::SColor(alpha, static_cast<u32>((r + m) * 255), static_cast<u32>((g + m) * 255), static_cast<u32>((b + m) * 255));
}

void Plot(video::IImage* image, int x, int y, int size, int alpha) {
	x = ((x % size) + size) % size;
	y = ((y % size) + size) % size;
	const auto old = image->getPixel(x, y);
	if(static_cast<int>(old.getAlpha()) < alpha)
		image->setPixel(x, y, video::SColor(alpha, 255, 255, 255));
}

video::ITexture* MakePattern(video::IVideoDriver* driver, Pattern pattern) {
	const int size = pattern == Pattern::FLAT ? 8 : 256;
	auto* image = driver->createImage(video::ECF_A8R8G8B8, core::dimension2d<u32>(size, size));
	if(!image)
		return nullptr;
	image->fill(video::SColor(0, 255, 255, 255));
	const float two_pi = 6.2831853f;
	switch(pattern) {
	case Pattern::FLAT:
		image->fill(video::SColor(255, 255, 255, 255));
		break;
	case Pattern::SHEEN:
		for(int y = 0; y < size; ++y) {
			for(int x = 0; x < size; ++x) {
				const int d = (x + y) % size;
				const float dist = std::fabs(static_cast<float>(d) - 128.0f);
				const float a = std::max(0.0f, 1.0f - dist / 46.0f);
				image->setPixel(x, y, video::SColor(static_cast<u32>(a * a * 255), 255, 255, 255));
			}
		}
		break;
	case Pattern::GLITTER:
		for(uint32_t i = 0; i < 90; ++i) {
			const int cx = Hash(i * 2 + 1) % size;
			const int cy = Hash(i * 2 + 2) % size;
			const int arm = 3 + static_cast<int>(Hash(i + 777) % 6);
			const int power = 150 + static_cast<int>(Hash(i + 999) % 106);
			Plot(image, cx, cy, size, 255);
			for(int k = 1; k <= arm; ++k) {
				const int a = power * (arm + 1 - k) / (arm + 1);
				Plot(image, cx + k, cy, size, a);
				Plot(image, cx - k, cy, size, a);
				Plot(image, cx, cy + k, size, a);
				Plot(image, cx, cy - k, size, a);
			}
			Plot(image, cx + 1, cy + 1, size, power / 2);
			Plot(image, cx - 1, cy - 1, size, power / 2);
			Plot(image, cx + 1, cy - 1, size, power / 2);
			Plot(image, cx - 1, cy + 1, size, power / 2);
		}
		break;
	case Pattern::LINES:
		for(int y = 0; y < size; ++y) {
			for(int x = 0; x < size; ++x) {
				const int v = ((x - y) % 8 + 8) % 8;
				const float hue = static_cast<float>((x + y) % size) / size;
				const float pulse = 0.65f + 0.35f * std::sin(two_pi * 2 * hue);
				if(v < 2)
					image->setPixel(x, y, Hsv(hue, 0.75f, 1.0f, static_cast<int>(255 * pulse)));
			}
		}
		break;
	case Pattern::DOTS:
		for(int y = 0; y < size; ++y) {
			for(int x = 0; x < size; ++x) {
				const float dx = static_cast<float>(x % 8) - 3.5f;
				const float dy = static_cast<float>(y % 8) - 3.5f;
				const float dist = std::sqrt(dx * dx + dy * dy);
				if(dist < 2.4f) {
					const float hue = static_cast<float>((x * 7 / 10 + y) % size) / size;
					image->setPixel(x, y, Hsv(hue, 0.7f, 1.0f, static_cast<int>(255 * (1.0f - dist / 3.2f))));
				}
			}
		}
		break;
	case Pattern::CLOUD:
		for(int y = 0; y < size; ++y) {
			for(int x = 0; x < size; ++x) {
				const float fx = x / static_cast<float>(size), fy = y / static_cast<float>(size);
				float n = 0.5f + 0.25f * std::sin(two_pi * (2 * fx + fy)) + 0.15f * std::sin(two_pi * (fx - 3 * fy) + 1.0f) + 0.10f * std::sin(two_pi * (5 * fx + 2 * fy) + 2.0f);
				n = std::clamp(n, 0.0f, 1.0f);
				image->setPixel(x, y, video::SColor(static_cast<u32>(n * n * 255), 220, 240, 255));
			}
		}
		break;
	case Pattern::BEAM:
		//a wide soft diagonal band
		for(int y = 0; y < size; ++y) {
			for(int x = 0; x < size; ++x) {
				const float d = std::fabs(static_cast<float>((x + y) % size) - 128.0f);
				const float a = std::max(0.0f, 1.0f - d / 80.0f);
				image->setPixel(x, y, video::SColor(static_cast<u32>(a * a * (3 - 2 * a) * 255), 255, 255, 255));
			}
		}
		break;
	case Pattern::RAYS:
		//several soft diagonal streaks of different widths
		for(int y = 0; y < size; ++y) {
			for(int x = 0; x < size; ++x) {
				const int d = (x + y) % size;
				float a = 0;
				static const int centers[5] = { 20, 70, 120, 175, 225 };
				static const float widths[5] = { 10, 26, 8, 20, 14 };
				for(int k = 0; k < 5; ++k)
					a = std::max(a, std::max(0.0f, 1.0f - std::fabs(static_cast<float>(d - centers[k])) / widths[k]));
				image->setPixel(x, y, video::SColor(static_cast<u32>(a * 255), 255, 255, 255));
			}
		}
		break;
	case Pattern::HATCH:
		//fine diagonal lines with soft edges and a gentle pulse along them
		for(int y = 0; y < size; ++y) {
			for(int x = 0; x < size; ++x) {
				const float line = 0.5f + 0.5f * std::cos(two_pi * (x - y) / 4.0f);
				const float along = std::sin(two_pi * static_cast<float>((x + y) % size) / size * 2.0f);
				const float pulse = 0.5f + 0.5f * along * along;
				image->setPixel(x, y, video::SColor(static_cast<u32>(255 * line * line * pulse), 255, 255, 255));
			}
		}
		break;
	case Pattern::VDASH:
		//short vertical dashes in a staggered grid
		for(int y = 0; y < size; ++y) {
			for(int x = 0; x < size; ++x) {
				const int col = x % 6;
				const int row = (y + (x / 6) * 5) % 16;
				if(col < 2 && row < 9) {
					const float hue = static_cast<float>(x % size) / size;
					image->setPixel(x, y, Hsv(hue * 3, 0.6f, 1.0f, 255));
				}
			}
		}
		break;
	case Pattern::GLOW:
		//a soft round glow in the middle of the tile
		for(int y = 0; y < size; ++y) {
			for(int x = 0; x < size; ++x) {
				const float dx = (x - 128) / 128.0f, dy = (y - 128) / 128.0f;
				const float a = std::max(0.0f, 1.0f - std::sqrt(dx * dx + dy * dy));
				image->setPixel(x, y, video::SColor(static_cast<u32>(a * a * 255), 255, 255, 255));
			}
		}
		break;
	case Pattern::HATCHBAND:
		//fine soft diagonal lines, only inside a wide soft diagonal band, in shifting colors
		for(int y = 0; y < size; ++y) {
			for(int x = 0; x < size; ++x) {
				const float d = std::fabs(static_cast<float>((x + y) % size) - 128.0f);
				const float band = std::max(0.0f, 1.0f - d / 85.0f);
				const float line = 0.5f + 0.5f * std::cos(two_pi * (x - y) / 4.0f);
				const float hue = 0.1f + 0.35f * static_cast<float>(((x + y) % size)) / size;
				image->setPixel(x, y, Hsv(hue * 2.0f, 0.55f, 1.0f, static_cast<int>(255 * band * band * line * line)));
			}
		}
		break;
	case Pattern::VDASHBAND:
		//staggered vertical dashes in rainbow colors, only inside a wide vertical band
		for(int y = 0; y < size; ++y) {
			for(int x = 0; x < size; ++x) {
				const float band = std::max(0.0f, 1.0f - std::fabs(static_cast<float>(x) - 128.0f) / 100.0f);
				const int col = x % 4;
				const int row = (y + (x / 4) * 7) % 12;
				if(col < 2 && row < 7 && band > 0) {
					const float hue = static_cast<float>(y % size) / size + static_cast<float>(x) / size * 0.5f;
					image->setPixel(x, y, Hsv(hue * 2.0f, 0.55f, 1.0f, static_cast<int>(255 * band)));
				}
			}
		}
		break;
	case Pattern::VIGNETTE:
		//bright at the edges, clear in the middle
		for(int y = 0; y < size; ++y) {
			for(int x = 0; x < size; ++x) {
				const float dx = std::fabs(x - 127.5f) / 127.5f, dy = std::fabs(y - 127.5f) / 127.5f;
				const float e = std::max(dx, dy);
				const float a = std::clamp((e - 0.45f) / 0.55f, 0.0f, 1.0f);
				image->setPixel(x, y, video::SColor(static_cast<u32>(a * a * 255), 255, 255, 255));
			}
		}
		break;
	case Pattern::WEAVEBAND:
		for(int y = 0; y < size; ++y) {
			for(int x = 0; x < size; ++x) {
				const float d = std::fabs(static_cast<float>((x + y) % size) - 128.0f);
				const float band = std::max(0.0f, 1.0f - d / 100.0f);
				const int cx = x / 4, cy = y / 4;
				const float px = static_cast<float>(x % 4), py = static_cast<float>(y % 4);
				//a rounded thread profile across and a dip where the thread goes under
				const float across_h = std::sin(3.14159265f * (py + 0.5f) / 4.0f), across_v = std::sin(3.14159265f * (px + 0.5f) / 4.0f);
				const float along_h = 0.65f + 0.35f * std::sin(3.14159265f * (px + 0.5f) / 4.0f), along_v = 0.65f + 0.35f * std::sin(3.14159265f * (py + 0.5f) / 4.0f);
				const bool horizontal_on_top = ((cx + cy) % 2) == 0;
				const float h = across_h * (horizontal_on_top ? along_h : 0.3f);
				const float v = across_v * (horizontal_on_top ? 0.3f : along_v);
				const bool horizontal = h >= v;
				const float shade = std::max(h, v);
				const float hue = static_cast<float>(((x + y) * 4) % size) / size + (horizontal ? 0.0f : 0.5f);
				image->setPixel(x, y, Hsv(hue, 0.9f, 1.0f, static_cast<int>(255 * band * band * shade)));
			}
		}
		break;
	case Pattern::WEAVE:
		for(int y = 0; y < size; ++y) {
			for(int x = 0; x < size; ++x) {
				const int cx = x / 4, cy = y / 4;
				const float px = static_cast<float>(x % 4), py = static_cast<float>(y % 4);
				//a rounded thread profile across and a dip where the thread goes under
				const float across_h = std::sin(3.14159265f * (py + 0.5f) / 4.0f), across_v = std::sin(3.14159265f * (px + 0.5f) / 4.0f);
				const float along_h = 0.65f + 0.35f * std::sin(3.14159265f * (px + 0.5f) / 4.0f), along_v = 0.65f + 0.35f * std::sin(3.14159265f * (py + 0.5f) / 4.0f);
				const bool horizontal_on_top = ((cx + cy) % 2) == 0;
				const float h = across_h * (horizontal_on_top ? along_h : 0.3f);
				const float v = across_v * (horizontal_on_top ? 0.3f : along_v);
				const bool horizontal = h >= v;
				const float shade = std::max(h, v);
				const float hue = static_cast<float>(((x + y) * 4) % size) / size + (horizontal ? 0.0f : 0.5f);
				image->setPixel(x, y, Hsv(hue, 0.9f, 1.0f, static_cast<int>(255 * 1.0f * shade)));
			}
		}
		break;
	default:
		break;
	}
	const bool mips = driver->getTextureCreationFlag(video::ETCF_CREATE_MIP_MAPS);
	driver->setTextureCreationFlag(video::ETCF_CREATE_MIP_MAPS, true);
	video::ITexture* texture = driver->addTexture("rarity_pattern", image);
	driver->setTextureCreationFlag(video::ETCF_CREATE_MIP_MAPS, mips);
	if(pattern_images[static_cast<int>(pattern)])
		pattern_images[static_cast<int>(pattern)]->drop();
	pattern_images[static_cast<int>(pattern)] = image;
	return texture;
}

video::ITexture* GetPattern(video::IVideoDriver* driver, Pattern pattern) {
	if(patterns_driver != driver) {
		//the old driver (and its textures) is gone
		for(auto& t : patterns)
			t = nullptr;
		scaled_patterns.clear();
		patterns_driver = driver;
	}
	auto& slot = patterns[static_cast<int>(pattern)];
	if(!slot)
		slot = MakePattern(driver, pattern);
	return slot;
}

//a pattern tile at an exact size in pixels (box filtered), cached; shown 1:1 so nothing is pixelated
video::ITexture* GetScaledPattern(video::IVideoDriver* driver, Pattern pattern, int size_px) {
	if(!GetPattern(driver, pattern))
		return nullptr;
	video::IImage* base = pattern_images[static_cast<int>(pattern)];
	if(!base)
		return nullptr;
	const uint64_t key = (static_cast<uint64_t>(pattern) << 32) | static_cast<uint32_t>(size_px);
	const auto found = scaled_patterns.find(key);
	if(found != scaled_patterns.end())
		return found->second;
	if(scaled_patterns.size() > 200) {
		for(auto& entry : scaled_patterns)
			driver->removeTexture(entry.second);
		scaled_patterns.clear();
	}
	const int base_size = static_cast<int>(base->getDimension().Width);
	auto* image = driver->createImage(video::ECF_A8R8G8B8, core::dimension2d<u32>(size_px, size_px));
	if(!image)
		return nullptr;
	for(int dy = 0; dy < size_px; ++dy) {
		const int sy0 = dy * base_size / size_px;
		const int sy1 = std::max(sy0 + 1, ((dy + 1) * base_size + size_px - 1) / size_px);
		for(int dx = 0; dx < size_px; ++dx) {
			const int sx0 = dx * base_size / size_px;
			const int sx1 = std::max(sx0 + 1, ((dx + 1) * base_size + size_px - 1) / size_px);
			float a = 0, r = 0, g = 0, b = 0;
			int n = 0;
			for(int sy = sy0; sy < sy1 && sy < base_size; ++sy) {
				for(int sx = sx0; sx < sx1 && sx < base_size; ++sx) {
					const auto c = base->getPixel(sx, sy);
					const float ca = static_cast<float>(c.getAlpha());
					a += ca;
					r += ca * c.getRed();
					g += ca * c.getGreen();
					b += ca * c.getBlue();
					++n;
				}
			}
			if(n == 0 || a <= 0) {
				image->setPixel(dx, dy, video::SColor(0, 255, 255, 255));
				continue;
			}
			image->setPixel(dx, dy, video::SColor(static_cast<u32>(a / n), static_cast<u32>(r / a), static_cast<u32>(g / a), static_cast<u32>(b / a)));
		}
	}
	const bool mips = driver->getTextureCreationFlag(video::ETCF_CREATE_MIP_MAPS);
	driver->setTextureCreationFlag(video::ETCF_CREATE_MIP_MAPS, false);
	video::ITexture* texture = driver->addTexture("rarity_scaled", image);
	driver->setTextureCreationFlag(video::ETCF_CREATE_MIP_MAPS, mips);
	image->drop();
	scaled_patterns[key] = texture;
	return texture;
}

//pattern tiles are made at a few sizes only (the draw stretches them a little), so the cache stays small
int QuantizeSize(int size) {
	static const int steps[] = { 16, 24, 32, 48, 64, 96, 128, 192, 256, 384 };
	for(const int step : steps)
		if(size <= step)
			return step;
	return 384;
}

float Wrap01(float v) {
	v -= std::floor(v);
	return v;
}

//the color of a layer at a point in time (some layers move through colors)
//the layers of a rarity: its own plus a holo border
static std::vector<Layer> BuildLayers(Rarity rarity) {
	auto layers = BaseLayers(rarity);
	switch(rarity) {
	case Rarity::SR:
		layers.push_back({ Pattern::BEAM, 0x90ffffff, 1.4f, 0.3f, 0, Region::BORDER });
		break;
	case Rarity::UR:
		layers.push_back({ Pattern::BEAM, 0xb0ffffff, 1.4f, 0.3f, 0, Region::BORDER, -1, 0, 0.3f, true, 0, 0, 0.7f });
		break;
	case Rarity::SCR:
		layers.push_back({ Pattern::BEAM, 0xc0ffffff, 1.2f, 0.35f, 0, Region::BORDER, -1, 0, 0.35f, true, 0, 0, 0.6f });
		break;
	case Rarity::GR:
		layers.push_back({ Pattern::BEAM, WithAlpha(GOLD, 0xd0), 1.2f, 0.35f, 0, Region::BORDER });
		break;
	case Rarity::PR:
		layers.push_back({ Pattern::BEAM, 0xd0ffffff, 1.0f, 0.4f, 0, Region::BORDER, -1, 0, 0.45f, true, 0, 0, 0.95f });
		break;
	case Rarity::GST:
		layers.push_back({ Pattern::FLAT, 0x90a0d8ff, 1, 0, 0, Region::BORDER, -1, 0, 0, false, 0.5f, 0.5f });
		break;
	default:
		break;
	}
	return layers;
}
//the layers of a rarity are built once
const std::vector<Layer>& LayersFor(Rarity rarity) {
	static std::vector<Layer> cache[static_cast<int>(Rarity::COUNT)];
	static bool built[static_cast<int>(Rarity::COUNT)] = {};
	const int index = static_cast<int>(rarity);
	if(!built[index]) {
		cache[index] = BuildLayers(rarity);
		built[index] = true;
	}
	return cache[index];
}

uint32_t LayerColor(const Layer& layer, double seconds) {
	uint32_t base_alpha = layer.color >> 24;
	if(layer.pulse_amp > 0) {
		const double wave = 0.5 + 0.5 * std::sin(seconds * layer.pulse_rate * 6.2831853);
		base_alpha = static_cast<uint32_t>(std::clamp(base_alpha * (1.0 - layer.pulse_amp + layer.pulse_amp * 2 * wave), 0.0, 255.0));
	}
	if(layer.rainbow) {
		const auto c = Hsv(static_cast<float>(seconds * layer.hue_rate), layer.sat, 1.0f, base_alpha);
		return c.color;
	}
	if(layer.hue_center >= 0 && layer.hue_amp > 0) {
		const float hue = layer.hue_center + layer.hue_amp * std::sin(static_cast<float>(seconds * layer.hue_rate * 6.2831853));
		const auto c = Hsv(hue, layer.sat, 1.0f, base_alpha);
		//keep the strength of the layer, only the hue moves
		return c.color;
	}
	return (base_alpha << 24) | (layer.color & 0xffffff);
}

uint32_t ScaleAlpha(uint32_t color, uint32_t alpha) {
	const uint32_t a = ((color >> 24) * alpha) / 255;
	return (a << 24) | (color & 0xffffff);
}
}

namespace {
//write to a temporary file and rename it over the target, so a crash cannot leave half a file
void WriteFileAtomic(const std::filesystem::path& path, const std::string& content) {
	std::filesystem::path temp = path;
	temp += ".tmp";
	{
		std::ofstream file(temp, std::ios::binary | std::ios::trunc);
		if(!file.is_open())
			return;
		file << content;
		if(!file.good())
			return;
	}
	std::error_code ec;
	std::filesystem::rename(temp, path, ec);
	if(ec) {
		std::filesystem::remove(path, ec);
		std::filesystem::rename(temp, path, ec);
	}
}
}

void RarityFx::Load() {
	chosen.clear();
	std::ifstream file("rarities.txt");
	uint32_t code;
	int value;
	while(file >> code >> value) {
		if(value > 0 && value < static_cast<int>(Rarity::COUNT))
			chosen[code] = static_cast<Rarity>(value);
	}
}

void RarityFx::Save() {
	std::ostringstream out;
	for(const auto& entry : chosen)
		out << entry.first << " " << static_cast<int>(entry.second) << "\n";
	WriteFileAtomic("rarities.txt", out.str());
}

Rarity RarityFx::Get(uint32_t code) {
	if(chosen.empty())
		return Rarity::NORMAL;
	const auto it = chosen.find(code);
	return it == chosen.end() ? Rarity::NORMAL : it->second;
}

void RarityFx::Set(uint32_t code, Rarity rarity) {
	if(rarity == Rarity::NORMAL)
		chosen.erase(code);
	else
		chosen[code] = rarity;
	Save();
}

namespace {
std::filesystem::path DeckFile(const std::wstring& name) {
	return std::filesystem::path(L"deck/" + name + L".rarity");
}
}

Rarity RarityFx::DeckTable::Lookup(uint32_t code, int ordinal) const {
	if(per_copy && ordinal >= 0) {
		const auto it = copies.find(code);
		if(it != copies.end() && static_cast<size_t>(ordinal) < it->second.size() && it->second[ordinal] != 255)
			return static_cast<Rarity>(it->second[ordinal]);
	}
	const auto it = defaults.find(code);
	if(it != defaults.end())
		return static_cast<Rarity>(it->second);
	return Get(code);
}

RarityFx::DeckTable RarityFx::LoadTable(const std::wstring& name) {
	DeckTable table;
	std::ifstream file(DeckFile(name));
	std::string kind;
	while(file >> kind) {
		if(kind == "enabled") {
			int v = 0; file >> v; table.enabled = v != 0;
		} else if(kind == "percopy") {
			int v = 0; file >> v; table.per_copy = v != 0;
		} else if(kind == "d") {
			uint32_t code; int value;
			if(file >> code >> value && value >= 0 && value < static_cast<int>(Rarity::COUNT))
				table.defaults[code] = static_cast<uint8_t>(value);
		} else if(kind == "c") {
			uint32_t code; int n;
			if(!(file >> code >> n))
				break;
			auto& list = table.copies[code];
			for(int i = 0; i < n; ++i) {
				int value = 255;
				file >> value;
				if(i < 200)
					list.push_back(value >= 0 && value < static_cast<int>(Rarity::COUNT) ? static_cast<uint8_t>(value) : static_cast<uint8_t>(255));
			}
		} else {
			break;
		}
	}
	return table;
}

void RarityFx::SaveTable(const std::wstring& name, const DeckTable& table) {
	if(name.empty())
		return;
	std::error_code ec;
	if(!table.enabled && table.defaults.empty() && table.copies.empty()) {
		std::filesystem::remove(DeckFile(name), ec);
		return;
	}
	std::ostringstream out;
	out << "enabled " << (table.enabled ? 1 : 0) << "\n";
	out << "percopy " << (table.per_copy ? 1 : 0) << "\n";
	for(const auto& entry : table.defaults)
		out << "d " << entry.first << " " << static_cast<int>(entry.second) << "\n";
	for(const auto& entry : table.copies) {
		out << "c " << entry.first << " " << entry.second.size();
		for(const auto value : entry.second)
			out << " " << static_cast<int>(value);
		out << "\n";
	}
	WriteFileAtomic(DeckFile(name), out.str());
}

void RarityFx::EditDeck(const std::wstring& name) {
	if(edit_loaded && name == edit_name)
		return;
	edit_loaded = true;
	edit_name = name;
	edit = name.empty() ? DeckTable() : LoadTable(name);
	++generation;
}

void RarityFx::SetDeckMode(bool on) {
	++generation;
	edit.enabled = on;
	if(!on)
		edit.per_copy = false;
	SaveTable(edit_name, edit);
}

void RarityFx::SetPerCopy(bool on) {
	++generation;
	edit.per_copy = on && edit.enabled;
	SaveTable(edit_name, edit);
}

Rarity RarityFx::GetFor(uint32_t code, int ordinal) {
	if(!edit.enabled)
		return Get(code);
	return edit.Lookup(code, ordinal);
}

void RarityFx::SetFor(uint32_t code, int ordinal, Rarity rarity) {
	if(!edit.enabled) {
		Set(code, rarity);
		return;
	}
	if(edit.per_copy && ordinal >= 0) {
		auto& list = edit.copies[code];
		if(list.size() <= static_cast<size_t>(ordinal))
			list.resize(ordinal + 1, 255);
		list[ordinal] = static_cast<uint8_t>(rarity);
	} else {
		edit.defaults[code] = static_cast<uint8_t>(rarity);
		edit.copies.erase(code);
	}
	SaveTable(edit_name, edit);
}

void RarityFx::ResetDeck() {
	++generation;
	edit.defaults.clear();
	edit.copies.clear();
	SaveTable(edit_name, edit);
}

void RarityFx::ResetAll() {
	++generation;
	chosen.clear();
	Save();
	edit.defaults.clear();
	edit.copies.clear();
	edit.enabled = false;
	edit.per_copy = false;
	duel_pool.clear();
	std::error_code ec;
	std::vector<std::filesystem::path> files;
	for(const auto& entry : std::filesystem::recursive_directory_iterator("deck", ec)) {
		if(entry.path().extension() == ".rarity")
			files.push_back(entry.path());
	}
	for(const auto& file : files)
		std::filesystem::remove(file, ec);
}

void RarityFx::SaveDeckAs(const std::wstring& name) {
	edit_name = name;
	edit_loaded = true;
	SaveTable(name, edit);
}

void RarityFx::RenameDeck(const std::wstring& from, const std::wstring& to) {
	std::error_code ec;
	if(std::filesystem::exists(DeckFile(from), ec)) {
		std::filesystem::remove(DeckFile(to), ec);
		std::filesystem::rename(DeckFile(from), DeckFile(to), ec);
	}
	if(edit_name == from)
		edit_name = to;
}

void RarityFx::DeleteDeck(const std::wstring& name) {
	++generation;
	std::error_code ec;
	std::filesystem::remove(DeckFile(name), ec);
	if(edit_name == name)
		edit = DeckTable();
}

void RarityFx::PrepareDuel(const std::wstring& name, const std::vector<uint32_t>& codes) {
	duel_pool.clear();
	if(!name.empty())
		duel_name = name;
	const DeckTable table = (edit_loaded && duel_name == edit_name) ? edit : LoadTable(duel_name);
	if(!table.enabled)
		return;
	std::map<uint32_t, int> seen;
	for(const auto code : codes)
		duel_pool[code].push_back(table.Lookup(code, seen[code]++));
}

Rarity RarityFx::TakeDuelRarity(uint32_t code) {
	const auto it = duel_pool.find(code);
	if(it == duel_pool.end() || it->second.empty())
		return Get(code);
	const auto rarity = it->second.front();
	it->second.pop_front();
	return rarity;
}

const wchar_t* RarityFx::Name(Rarity rarity) {
	static const wchar_t* names[] = { L"Normal (R)", L"Super Rare (SR)", L"Ultra Rare (UR)", L"Secret Rare (ScR)", L"Gold Rare (GR)", L"Prismatic Secret (PR)", L"Ghost Rare (Gst)" };
	return names[static_cast<int>(rarity)];
}

void RarityFx::ReleaseTextures() {
	for(auto& t : patterns)
		t = nullptr;
	patterns_driver = nullptr;
}

void RarityFx::Draw2D(video::IVideoDriver* driver, const core::recti& card, Rarity rarity, uint32_t time_ms, const core::recti* clip, bool rotated) {
	if(rarity == Rarity::NORMAL)
		return;
	const int w = card.getWidth(), h = card.getHeight();
	if(w < 12 || h < 12)
		return;
	const double seconds = time_ms / 1000.0 * SPEED;
	//the mouse over the card: the layers shift against it (each by its own amount, like layers at different depths)
	const bool hovered = !rotated && mouse_x >= card.UpperLeftCorner.X && mouse_x < card.LowerRightCorner.X && mouse_y >= card.UpperLeftCorner.Y && mouse_y < card.LowerRightCorner.Y && (!clip || clip->isPointInside(core::vector2di(mouse_x, mouse_y)));
	const float tilt_x = hovered ? std::clamp((mouse_x - card.getCenter().X) / (w * 0.5f), -1.0f, 1.0f) : 0.0f;
	const float tilt_y = hovered ? std::clamp((mouse_y - card.getCenter().Y) / (h * 0.5f), -1.0f, 1.0f) : 0.0f;
	int layer_index = -1;
	for(const auto& layer : LayersFor(rarity)) {
		++layer_index;
		const float depth = 0.15f + 0.12f * (layer_index % 4);
		for(const auto& strip : RegionStrips(layer.region)) {
		const float fx0 = strip[0], fx1 = strip[1], fy0 = strip[2], fy1 = strip[3];
		//a card lying on its side (defense position): the card's top is on the left, its width runs up and down
		const core::recti area = rotated
			? core::recti(card.UpperLeftCorner.X + static_cast<int>(w * fy0), card.UpperLeftCorner.Y + static_cast<int>(h * fx0),
						  card.UpperLeftCorner.X + static_cast<int>(w * fy1), card.UpperLeftCorner.Y + static_cast<int>(h * fx1))
			: core::recti(card.UpperLeftCorner.X + static_cast<int>(w * fx0), card.UpperLeftCorner.Y + static_cast<int>(h * fy0),
						  card.UpperLeftCorner.X + static_cast<int>(w * fx1), card.UpperLeftCorner.Y + static_cast<int>(h * fy1));
		const video::SColor color(LayerColor(layer, seconds));
		if(layer.pattern == Pattern::FLAT) {
			driver->draw2DRectangle(color, area, clip);
			continue;
		}
		const int tile = std::max(8, static_cast<int>((rotated ? h : w) * layer.tile));
		//tiles up to 384 pixels are made at their real size, bigger (smooth) ones are stretched a little
		const int made = QuantizeSize(std::min(tile, 384));
		auto* texture = GetScaledPattern(driver, layer.pattern, made);
		if(!texture)
			continue;
		const int off_x = static_cast<int>(Wrap01(static_cast<float>(seconds * layer.vx) - tilt_x * depth) * tile);
		const int off_y = static_cast<int>(Wrap01(static_cast<float>(seconds * layer.vy) - tilt_y * depth) * tile);
		const video::SColor colors[4] = { color, color, color, color };
		for(int x = area.UpperLeftCorner.X - off_x; x < area.LowerRightCorner.X; x += tile) {
			for(int y = area.UpperLeftCorner.Y - off_y; y < area.LowerRightCorner.Y; y += tile) {
				core::recti dest(x, y, x + tile, y + tile);
				dest.clipAgainst(area);
				if(dest.getWidth() <= 0 || dest.getHeight() <= 0)
					continue;
				const core::recti source((dest.UpperLeftCorner.X - x) * made / tile, (dest.UpperLeftCorner.Y - y) * made / tile,
										 (dest.LowerRightCorner.X - x) * made / tile, (dest.LowerRightCorner.Y - y) * made / tile);
				driver->draw2DImage(texture, dest, source, clip, colors, true);
			}
		}
		}
	}
	if(hovered && w >= 60) {
		//a soft glint under the mouse
		const int size = std::max(16, w / 2);
		const int made = QuantizeSize(size);
		if(auto* glow = GetScaledPattern(driver, Pattern::GLOW, made)) {
			const core::recti dest(mouse_x - size / 2, mouse_y - size / 2, mouse_x + size / 2, mouse_y + size / 2);
			core::recti limit = card;
			if(clip)
				limit.clipAgainst(*clip);
			const video::SColor c(0x58ffffff);
			const video::SColor colors[4] = { c, c, c, c };
			driver->draw2DImage(glow, dest, core::recti(0, 0, made, made), &limit, colors, true);
		}
	}
}

void RarityFx::DrawShine3D(video::IVideoDriver* driver, const video::SMaterial& base, float progress, uint32_t alpha) {
	if(progress <= 0.0f || progress >= 1.0f || alpha == 0)
		return;
	auto* texture = GetPattern(driver, Pattern::BEAM);
	if(!texture)
		return;
	static const u16 indices[6] = { 0, 1, 2, 2, 1, 3 };
	//a wide beam texture slid once across the whole card, fading in and out
	const float strength = std::sin(progress * 3.14159265f);
	const video::SColor color(ScaleAlpha(0xffffffff, static_cast<uint32_t>(alpha * strength)));
	const float u0 = progress, ru = 1.0f / 3.0f;
	const float z = 0.0045f;
	video::S3DVertex v[4] = {
		video::S3DVertex(-0.35f, -0.5f, z, 0, 0, 1, color, u0, 0.0f),
		video::S3DVertex(0.35f, -0.5f, z, 0, 0, 1, color, u0 + ru, 0.0f),
		video::S3DVertex(-0.35f, 0.5f, z, 0, 0, 1, color, u0, 1.0f / 3.0f * 1.43f),
		video::S3DVertex(0.35f, 0.5f, z, 0, 0, 1, color, u0 + ru, 1.0f / 3.0f * 1.43f),
	};
	video::SMaterial material = base;
	material.setTexture(0, texture);
	driver->setMaterial(material);
	driver->drawVertexPrimitiveList(v, 4, indices, 2);
}

void RarityFx::DrawSummonShine(video::IVideoDriver* driver, const core::recti& card, float progress, const core::recti* clip) {
	if(progress <= 0.0f || progress >= 1.0f)
		return;
	const int w = card.getWidth(), h = card.getHeight();
	if(w < 12 || h < 12)
		return;
	//a bright diagonal band sweeping from the top left to the bottom right, fading out at the end
	const float fade = progress < 0.8f ? 1.0f : (1.0f - progress) / 0.2f;
	const int band = std::max(8, w / 3);
	const int travel = w + h + band;
	const int pos = static_cast<int>(progress * travel) - band;
	const int steps = 12;
	core::recti limit = card;
	if(clip)
		limit.clipAgainst(*clip);
	for(int i = 0; i < steps; ++i) {
		//the band is drawn as thin slanted slices (rects clipped to the card), brightest in the middle
		const float middle = 1.0f - std::fabs((i + 0.5f) / steps * 2.0f - 1.0f);
		const int a = static_cast<int>(180 * fade * middle);
		if(a <= 0)
			continue;
		const int x0 = card.UpperLeftCorner.X + pos + i * band / steps;
		for(int y = 0; y < h; y += 4) {
			const core::recti slice(x0 - y, card.UpperLeftCorner.Y + y, x0 - y + band / steps + 1, card.UpperLeftCorner.Y + y + 4);
			driver->draw2DRectangle(video::SColor(a, 255, 255, 255), slice, &limit);
		}
	}
}

void RarityFx::Draw3D(video::IVideoDriver* driver, const video::SMaterial& base, Rarity rarity, uint32_t time_ms, uint32_t alpha) {
	if(rarity == Rarity::NORMAL || alpha == 0)
		return;
	static const u16 indices[6] = { 0, 1, 2, 2, 1, 3 };
	const double seconds = time_ms / 1000.0 * SPEED;
	for(const auto& layer : LayersFor(rarity)) {
		auto* texture = GetPattern(driver, layer.pattern);
		if(!texture)
			continue;
		//the card front is 0.7 x 1.0, the picture a part of it (texture top is at y = -0.5)
		video::SMaterial material = base;
		material.setTexture(0, texture);
		for(const auto& strip : RegionStrips(layer.region)) {
		const float fx0 = strip[0], fx1 = strip[1], fy0 = strip[2], fy1 = strip[3];
		const float x0 = -0.35f + fx0 * 0.7f, x1 = -0.35f + fx1 * 0.7f;
		const float y0 = -0.5f + fy0, y1 = -0.5f + fy1;
		const float tile = 0.7f * layer.tile;
		const float ru = layer.pattern == Pattern::FLAT ? 1.0f : (x1 - x0) / tile;
		const float rv = layer.pattern == Pattern::FLAT ? 1.0f : (y1 - y0) / tile;
		const float u0 = layer.pattern == Pattern::FLAT ? 0.0f : Wrap01(static_cast<float>(seconds * layer.vx));
		const float v0 = layer.pattern == Pattern::FLAT ? 0.0f : Wrap01(static_cast<float>(seconds * layer.vy));
		const video::SColor color(ScaleAlpha(LayerColor(layer, seconds), alpha));
		const float z = 0.004f;
		video::S3DVertex v[4] = {
			video::S3DVertex(x0, y0, z, 0, 0, 1, color, u0, v0),
			video::S3DVertex(x1, y0, z, 0, 0, 1, color, u0 + ru, v0),
			video::S3DVertex(x0, y1, z, 0, 0, 1, color, u0, v0 + rv),
			video::S3DVertex(x1, y1, z, 0, 0, 1, color, u0 + ru, v0 + rv),
		};
		driver->setMaterial(material);
		driver->drawVertexPrimitiveList(v, 4, indices, 2);
		}
	}
}

}
