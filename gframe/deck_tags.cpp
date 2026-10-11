#include "deck_tags.h"
#include <filesystem>
#include <fstream>
#include <sstream>

namespace ygo {

namespace {
const char* const TAG_WORDS[] = { "none", "starter", "extender", "handtrap", "brick" };
std::wstring edit_name;
bool edit_loaded = false;
DeckTagData edit_data;
uint32_t generation = 0;

int TagFromWord(const std::string& word) {
	for(int i = 1; i <= 4; ++i)
		if(word == TAG_WORDS[i])
			return i;
	return 0;
}

std::filesystem::path FileFor(const std::wstring& name) {
	return std::filesystem::path(L"deck/" + name + L".tags");
}

std::string ReadAll(const std::filesystem::path& path) {
	std::ifstream file(path, std::ios::binary);
	std::ostringstream text;
	text << file.rdbuf();
	return text.str();
}

//write to a temporary file and rename it over the target
void WriteAtomic(const std::filesystem::path& path, const std::string& content) {
	std::filesystem::path temp = path;
	temp += ".tmp";
	{
		std::ofstream file(temp, std::ios::binary | std::ios::trunc);
		if(!file.is_open())
			return;
		file << content;
		if(!file.good())
			return;
	}
	std::error_code ec;
	std::filesystem::rename(temp, path, ec);
}

void SaveEdited() {
	if(!edit_name.empty())
		WriteAtomic(FileFor(edit_name), SerializeTags(edit_data));
}
}

std::vector<Rule> DefaultRules() {
	Rule good;
	good.name = "Good hand";
	good.conditions = { { Tag::STARTER, false, 1 }, { Tag::BRICK, true, 0 } };
	Rule trap;
	trap.name = "Hand with handtrap";
	trap.conditions = { { Tag::STARTER, false, 1 }, { Tag::HANDTRAP, false, 1 } };
	return { good, trap };
}

bool SafeTagText(const std::string& text) {
	if(text.size() > 100 * 1024)
		return false;
	for(const char c : text) {
		if(!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || c == ' ' || c == '\n' || c == '\r' || c == '_'))
			return false;
	}
	return true;
}

std::string SerializeTags(const DeckTagData& data) {
	std::ostringstream out;
	for(const auto& entry : data.tags) {
		if(entry.second != Tag::NONE)
			out << "t " << entry.first << " " << static_cast<int>(entry.second) << "\n";
	}
	for(const auto& rule : data.rules) {
		std::string name = rule.name;
		for(auto& c : name) {
			if(c == ' ')
				c = '_';
			else if(c >= 'A' && c <= 'Z')
				c = static_cast<char>(c - 'A' + 'a');
			else if(!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
				c = '_';
		}
		out << "r " << (name.empty() ? "rule" : name);
		for(const auto& c : rule.conditions)
			out << " " << TAG_WORDS[static_cast<int>(c.tag)] << " " << (c.exact ? "eq" : "ge") << " " << c.n;
		out << "\n";
	}
	return out.str();
}

DeckTagData ParseTags(const std::string& text) {
	DeckTagData data;
	std::istringstream in(text);
	std::string line;
	while(std::getline(in, line)) {
		std::istringstream words(line);
		std::string kind;
		words >> kind;
		if(kind == "t") {
			uint32_t code = 0;
			int tag = 0;
			if(words >> code >> tag && tag >= 1 && tag <= 4)
				data.tags[code] = static_cast<Tag>(tag);
		} else if(kind == "r" && data.rules.size() < 8) {
			Rule rule;
			words >> rule.name;
			if(rule.name.empty())
				continue;
			for(auto& c : rule.name)
				if(c == '_')
					c = ' ';
			if(rule.name[0] >= 'a' && rule.name[0] <= 'z') //names are saved in lower case: show them with a capital letter
				rule.name[0] = static_cast<char>(rule.name[0] - 'a' + 'A');
			std::string tag, cmp;
			int n = 0;
			while(rule.conditions.size() < 4 && words >> tag >> cmp >> n) {
				const int t = TagFromWord(tag);
				if(!t || (cmp != "ge" && cmp != "eq") || n < 0 || n > 8)
					continue;
				rule.conditions.push_back({ static_cast<Tag>(t), cmp == "eq", n });
			}
			if(!rule.conditions.empty())
				data.rules.push_back(rule);
		}
	}
	if(data.rules.empty())
		data.rules = DefaultRules();
	return data;
}

void DeckTags::Edit(const std::wstring& deck_name) {
	if(edit_loaded && deck_name == edit_name)
		return;
	edit_loaded = true;
	edit_name = deck_name;
	edit_data = deck_name.empty() ? ParseTags("") : LoadFor(deck_name);
	++generation;
}

Tag DeckTags::Get(uint32_t code) {
	const auto it = edit_data.tags.find(code);
	return it == edit_data.tags.end() ? Tag::NONE : it->second;
}

void DeckTags::Set(uint32_t code, Tag tag) {
	if(tag == Tag::NONE)
		edit_data.tags.erase(code);
	else
		edit_data.tags[code] = tag;
	++generation;
	SaveEdited(); //the rules are kept in the same file
}

void DeckTags::Cycle(uint32_t code) {
	Set(code, static_cast<Tag>((static_cast<int>(Get(code)) + 1) % 5));
}

void DeckTags::ResetTags() {
	edit_data.tags.clear();
	++generation;
	SaveEdited();
}

std::vector<Rule>& DeckTags::Rules() {
	return edit_data.rules;
}

void DeckTags::SaveRules() {
	++generation;
	SaveEdited();
}

DeckTagData DeckTags::LoadFor(const std::wstring& deck_name) {
	if(deck_name.empty())
		return ParseTags("");
	return ParseTags(ReadAll(FileFor(deck_name)));
}

std::string DeckTags::FileText(const std::wstring& deck_name) {
	std::error_code ec;
	if(deck_name.empty() || !std::filesystem::exists(FileFor(deck_name), ec))
		return std::string();
	return ReadAll(FileFor(deck_name));
}

void DeckTags::SaveDeckAs(const std::wstring& name) {
	edit_name = name;
	edit_loaded = true;
	if(!edit_data.tags.empty())
		SaveEdited();
}

void DeckTags::RenameDeck(const std::wstring& from, const std::wstring& to) {
	std::error_code ec;
	if(std::filesystem::exists(FileFor(from), ec)) {
		std::filesystem::remove(FileFor(to), ec);
		std::filesystem::rename(FileFor(from), FileFor(to), ec);
	}
	if(edit_name == from)
		edit_name = to;
}

void DeckTags::DeleteDeck(const std::wstring& name) {
	std::error_code ec;
	std::filesystem::remove(FileFor(name), ec);
	if(edit_name == name) {
		edit_data = ParseTags("");
		++generation;
	}
}

void DeckTags::WriteText(const std::wstring& deck_name, const std::string& text) {
	if(deck_name.empty() || !SafeTagText(text))
		return;
	std::error_code ec;
	std::filesystem::create_directories("deck", ec);
	WriteAtomic(FileFor(deck_name), text);
	if(edit_loaded && deck_name == edit_name) {
		edit_data = ParseTags(text);
		++generation;
	}
}

uint32_t DeckTags::Generation() {
	return generation;
}

}
