#ifndef RARITY_H
#define RARITY_H

#include <cstdint>
#include <unordered_map>
#include <vector>
#include <irrlicht.h>

namespace ygo {

//Card rarities as visual effects over the card picture (like in YGO Omega): foil sheens, glitter, rainbow patterns, tints.
//Every card can be given any rarity, the choice is saved per card (rarities.txt) and applies to all copies.
enum class Rarity : uint8_t {
	NORMAL,
	SR, UR, SCR, GR, PR, GST,
	COUNT
};

class RarityFx {
public:
	static void Load();
	static Rarity Get(uint32_t code);
	static void Set(uint32_t code, Rarity rarity);
	static const wchar_t* Name(Rarity rarity);
	//2D: the effect over a card picture drawn in `card` (browser grid, card preview)
	static void Draw2D(irr::video::IVideoDriver* driver, const irr::core::recti& card, Rarity rarity, uint32_t time_ms, const irr::core::recti* clip, bool rotated = false);
	//3D: the effect over the front of a card in a duel; `world` is the card's transform (already set), alpha 0-255
	static void Draw3D(irr::video::IVideoDriver* driver, const irr::video::SMaterial& base, Rarity rarity, uint32_t time_ms, uint32_t alpha);
	//the textures are rebuilt after the driver was reset
	static void ReleaseTextures();
private:
	static void Save();
	static inline std::unordered_map<uint32_t, Rarity> chosen;
};

}

#endif
