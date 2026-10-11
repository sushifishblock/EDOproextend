#include "game.h"
#include <algorithm>
#include <irrlicht.h>
#include "deck_manager.h"
#include "deck_tags.h"
#include "hand_sim.h"
#include "menu_handler.h"

namespace ygo {

//The Odds window of the deck editor: widgets, rule editor and its events. The table is drawn in Game::DrawDeckOdds.

void Game::CreateOddsWindow() {
	wDeckOdds = env->addWindow(Scale(232, 50, 792, 590), false, L"Hand odds");
	wDeckOdds->getCloseButton()->setVisible(false);
	wDeckOdds->setVisible(false);
	env->addStaticText(L"Hand", Scale(15, 32, 60, 54), false, false, wDeckOdds);
	cbOddsHand = env->addComboBox(Scale(62, 32, 250, 54), wDeckOdds, COMBOBOX_ODDS_HAND);
	cbOddsHand->addItem(L"5 cards (going first)", 5);
	cbOddsHand->addItem(L"6 cards (going second)", 6);
	cbOddsHand->addItem(L"3 cards", 3);
	cbOddsHand->addItem(L"4 cards", 4);
	cbOddsHand->addItem(L"7 cards", 7);
	cbOddsHand->addItem(L"8 cards", 8);
	cbOddsHand->setSelected(0);
	env->addStaticText(L"Compare with", Scale(262, 32, 345, 54), false, false, wDeckOdds);
	cbOddsCompare = env->addComboBox(Scale(348, 32, 545, 54), wDeckOdds, COMBOBOX_ODDS_COMPARE);
	btnOddsReroll = env->addButton(Scale(460, 60, 545, 82), wDeckOdds, BUTTON_ODDS_REROLL, L"Re-roll");
	btnOddsResetTags = env->addButton(Scale(350, 60, 455, 82), wDeckOdds, BUTTON_ODDS_RESET_TAGS, L"Reset tags");
	env->addStaticText(L"Hand rules (a hand counts when every condition holds):", Scale(15, 326, 450, 344), false, false, wDeckOdds);
	cbOddsRule = env->addComboBox(Scale(15, 346, 300, 368), wDeckOdds, COMBOBOX_ODDS_RULE);
	env->addButton(Scale(305, 346, 375, 368), wDeckOdds, BUTTON_ODDS_ADD, L"Add rule");
	env->addButton(Scale(380, 346, 450, 368), wDeckOdds, BUTTON_ODDS_DELETE, L"Delete");
	env->addStaticText(L"Name", Scale(15, 374, 60, 396), false, false, wDeckOdds);
	ebOddsRuleName = env->addEditBox(L"", Scale(62, 374, 300, 396), true, wDeckOdds, EDITBOX_ODDS_NAME);
	for(int i = 0; i < 4; ++i) {
		const int y = 402 + 22 * i;
		cbOddsTag[i] = env->addComboBox(Scale(15, y, 150, y + 20), wDeckOdds, COMBOBOX_ODDS_TAG0 + i);
		cbOddsTag[i]->addItem(L"(not used)", 0);
		cbOddsTag[i]->addItem(L"Starter", 1);
		cbOddsTag[i]->addItem(L"Extender", 2);
		cbOddsTag[i]->addItem(L"Handtrap", 3);
		cbOddsTag[i]->addItem(L"Brick", 4);
		cbOddsCmp[i] = env->addComboBox(Scale(155, y, 260, y + 20), wDeckOdds, COMBOBOX_ODDS_CMP0 + i);
		cbOddsCmp[i]->addItem(L"at least", 0);
		cbOddsCmp[i]->addItem(L"exactly", 1);
		ebOddsN[i] = env->addEditBox(L"1", Scale(265, y, 300, y + 20), true, wDeckOdds, EDITBOX_ODDS_N0 + i);
	}
	env->addButton(Scale(230, 505, 330, 530), wDeckOdds, BUTTON_DECK_TOOLS_CLOSE, L"Close");
}

void Game::OpenOddsWindow() {
	wDeckStats->setVisible(false);
	wDeckDiff->setVisible(false);
	wDeckVersions->setVisible(false);
	//the saved decks to compare with, "(none)" first
	RefreshDeck(cbOddsCompare);
	std::vector<std::wstring> names;
	for(irr::u32 i = 0; i < cbOddsCompare->getItemCount(); ++i)
		names.emplace_back(cbOddsCompare->getItem(i));
	cbOddsCompare->clear();
	cbOddsCompare->addItem(L"(none)");
	for(const auto& name : names)
		cbOddsCompare->addItem(name.c_str());
	cbOddsCompare->setSelected(0);
	oddsRuleSel = 0;
	RefreshOddsRuleEditor();
	PopupElement(wDeckOdds);
}

void Game::RefreshOddsRuleEditor() {
	oddsRefreshing = true;
	const auto& rules = DeckTags::Rules();
	cbOddsRule->clear();
	for(const auto& rule : rules)
		cbOddsRule->addItem(std::wstring(rule.name.begin(), rule.name.end()).c_str());
	if(oddsRuleSel >= static_cast<int>(rules.size()))
		oddsRuleSel = static_cast<int>(rules.size()) - 1;
	const bool has = oddsRuleSel >= 0;
	if(has)
		cbOddsRule->setSelected(oddsRuleSel);
	ebOddsRuleName->setEnabled(has);
	ebOddsRuleName->setText(has ? std::wstring(rules[oddsRuleSel].name.begin(), rules[oddsRuleSel].name.end()).c_str() : L"");
	for(int i = 0; i < 4; ++i) {
		const bool row = has && i < static_cast<int>(rules[oddsRuleSel].conditions.size());
		cbOddsTag[i]->setEnabled(has);
		cbOddsCmp[i]->setEnabled(has);
		ebOddsN[i]->setEnabled(has);
		cbOddsTag[i]->setSelected(row ? static_cast<int>(rules[oddsRuleSel].conditions[i].tag) : 0);
		cbOddsCmp[i]->setSelected(row && rules[oddsRuleSel].conditions[i].exact ? 1 : 0);
		ebOddsN[i]->setText(row ? std::to_wstring(rules[oddsRuleSel].conditions[i].n).c_str() : L"1");
	}
	oddsRefreshing = false;
}

//the rule editor widgets -> the selected rule (saved with the deck)
void Game::ApplyOddsRuleEdits() {
	auto& rules = DeckTags::Rules();
	if(oddsRefreshing || oddsRuleSel < 0 || oddsRuleSel >= static_cast<int>(rules.size()))
		return;
	auto& rule = rules[oddsRuleSel];
	std::string name;
	for(const wchar_t* c = ebOddsRuleName->getText(); *c && name.size() < 40; ++c)
		if(*c >= 32 && *c < 127)
			name.push_back(static_cast<char>(*c));
	rule.name = name.empty() ? "Rule" : name;
	rule.conditions.clear();
	for(int i = 0; i < 4; ++i) {
		const int tag = static_cast<int>(cbOddsTag[i]->getItemData(cbOddsTag[i]->getSelected()));
		if(tag < 1 || tag > 4)
			continue;
		const int n = std::clamp(_wtoi(ebOddsN[i]->getText()), 0, 8);
		rule.conditions.push_back({ static_cast<Tag>(tag), cbOddsCmp[i]->getSelected() == 1, n });
	}
	DeckTags::SaveRules();
	RefillOddsRuleList();
}

//the names in the rule list (the editor boxes are left alone, so typing is not interrupted)
void Game::RefillOddsRuleList() {
	oddsRefreshing = true;
	cbOddsRule->clear();
	for(const auto& rule : DeckTags::Rules())
		cbOddsRule->addItem(std::wstring(rule.name.begin(), rule.name.end()).c_str());
	if(oddsRuleSel >= 0 && oddsRuleSel < static_cast<int>(cbOddsRule->getItemCount()))
		cbOddsRule->setSelected(oddsRuleSel);
	oddsRefreshing = false;
}

//returns true when the event was one of the Odds window
bool Game::OnOddsEvent(const irr::SEvent& event) {
	if(event.EventType != irr::EET_GUI_EVENT)
		return false;
	const int id = event.GUIEvent.Caller->getID();
	switch(event.GUIEvent.EventType) {
	case irr::gui::EGET_BUTTON_CLICKED: {
		auto& rules = DeckTags::Rules();
		switch(id) {
		case BUTTON_DECK_ODDS:
			OpenOddsWindow();
			return true;
		case BUTTON_ODDS_RESET_TAGS:
			//two clicks, so the tags are not lost by accident
			if(!oddsResetArmed) {
				oddsResetArmed = true;
				oddsResetArmTime = device->getTimer()->getTime();
				btnOddsResetTags->setText(L"Sure? Click again");
			} else {
				oddsResetArmed = false;
				btnOddsResetTags->setText(L"Reset tags");
				DeckTags::ResetTags();
			}
			return true;
		case BUTTON_ODDS_REROLL:
			oddsSeed = oddsSeed * 1664525u + 1013904223u;
			return true;
		case BUTTON_ODDS_ADD:
			if(rules.size() < 8) {
				Rule rule;
				rule.name = "New rule";
				rule.conditions = { { Tag::STARTER, false, 1 } };
				rules.push_back(rule);
				oddsRuleSel = static_cast<int>(rules.size()) - 1;
				DeckTags::SaveRules();
				RefreshOddsRuleEditor();
			}
			return true;
		case BUTTON_ODDS_DELETE:
			if(oddsRuleSel >= 0 && oddsRuleSel < static_cast<int>(rules.size())) {
				rules.erase(rules.begin() + oddsRuleSel);
				DeckTags::SaveRules();
				RefreshOddsRuleEditor();
			}
			return true;
		default:
			return false;
		}
	}
	case irr::gui::EGET_COMBO_BOX_CHANGED:
		if(id == COMBOBOX_ODDS_RULE) {
			if(!oddsRefreshing) {
				oddsRuleSel = cbOddsRule->getSelected();
				RefreshOddsRuleEditor();
			}
			return true;
		}
		if((id >= COMBOBOX_ODDS_TAG0 && id < COMBOBOX_ODDS_TAG0 + 4) || (id >= COMBOBOX_ODDS_CMP0 && id < COMBOBOX_ODDS_CMP0 + 4)) {
			ApplyOddsRuleEdits();
			return true;
		}
		return id == COMBOBOX_ODDS_HAND || id == COMBOBOX_ODDS_COMPARE;
	case irr::gui::EGET_EDITBOX_CHANGED:
		if(id == EDITBOX_ODDS_NAME || (id >= EDITBOX_ODDS_N0 && id < EDITBOX_ODDS_N0 + 4)) {
			ApplyOddsRuleEdits();
			return true;
		}
		return false;
	default:
		return false;
	}
}

}
