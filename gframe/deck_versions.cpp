#include "deck_versions.h"
#include <algorithm>
#include <ctime>
#include <fstream>
#include <sstream>

namespace ygo {

namespace {
std::filesystem::path FolderOf(const std::wstring& deck_name) {
	return std::filesystem::path(L"deck/versions/" + deck_name);
}
std::string ReadAll(const std::filesystem::path& path) {
	std::ifstream file(path, std::ios::binary);
	std::ostringstream text;
	text << file.rdbuf();
	return text.str();
}
}

std::vector<VersionFile> DeckVersions::List(const std::wstring& deck_name) {
	std::vector<VersionFile> files;
	std::error_code ec;
	for(const auto& entry : std::filesystem::directory_iterator(FolderOf(deck_name), ec)) {
		if(entry.path().extension() != ".ydk")
			continue;
		VersionFile file;
		file.name = entry.path().filename().string();
		file.size = entry.file_size(ec);
		files.push_back(file);
	}
	std::sort(files.begin(), files.end(), [](const VersionFile& a, const VersionFile& b) { return a.name > b.name; });
	return files;
}

std::filesystem::path DeckVersions::PathOf(const std::wstring& deck_name, const std::string& file_name) {
	return FolderOf(deck_name) / std::filesystem::path(file_name);
}

bool DeckVersions::Keep(const std::wstring& deck_name) {
	try {
		const std::filesystem::path source(L"deck/" + deck_name + L".ydk");
		std::error_code ec;
		if(deck_name.empty() || !std::filesystem::exists(source, ec))
			return true;
		const std::string content = ReadAll(source);
		auto files = List(deck_name);
		if(!files.empty() && ReadAll(PathOf(deck_name, files.front().name)) == content)
			return true; //nothing new
		std::filesystem::create_directories(FolderOf(deck_name), ec);
		const std::time_t now = std::time(nullptr);
		std::tm local{};
		localtime_s(&local, &now);
		char name[32];
		std::strftime(name, sizeof(name), "%Y%m%d-%H%M%S", &local);
		std::string file_name = std::string(name) + ".ydk";
		if(!files.empty() && files.front().name >= file_name) //two saves in one second
			file_name = std::string(name) + "b.ydk";
		std::ofstream out(PathOf(deck_name, file_name), std::ios::binary | std::ios::trunc);
		out << content;
		out.close();
		//stay within the limits
		files = List(deck_name);
		for(const size_t index : VersionsToPrune(files, MAX_COUNT, MAX_BYTES))
			std::filesystem::remove(PathOf(deck_name, files[index].name), ec);
	} catch(...) {
		//a version that could not be kept never stops a save
	}
	return true;
}

void DeckVersions::Delete(const std::wstring& deck_name, const std::string& file_name) {
	if(file_name.find_first_of("/\\") != std::string::npos)
		return;
	std::error_code ec;
	std::filesystem::remove(PathOf(deck_name, file_name), ec);
}

void DeckVersions::Clear(const std::wstring& deck_name) {
	std::error_code ec;
	std::filesystem::remove_all(FolderOf(deck_name), ec);
}

void DeckVersions::RenameDeck(const std::wstring& from, const std::wstring& to) {
	std::error_code ec;
	if(std::filesystem::exists(FolderOf(from), ec)) {
		std::filesystem::remove_all(FolderOf(to), ec);
		std::filesystem::rename(FolderOf(from), FolderOf(to), ec);
	}
}

void DeckVersions::DeleteDeck(const std::wstring& name) {
	Clear(name);
}

}
