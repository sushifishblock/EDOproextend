#ifndef RARITY_RULES_H
#define RARITY_RULES_H

#include <cstdint>
#include <string>
#include <vector>

namespace ygo {

//Rarity rules: "every card of an archetype / type / attribute... gets this rarity" (rarity_rules.json, made in the Rarity Studio).
//A rule only decides for cards that have no rarity of their own. The first matching rule wins.
struct CardFacts {
	uint32_t type = 0;
	uint32_t attribute = 0;
	uint32_t level = 0;
	uint64_t race = 0;
	std::vector<uint16_t> setcodes;
	std::wstring name; //any case
};

struct RarityRule {
	std::string rarity; //"SR", "UR", "SCR", "GR", "PR", "GST", "C1".."C8"
	uint32_t setcode = 0; //0 = any archetype (matches sub-archetypes like the game does)
	uint32_t type_all = 0; //every one of these type bits must be set
	uint32_t type_none = 0; //none of these type bits may be set
	uint32_t attribute_any = 0; //the attribute is one of these (bit mask)
	uint64_t race_any = 0; //the race is one of these (bit mask)
	uint32_t level_min = 0; //0 = no limit
	uint32_t level_max = 0; //0 = no limit
	std::string name; //the card name contains this (UTF-8, any case); empty = any
	bool HasCondition() const;
	bool Matches(const CardFacts& card) const;
};

class RarityRules {
public:
	//{"rules":[{"rarity":"UR","setcode":7,"type_all":1,...}, ...]}. Returns false when the text is not a rule file (rules is left empty).
	static bool Parse(const std::string& json, std::vector<RarityRule>& rules);
	//the rules as the text of rarity_rules.json
	static std::string ToJson(const std::vector<RarityRule>& rules);
	//the first rule that matches, or nullptr
	static const RarityRule* Find(const std::vector<RarityRule>& rules, const CardFacts& card);
};

}

#endif
