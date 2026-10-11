#ifndef HAND_SIM_H
#define HAND_SIM_H

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace ygo {

//What a card is for in a hand (the user tags the cards of a deck)
enum class Tag : uint8_t { NONE, STARTER, EXTENDER, HANDTRAP, BRICK, COUNT };

//"at least n" (or "exactly n" when exact) cards of a tag in the opening hand
struct Condition {
	Tag tag = Tag::STARTER;
	bool exact = false;
	int n = 1;
};

//a hand is "good" when all conditions of a rule hold
struct Rule {
	std::string name;
	std::vector<Condition> conditions;
};

struct SimResult {
	int runs = 0, hand = 0, deck_size = 0;
	std::array<std::array<int, 4>, 5> counts{}; //[tag][0, 1, 2, 3 or more cards of that tag]: number of hands
	std::vector<int> rule_hits;                 //per rule: number of hands that satisfy it
	double Percent(int hands) const { return runs ? 100.0 * hands / runs : 0.0; }
};

//Deals `runs` random opening hands of `hand` cards from the deck (given as the tag of every card) and counts them.
//Gives runs == 0 when the deck has fewer cards than the hand.
SimResult Simulate(const std::vector<Tag>& deck, int hand, int runs, const std::vector<Rule>& rules, uint32_t seed);
const char* TagName(Tag tag);

}

#endif
