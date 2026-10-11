#include "../gframe/deck_tags.h"
#include <cstdio>
using namespace ygo;
static int failures = 0;
static void Expect(const char* name, bool ok) {
	std::printf("%s %s\n", ok ? "ok  " : "FAIL", name);
	if(!ok)
		++failures;
}
int main() {
	DeckTagData data;
	data.tags[12345] = Tag::STARTER;
	data.tags[777] = Tag::BRICK;
	Rule rule;
	rule.name = "My rule";
	rule.conditions = { { Tag::HANDTRAP, false, 2 }, { Tag::BRICK, true, 0 } };
	data.rules = { rule };
	const std::string text = SerializeTags(data);
	Expect("serialized text is safe", SafeTagText(text));
	const auto back = ParseTags(text);
	Expect("tags round trip", back.tags.size() == 2 && back.tags.at(12345) == Tag::STARTER && back.tags.at(777) == Tag::BRICK);
	//the name is saved in lower case and comes back with a capital letter: "My rule"
	Expect("rule round trip", back.rules.size() == 1 && back.rules[0].name == "My rule" && back.rules[0].conditions.size() == 2
		&& back.rules[0].conditions[0].tag == Tag::HANDTRAP && !back.rules[0].conditions[0].exact && back.rules[0].conditions[0].n == 2
		&& back.rules[0].conditions[1].exact && back.rules[0].conditions[1].n == 0);
	const auto garbled = ParseTags("t abc 9\nr x\nzzz\nt 5 2\n");
	Expect("garbled lines are skipped", garbled.tags.size() == 1 && garbled.tags.at(5) == Tag::EXTENDER);
	Expect("default rules when none", garbled.rules.size() == 2 && DefaultRules().size() == 2);
	Expect("unsafe text rejected", !SafeTagText("t 1 1\n../evil") && !SafeTagText("A"));
	DeckTags::Edit(L"testdeck_does_not_exist");
	DeckTags::Cycle(42);
	Expect("cycle none -> starter", DeckTags::Get(42) == Tag::STARTER);
	for(int i = 0; i < 4; ++i)
		DeckTags::Cycle(42);
	Expect("cycle comes back to none after 5 steps", DeckTags::Get(42) == Tag::NONE);
	std::printf(failures ? "FAILED\n" : "ALL OK\n");
	return failures ? 1 : 0;
}
