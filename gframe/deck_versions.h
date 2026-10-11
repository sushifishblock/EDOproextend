#ifndef DECK_VERSIONS_H
#define DECK_VERSIONS_H

#include <filesystem>
#include <string>
#include <vector>
#include "deck_history.h"

namespace ygo {

//Older saved versions of a deck: deck/versions/<deckname>/<YYYYMMDD-HHMMSS>.ydk (plain deck files, about 1 KB each).
//At most 200 versions and 1 MB per deck are kept; the oldest ones are deleted first.
class DeckVersions {
public:
	static constexpr size_t MAX_COUNT = 200;
	static constexpr uint64_t MAX_BYTES = 1024 * 1024;
	//call before a deck is saved: keeps the file that is about to be replaced (when it differs from the newest version). Returns true.
	static bool Keep(const std::wstring& deck_name);
	static std::vector<VersionFile> List(const std::wstring& deck_name); //newest first
	static std::filesystem::path PathOf(const std::wstring& deck_name, const std::string& file_name);
	static void Delete(const std::wstring& deck_name, const std::string& file_name);
	static void Clear(const std::wstring& deck_name);
	static void RenameDeck(const std::wstring& from, const std::wstring& to);
	static void DeleteDeck(const std::wstring& name);
};

}

#endif
