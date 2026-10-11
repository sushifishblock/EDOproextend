#include "game.h"
#include <irrlicht.h>
#include "deck_manager.h"
#include "deck_tools.h"
#include "deck_versions.h"
#include "menu_handler.h"
#include "utils.h"

namespace ygo {

//Undo / redo of the deck being edited (kept in memory) and the Versions window (older saved versions of the deck).

namespace {
DeckSnapshot SnapshotOf(const Deck& deck) {
	DeckSnapshot snapshot;
	for(const auto* card : deck.main)
		snapshot.main.push_back(card->getRealCode());
	for(const auto* card : deck.extra)
		snapshot.extra.push_back(card->getRealCode());
	for(const auto* card : deck.side)
		snapshot.side.push_back(card->getRealCode());
	return snapshot;
}

Deck DeckOf(const DeckSnapshot& snapshot) {
	//the real card data when the card is known (a placeholder only for cards the database does not have, like a loaded deck)
	const auto resolve = [](uint32_t code) -> const CardDataC* {
		if(const auto* data = gDataManager->GetCardData(code))
			return data;
		if(const auto* mapped = gDataManager->GetMappedCardData(code))
			return mapped;
		return gdeckManager->GetDummyOrMappedCardData(code);
	};
	Deck deck;
	for(const auto code : snapshot.main)
		deck.main.push_back(resolve(code));
	for(const auto code : snapshot.extra)
		deck.extra.push_back(resolve(code));
	for(const auto code : snapshot.side)
		deck.side.push_back(resolve(code));
	return deck;
}

//"20261011-031244.ydk" -> "2026-10-11 03:12:44"
std::wstring TimeText(const std::string& name) {
	if(name.size() < 15)
		return std::wstring(name.begin(), name.end());
	const std::string t = name.substr(0, 4) + "-" + name.substr(4, 2) + "-" + name.substr(6, 2) + " " + name.substr(9, 2) + ":" + name.substr(11, 2) + ":" + name.substr(13, 2);
	return std::wstring(t.begin(), t.end());
}
}

std::wstring Game::CurrentDeckName() {
	const int sel = cbDBDecks->getSelected();
	return sel >= 0 ? std::wstring(cbDBDecks->getItem(sel)) : std::wstring();
}

void Game::UpdateDeckHistory() {
	if(versionsClearArmed && device->getTimer()->getTime() - versionsClearArmTime > 4000) {
		versionsClearArmed = false;
		btnVersionsClear->setText(L"Clear versions");
	}
	if(!is_building || is_siding) {
		deckHistoryValid = false;
		return;
	}
	const auto snapshot = SnapshotOf(deckBuilder.GetCurrentDeck());
	const std::wstring name = CurrentDeckName();
	if(!deckHistoryValid) {
		deckHistory.Reset(snapshot);
		deckHistoryName = name;
		deckHistoryValid = true;
	} else if(name != deckHistoryName) {
		//another deck was opened: a new history; the same deck under a new name (Save as, Rename) keeps its history
		if(const auto* current = deckHistory.Current(); current && *current == snapshot)
			deckHistoryName = name;
		else {
			deckHistory.Reset(snapshot);
			deckHistoryName = name;
		}
	} else if(!deckBuilder.is_draging) {
		deckHistory.Record(snapshot); //(a card being dragged is not in the deck for a moment: not a change)
	}
	btnDeckUndo->setEnabled(deckHistory.CanUndo());
	btnDeckRedo->setEnabled(deckHistory.CanRedo());
}

void Game::DeckUndo(bool redo) {
	if(!is_building || is_siding || !deckHistoryValid || deckBuilder.is_draging)
		return;
	UpdateDeckHistory(); //records a change made this very frame first
	const DeckSnapshot* state = redo ? deckHistory.Redo() : deckHistory.Undo();
	if(!state)
		return;
	deckBuilder.SetCurrentDeck(DeckOf(*state));
	UpdateDeckHistory();
}

void Game::CreateVersionsWindow() {
	wDeckVersions = env->addWindow(Scale(232, 50, 792, 590), false, L"Saved versions");
	wDeckVersions->getCloseButton()->setVisible(false);
	wDeckVersions->setVisible(false);
	env->addStaticText(L"Every Save keeps the file it replaces here (the newest 200 or 1 MB per deck).", Scale(15, 30, 545, 48), false, false, wDeckVersions);
	lstVersions = env->addListBox(Scale(15, 52, 545, 220), wDeckVersions, LISTBOX_VERSIONS, true);
	env->addStaticText(L"Changes between your deck now and the selected version:", Scale(15, 226, 545, 244), false, false, wDeckVersions);
	lstVersionDiff = env->addListBox(Scale(15, 246, 545, 470), wDeckVersions, -1, true);
	env->addButton(Scale(15, 478, 125, 502), wDeckVersions, BUTTON_VERSION_RESTORE, L"Restore");
	env->addButton(Scale(130, 478, 215, 502), wDeckVersions, BUTTON_VERSION_DELETE, L"Delete");
	btnVersionsClear = env->addButton(Scale(220, 478, 360, 502), wDeckVersions, BUTTON_VERSION_CLEAR, L"Clear versions");
	env->addButton(Scale(445, 505, 545, 530), wDeckVersions, BUTTON_DECK_TOOLS_CLOSE, L"Close");
}

void Game::OpenVersionsWindow() {
	wDeckStats->setVisible(false);
	wDeckDiff->setVisible(false);
	wDeckOdds->setVisible(false);
	versionsDeckName = CurrentDeckName();
	RefreshVersionsList();
	PopupElement(wDeckVersions);
}

void Game::RefreshVersionsList() {
	lstVersions->clear();
	lstVersionDiff->clear();
	versionFiles = DeckVersions::List(versionsDeckName);
	if(versionsDeckName.empty()) {
		lstVersions->addItem(L"Save the deck first: versions are kept for saved decks.");
		versionFiles.clear();
		return;
	}
	for(const auto& file : versionFiles) {
		Deck deck;
		std::wstring text = TimeText(file.name);
		if(DeckManager::LoadDeckFromFile(Utils::ToPathString(DeckVersions::PathOf(versionsDeckName, file.name).wstring()), deck, true))
			text += L"     Main " + std::to_wstring(deck.main.size()) + L"   Extra " + std::to_wstring(deck.extra.size()) + L"   Side " + std::to_wstring(deck.side.size());
		lstVersions->addItem(text.c_str());
	}
	if(versionFiles.empty())
		lstVersions->addItem(L"No older versions yet: the next Save keeps the file it replaces.");
	else
		lstVersions->setSelected(0);
	ShowSelectedVersionDiff();
}

void Game::ShowSelectedVersionDiff() {
	lstVersionDiff->clear();
	const int sel = lstVersions->getSelected();
	if(sel < 0 || sel >= static_cast<int>(versionFiles.size()))
		return;
	Deck deck;
	if(!DeckManager::LoadDeckFromFile(Utils::ToPathString(DeckVersions::PathOf(versionsDeckName, versionFiles[sel].name).wstring()), deck, true)) {
		lstVersionDiff->addItem(L"This version could not be read.");
		return;
	}
	const auto lines = DiffDecks(deckBuilder.GetCurrentDeck(), deck, L"this version");
	for(size_t i = 0; i < lines.size(); ++i) {
		lstVersionDiff->addItem(lines[i].text.c_str());
		lstVersionDiff->setItemOverrideColor(static_cast<irr::u32>(i), irr::video::SColor(lines[i].color));
	}
}

//returns true when the event belonged to the Versions window, the undo / redo buttons or the versions button
bool Game::OnVersionsEvent(const irr::SEvent& event) {
	if(event.EventType != irr::EET_GUI_EVENT)
		return false;
	const int id = event.GUIEvent.Caller->getID();
	if(event.GUIEvent.EventType == irr::gui::EGET_LISTBOX_CHANGED && id == LISTBOX_VERSIONS) {
		ShowSelectedVersionDiff();
		return true;
	}
	if(event.GUIEvent.EventType != irr::gui::EGET_BUTTON_CLICKED)
		return false;
	const int sel = lstVersions ? lstVersions->getSelected() : -1;
	const bool has = sel >= 0 && sel < static_cast<int>(versionFiles.size());
	switch(id) {
	case BUTTON_DECK_UNDO:
		DeckUndo(false);
		return true;
	case BUTTON_DECK_REDO:
		DeckUndo(true);
		return true;
	case BUTTON_DECK_VERSIONS:
		OpenVersionsWindow();
		return true;
	case BUTTON_VERSION_RESTORE: {
		Deck deck;
		if(has && DeckManager::LoadDeckFromFile(Utils::ToPathString(DeckVersions::PathOf(versionsDeckName, versionFiles[sel].name).wstring()), deck, true)) {
			deckBuilder.SetCurrentDeck(deck);
			UpdateDeckHistory();
			ShowSelectedVersionDiff();
			stACMessage->setText(L"Version restored. Ctrl+Z undoes it.");
			PopupElement(wACMessage, 30);
		}
		return true;
	}
	case BUTTON_VERSION_DELETE:
		if(has) {
			DeckVersions::Delete(versionsDeckName, versionFiles[sel].name);
			RefreshVersionsList();
		}
		return true;
	case BUTTON_VERSION_CLEAR:
		//two clicks, so the history is not lost by accident
		if(!versionsClearArmed) {
			versionsClearArmed = true;
			versionsClearArmTime = device->getTimer()->getTime();
			btnVersionsClear->setText(L"Sure? Click again");
		} else {
			versionsClearArmed = false;
			btnVersionsClear->setText(L"Clear versions");
			DeckVersions::Clear(versionsDeckName);
			RefreshVersionsList();
		}
		return true;
	default:
		return false;
	}
}

}
