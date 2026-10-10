#include "deck_tools.h"
#include <algorithm>
#include <map>
#include "data_manager.h"
#include "ocgapi_constants.h"

namespace ygo {

DeckStats DeckStats::Compute(const Deck& deck) {
	DeckStats s;
	s.main_total = static_cast<int>(deck.main.size());
	s.extra_total = static_cast<int>(deck.extra.size());
	s.side_total = static_cast<int>(deck.side.size());
	std::map<int, int> race_counts;
	long long level_sum = 0;
	int level_count = 0;
	for(const auto* card : deck.main) {
		if(!card || (!card->code && !card->alias))
			continue;
		const uint32_t type = card->type;
		if(type & TYPE_MONSTER) {
			++s.monsters;
			if(type & TYPE_NORMAL)
				++s.mon_normal;
			else if(type & TYPE_RITUAL)
				++s.mon_ritual;
			if(type & TYPE_EFFECT)
				++s.mon_effect;
			if(type & TYPE_PENDULUM)
				++s.mon_pendulum;
			if(type & TYPE_TUNER)
				++s.mon_tuner;
			const auto level = static_cast<int32_t>(card->level);
			if(level >= 1 && level <= 12) {
				++s.levels[level];
				level_sum += level;
				++level_count;
			} else if(level > 12 && level < 100) {
				++s.levels[13];
			}
			for(int i = 0; i < 7; ++i)
				if(card->attribute & (1u << i))
					++s.attributes[i];
			for(int i = 0; i < 63; ++i)
				if(card->race & (1ull << i))
					++race_counts[i];
		} else if(type & TYPE_SPELL) {
			++s.spells;
			if(type & TYPE_QUICKPLAY)
				++s.spell_quick;
			else if(type & TYPE_CONTINUOUS)
				++s.spell_continuous;
			else if(type & TYPE_EQUIP)
				++s.spell_equip;
			else if(type & TYPE_FIELD)
				++s.spell_field;
			else if(type & TYPE_RITUAL)
				++s.spell_ritual;
			else
				++s.spell_normal;
		} else if(type & TYPE_TRAP) {
			++s.traps;
			if(type & TYPE_CONTINUOUS)
				++s.trap_continuous;
			else if(type & TYPE_COUNTER)
				++s.trap_counter;
			else
				++s.trap_normal;
		}
	}
	for(const auto* card : deck.extra) {
		if(!card)
			continue;
		const uint32_t type = card->type;
		if(type & TYPE_FUSION)
			++s.extra_fusion;
		else if(type & TYPE_SYNCHRO)
			++s.extra_synchro;
		else if(type & TYPE_XYZ)
			++s.extra_xyz;
		else if(type & TYPE_LINK)
			++s.extra_link;
	}
	for(const auto& entry : race_counts)
		s.races.emplace_back(entry.second, 1ull << entry.first);
	std::stable_sort(s.races.begin(), s.races.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
	if(level_count)
		s.average_level = static_cast<double>(level_sum) / level_count;
	return s;
}

namespace {
struct Entry {
	int mine = 0, other = 0;
	std::wstring name;
};
void Add(std::map<uint32_t, Entry>& map, const Deck::Vector& pile, bool mine) {
	for(const auto* card : pile) {
		if(!card)
			continue;
		const uint32_t key = !card->code ? card->alias : (CardDataC::IsInArtworkOffsetRange(card) ? card->alias : card->code);
		auto& entry = map[key];
		if(entry.name.empty()) {
			const auto name = gDataManager->GetName(key);
			entry.name = name.empty() ? L"#" + std::to_wstring(key) : std::wstring(name);
		}
		(mine ? entry.mine : entry.other)++;
	}
}
}

std::vector<DeckDiffLine> DiffDecks(const Deck& mine, const Deck& other, const std::wstring& other_name) {
	std::map<uint32_t, Entry> cards;
	for(const auto* pile : { &mine.main, &mine.extra, &mine.side })
		Add(cards, *pile, true);
	for(const auto* pile : { &other.main, &other.extra, &other.side })
		Add(cards, *pile, false);
	std::vector<const Entry*> only_mine, only_other, different;
	int same = 0;
	for(const auto& item : cards) {
		const auto& e = item.second;
		if(e.mine && !e.other)
			only_mine.push_back(&e);
		else if(!e.mine && e.other)
			only_other.push_back(&e);
		else if(e.mine != e.other)
			different.push_back(&e);
		else
			same += e.mine;
	}
	const auto by_name = [](const Entry* a, const Entry* b) { return a->name < b->name; };
	std::sort(only_mine.begin(), only_mine.end(), by_name);
	std::sort(only_other.begin(), only_other.end(), by_name);
	std::sort(different.begin(), different.end(), by_name);
	const auto total = [](const std::vector<const Entry*>& list, bool use_mine) {
		int n = 0;
		for(const auto* e : list)
			n += use_mine ? e->mine : e->other;
		return n;
	};
	std::vector<DeckDiffLine> lines;
	lines.push_back({ L"Comparing this deck with \"" + other_name + L"\"", 0xffffffff });
	lines.push_back({ std::to_wstring(same) + L" cards are identical in both (same card, same number of copies)", 0xffb0b0b0 });
	lines.push_back({ L"", 0xffffffff });
	lines.push_back({ L"Only in this deck (" + std::to_wstring(total(only_mine, true)) + L" cards)", 0xff80ff80 });
	for(const auto* e : only_mine)
		lines.push_back({ L"   +" + std::to_wstring(e->mine) + L"  " + e->name, 0xff80ff80 });
	if(only_mine.empty())
		lines.push_back({ L"   -", 0xffb0b0b0 });
	lines.push_back({ L"", 0xffffffff });
	lines.push_back({ L"Only in \"" + other_name + L"\" (" + std::to_wstring(total(only_other, false)) + L" cards)", 0xffff8080 });
	for(const auto* e : only_other)
		lines.push_back({ L"   -" + std::to_wstring(e->other) + L"  " + e->name, 0xffff8080 });
	if(only_other.empty())
		lines.push_back({ L"   -", 0xffb0b0b0 });
	lines.push_back({ L"", 0xffffffff });
	lines.push_back({ L"Different number of copies (" + std::to_wstring(different.size()) + L")", 0xffffd060 });
	for(const auto* e : different)
		lines.push_back({ L"   " + e->name + L":  " + std::to_wstring(e->mine) + L" here, " + std::to_wstring(e->other) + L" there", 0xffffd060 });
	if(different.empty())
		lines.push_back({ L"   -", 0xffb0b0b0 });
	return lines;
}

}
