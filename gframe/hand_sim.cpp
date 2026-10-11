#include "hand_sim.h"
#include <algorithm>
#include <random>

namespace ygo {

const char* TagName(Tag tag) {
	static const char* names[] = { "None", "Starter", "Extender", "Handtrap", "Brick" };
	return names[static_cast<int>(tag)];
}

SimResult Simulate(const std::vector<Tag>& deck, int hand, int runs, const std::vector<Rule>& rules, uint32_t seed) {
	SimResult result;
	result.hand = hand;
	result.deck_size = static_cast<int>(deck.size());
	result.rule_hits.assign(rules.size(), 0);
	if(hand < 1 || hand > static_cast<int>(deck.size()) || runs < 1)
		return result;
	result.runs = runs;
	std::mt19937 rng(seed);
	std::vector<Tag> work(deck);
	const int size = static_cast<int>(work.size());
	for(int run = 0; run < runs; ++run) {
		std::array<int, 5> in_hand{};
		for(int i = 0; i < hand; ++i) { //partial Fisher-Yates: the first `hand` cards of a random shuffle
			const int j = i + static_cast<int>(rng() % static_cast<uint32_t>(size - i));
			std::swap(work[i], work[j]);
			++in_hand[static_cast<int>(work[i])];
		}
		for(int t = 0; t < 5; ++t)
			++result.counts[t][std::min(in_hand[t], 3)];
		for(size_t r = 0; r < rules.size(); ++r) {
			bool ok = true;
			for(const auto& c : rules[r].conditions) {
				const int have = in_hand[static_cast<int>(c.tag)];
				if(c.exact ? have != c.n : have < c.n) {
					ok = false;
					break;
				}
			}
			if(ok)
				++result.rule_hits[r];
		}
	}
	return result;
}

}
