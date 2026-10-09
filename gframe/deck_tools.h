#ifndef DECK_TOOLS_H
#define DECK_TOOLS_H

#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include "deck.h"

namespace ygo {

//Numbers about a deck for the deck statistics panel.
struct DeckStats {
	int main_total = 0, extra_total = 0, side_total = 0;
	int monsters = 0, spells = 0, traps = 0;
	int mon_normal = 0, mon_effect = 0, mon_ritual = 0, mon_pendulum = 0, mon_tuner = 0;
	int spell_normal = 0, spell_quick = 0, spell_continuous = 0, spell_equip = 0, spell_field = 0, spell_ritual = 0;
	int trap_normal = 0, trap_continuous = 0, trap_counter = 0;
	int extra_fusion = 0, extra_synchro = 0, extra_xyz = 0, extra_link = 0;
	std::array<int, 14> levels{}; //index 1-12 = that level, 13 = anything higher or other
	std::array<int, 7> attributes{}; //earth, water, fire, wind, light, dark, divine
	std::vector<std::pair<int, uint64_t>> races; //count, race bit - sorted, most first
	double average_level = 0;
	static DeckStats Compute(const Deck& deck);
};

//The difference between two decks, as lines for a list.
struct DeckDiffLine {
	std::wstring text;
	uint32_t color; //ARGB
};
std::vector<DeckDiffLine> DiffDecks(const Deck& mine, const Deck& other, const std::wstring& other_name);

}

#endif
