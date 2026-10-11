#ifndef DECK_TAGS_H
#define DECK_TAGS_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include "hand_sim.h"

namespace ygo {

//The tags and hand rules of one deck, saved next to the deck as deck/<name>.tags
struct DeckTagData {
	std::map<uint32_t, Tag> tags; //card code -> tag
	std::vector<Rule> rules;
};

std::string SerializeTags(const DeckTagData& data);
//lines that cannot be understood are skipped; without any rule the default rules are used
DeckTagData ParseTags(const std::string& text);
std::vector<Rule> DefaultRules();
//a tag file only holds a-z, 0-9, spaces, new lines and underscores (checked before a file from a pack is written)
bool SafeTagText(const std::string& text);

class DeckTags {
public:
	//the deck the editor works on (cheap when it is the same deck as before)
	static void Edit(const std::wstring& deck_name);
	static Tag Get(uint32_t code);
	static void Set(uint32_t code, Tag tag); //saved at once
	static void Cycle(uint32_t code); //none -> starter -> extender -> handtrap -> brick -> none
	static void ResetTags(); //removes every tag of the deck (the rules stay)
	static std::vector<Rule>& Rules();
	static void SaveRules();
	static DeckTagData LoadFor(const std::wstring& deck_name);
	static std::string FileText(const std::wstring& deck_name); //"" when the deck has no file
	static void SaveDeckAs(const std::wstring& name);
	static void RenameDeck(const std::wstring& from, const std::wstring& to);
	static void DeleteDeck(const std::wstring& name);
	static void WriteText(const std::wstring& deck_name, const std::string& text); //used when a pack is installed
	static uint32_t Generation(); //changes whenever tags or rules changed or another deck was opened
};

}

#endif
