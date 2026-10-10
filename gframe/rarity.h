#ifndef RARITY_H
#define RARITY_H

#include <cstdint>
#include <deque>
#include <map>
#include <string>
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
	//the mouse position (screen pixels), updated every frame: foils over the card under the mouse shift with it and get a glint
	static void SetMouse(int x, int y) { mouse_x = x; mouse_y = y; }
	//a shine sweeping over a card that was just summoned (progress 0-1)
	static void DrawShine3D(irr::video::IVideoDriver* driver, const irr::video::SMaterial& base, float progress, uint32_t alpha);
	static void DrawSummonShine(irr::video::IVideoDriver* driver, const irr::core::recti& card, float progress, const irr::core::recti* clip);

	//Per-deck and per-copy choices. A deck can have its own table, saved next to it as deck/<name>.rarity:
	//a rarity for every copy of a card (the Nth copy in the deck) and/or one for all copies of a card.
	//Anything not set there falls back to the global choice.
	static void EditDeck(const std::wstring& name); //the deck the editor works on (called every frame, cheap)
	static bool DeckMode() { return edit.enabled; }
	//changes whenever the deck table the UI shows was switched, reset or edited from outside the picker
	static uint32_t Generation() { return generation; }
	static bool PerCopy() { return edit.per_copy; }
	static void SetDeckMode(bool on);
	static void SetPerCopy(bool on);
	//the rarity of one copy (ordinal = which copy of this card in the deck, -1 = not a particular copy)
	static Rarity GetFor(uint32_t code, int ordinal);
	static void SetFor(uint32_t code, int ordinal, Rarity rarity);
	static void ResetDeck(); //the rarities of the deck being edited
	static void ResetAll(); //every rarity: the global choices and all decks
	static void SaveDeckAs(const std::wstring& name);
	static void RenameDeck(const std::wstring& from, const std::wstring& to);
	static void DeleteDeck(const std::wstring& name);
	//a duel starts with this deck (codes in deck order): own cards then take the rarity of one copy each
	static void PrepareDuel(const std::wstring& name, const std::vector<uint32_t>& codes);
	static Rarity TakeDuelRarity(uint32_t code);
	static const std::wstring& DuelName() { return duel_name; }
private:
	struct DeckTable {
		bool enabled = false;
		bool per_copy = false;
		std::map<uint32_t, uint8_t> defaults; //code -> rarity for all copies
		std::map<uint32_t, std::vector<uint8_t>> copies; //code -> rarity per copy, 255 = not set
		Rarity Lookup(uint32_t code, int ordinal) const;
	};
	static DeckTable LoadTable(const std::wstring& name);
	static void SaveTable(const std::wstring& name, const DeckTable& table);
	static inline DeckTable edit;
	static inline std::wstring edit_name;
	static inline bool edit_loaded = false;
	static inline uint32_t generation = 0;
	static inline std::map<uint32_t, std::deque<Rarity>> duel_pool;
	static inline std::wstring duel_name;
	static inline int mouse_x = -10000, mouse_y = -10000;
	static void Save();
	static inline std::unordered_map<uint32_t, Rarity> chosen;
};

}

#endif
