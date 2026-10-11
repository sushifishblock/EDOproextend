#include "../../gframe/rarity_rules.h"
#include <cstdio>
using namespace ygo;
static int failures = 0;
static void Expect(const char* name, bool ok) {
	std::printf("%s %s\n", ok ? "ok  " : "FAIL", name);
	if(!ok)
		++failures;
}
int main() {
	CardFacts blue_eyes; //Blue-Eyes White Dragon: normal monster, LIGHT dragon, level 8, archetype 0xdd
	blue_eyes.type = 0x11;
	blue_eyes.attribute = 0x10;
	blue_eyes.race = 0x2000;
	blue_eyes.level = 8;
	blue_eyes.setcodes = { 0xdd };
	blue_eyes.name = L"Blue-Eyes White Dragon";
	CardFacts spell;
	spell.type = 0x2;
	spell.name = L"Pot of Greed";

	std::vector<RarityRule> rules;
	Expect("archetype rule parses", RarityRules::Parse(R"({"rules":[{"rarity":"UR","setcode":221}]})", rules) && rules.size() == 1);
	Expect("archetype matches", RarityRules::Find(rules, blue_eyes) && RarityRules::Find(rules, blue_eyes)->rarity == "UR");
	Expect("other card does not match", RarityRules::Find(rules, spell) == nullptr);

	RarityRules::Parse(R"({"rules":[{"rarity":"SR","setcode":4317}]})", rules); //0x10dd: a sub-archetype
	Expect("sub-archetype needs the sub bits", RarityRules::Find(rules, blue_eyes) == nullptr);
	CardFacts sub = blue_eyes;
	sub.setcodes = { 0x10dd };
	Expect("sub-archetype card matches", RarityRules::Find(rules, sub) != nullptr);
	RarityRules::Parse(R"({"rules":[{"rarity":"SR","setcode":221}]})", rules);
	Expect("archetype also matches its sub-archetype cards", RarityRules::Find(rules, sub) != nullptr);

	RarityRules::Parse(R"({"rules":[{"rarity":"GR","type_all":3},{"rarity":"SCR","type_all":1,"attribute_any":16,"level_min":8}]})", rules);
	Expect("conditions are all required", RarityRules::Find(rules, blue_eyes)->rarity == "SCR");
	Expect("type_all needs every bit", RarityRules::Find(rules, spell) == nullptr);

	RarityRules::Parse(R"({"rules":[{"rarity":"UR","race_any":8192},{"rarity":"SR","type_all":1}]})", rules);
	Expect("first matching rule wins", RarityRules::Find(rules, blue_eyes)->rarity == "UR");

	RarityRules::Parse(R"({"rules":[{"rarity":"PR","type_all":1,"type_none":32}]})", rules);
	Expect("type_none excludes", RarityRules::Find(rules, blue_eyes) != nullptr);
	blue_eyes.type |= 0x20;
	Expect("type_none excludes effect monsters", RarityRules::Find(rules, blue_eyes) == nullptr);

	RarityRules::Parse(R"({"rules":[{"rarity":"C1","name":"BLUE-eyes"}]})", rules);
	Expect("name contains, any case", RarityRules::Find(rules, sub) != nullptr && RarityRules::Find(rules, spell) == nullptr);

	RarityRules::Parse(R"({"rules":[{"rarity":"UR","level_min":5,"level_max":7}]})", rules);
	Expect("level range", RarityRules::Find(rules, sub) == nullptr);

	Expect("a rule with no condition is kept but never matches", RarityRules::Parse(R"({"rules":[{"rarity":"UR"}]})", rules) && rules.size() == 1 && RarityRules::Find(rules, blue_eyes) == nullptr);
	Expect("a saved list reads back the same", [&] {
		std::vector<RarityRule> a, b;
		RarityRules::Parse(R"({"rules":[{"rarity":"SR","setcode":221,"type_all":1,"name":"dragon"},{"rarity":"UR"}]})", a);
		return RarityRules::Parse(RarityRules::ToJson(a), b) && b.size() == 2 && b[0].setcode == 221 && b[0].name == "dragon" && b[1].rarity == "UR";
	}());
	Expect("garbage is refused", !RarityRules::Parse("not json", rules) && rules.empty());
	Expect("wrong shape is refused", !RarityRules::Parse(R"({"rules":5})", rules));
	std::printf(failures ? "FAILED\n" : "ALL OK\n");
	return failures ? 1 : 0;
}
