#include "mod_updater.h"
#include "deck_tags.h"
#include "rarity_rules.h"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <thread>
#include <vector>
#include "curl.h"
#include "config.h"
#include "game_config.h"
#include "utils.h"
#include <nlohmann/json.hpp>
#ifdef _WIN32
#include <Windows.h>
#endif

namespace ygo {

namespace {
constexpr const char* RELEASE_URL = "https://api.github.com/repos/sushifishblock/EDOproextend/releases/latest";
constexpr const char* ASSET_NAME = "EDOPro-x64.exe";

size_t WriteBody(char* data, size_t size, size_t count, void* user) {
	static_cast<std::string*>(user)->append(data, size * count);
	return size * count;
}
size_t WriteFile(char* data, size_t size, size_t count, void* user) {
	return fwrite(data, size, count, static_cast<FILE*>(user)) ;
}
int Progress(void* user, curl_off_t total, curl_off_t now, curl_off_t, curl_off_t) {
	if(total > 0)
		static_cast<std::atomic<int>*>(user)->store(static_cast<int>(now * 100 / total));
	return 0;
}

void SetCommon(CURL* curl, const std::string& url) {
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "edopro-mod-updater");
	curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
	curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
	curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	if(ygo::gGameConfig->ssl_certificate_path.size() && Utils::FileExists(Utils::ToPathString(ygo::gGameConfig->ssl_certificate_path)))
		curl_easy_setopt(curl, CURLOPT_CAINFO, ygo::gGameConfig->ssl_certificate_path.data());
}

//"v1.2" -> {1, 2}
std::vector<int> ParseVersion(const std::string& text) {
	std::vector<int> parts;
	int value = 0;
	bool have = false;
	for(const char c : text) {
		if(c == '-' || c == '+')
			break; //"1.4.0-beta" is not newer than "1.4.0"
		if(c >= '0' && c <= '9') {
			if(value < 100000)
				value = value * 10 + (c - '0');
			have = true;
		} else if(c == '.') {
			parts.push_back(value);
			value = 0;
			have = false;
		}
	}
	if(have)
		parts.push_back(value);
	return parts;
}
bool IsNewer(const std::string& tag) {
	auto a = ParseVersion(tag), b = ParseVersion(ModUpdater::VERSION);
	if(a.empty())
		return false;
	a.resize(std::max(a.size(), b.size()));
	b.resize(a.size());
	return a > b;
}

std::filesystem::path ExePath() {
#ifdef _WIN32
	wchar_t buffer[MAX_PATH * 2];
	const auto length = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
	if(length == 0 || length >= std::size(buffer))
		return {};
	return std::filesystem::path(std::wstring(buffer, length));
#else
	return {};
#endif
}
}

extern "C" const char kModVersionMarker[] = "EDOPROX_MODVERSION=" MOD_VERSION_STRING;

void ModUpdater::CleanupOld() {
	static const char* volatile keep_marker = kModVersionMarker; //keeps the marker in the exe
	(void)keep_marker;
	std::error_code ec;
	auto exe = ExePath();
	if(exe.empty())
		return;
	std::filesystem::remove(exe.wstring() + L".old", ec);
	std::filesystem::remove(exe.wstring() + L".new", ec);
}

void ModUpdater::StartCheck() {
	if(check_started.exchange(true))
		return;
	std::thread(&ModUpdater::CheckThread).detach();
}

namespace {
bool FetchText(const std::string& url, std::string& body, long max_size = 2 * 1024 * 1024) {
	auto* curl = curl_easy_init();
	if(!curl)
		return false;
	body.clear();
	SetCommon(curl, url);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteBody);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
	curl_easy_setopt(curl, CURLOPT_MAXFILESIZE, max_size);
	const auto result = curl_easy_perform(curl);
	long status = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
	curl_easy_cleanup(curl);
	return result == CURLE_OK && status == 200;
}

struct Candidate {
	std::string tag, url;
	long long size = 0;
};

//the exe of a release, if it is a plausible file of this project
bool ExeAsset(const nlohmann::json& release, Candidate& out) {
	for(const auto& asset : release.at("assets")) {
		if(asset.at("name").get<std::string>() != "EDOPro-x64.exe")
			continue;
		out.url = asset.at("browser_download_url").get<std::string>();
		out.size = asset.at("size").get<long long>();
		return out.url.rfind("https://github.com/sushifishblock/EDOproextend/releases/", 0) == 0 && out.size >= 5ll * 1024 * 1024 && out.size <= 200ll * 1024 * 1024;
	}
	return false;
}
}

void ModUpdater::CheckThread() {
	try {
		state = CHECKING;
		std::vector<Candidate> found;
		std::string body;
		//the newest release
		if(FetchText(RELEASE_URL, body)) {
			const auto root = nlohmann::json::parse(body);
			Candidate c;
			c.tag = root.at("tag_name").get<std::string>();
			if(ExeAsset(root, c))
				found.push_back(c);
		}
		//a build the author published from the Rarity Studio (release "rarity-pack"): the file is named EDOPro-x64-<version>.exe
		//(a new address for every version, so an old copy that is cached somewhere is never served for a new version)
		try {
			if(FetchText("https://api.github.com/repos/sushifishblock/EDOproextend/releases/tags/rarity-pack", body)) {
				const auto root = nlohmann::json::parse(body);
				for(const auto& asset : root.at("assets")) {
					const std::string name = asset.at("name").get<std::string>();
					if(name.rfind("EDOPro-x64-", 0) != 0 || name.size() < 16 || name.compare(name.size() - 4, 4, ".exe") != 0)
						continue;
					Candidate c;
					c.tag = name.substr(11, name.size() - 15);
					c.url = asset.at("browser_download_url").get<std::string>();
					c.size = asset.at("size").get<long long>();
					if(c.url.rfind("https://github.com/sushifishblock/EDOproextend/releases/", 0) == 0 && c.size >= 5ll * 1024 * 1024 && c.size <= 200ll * 1024 * 1024)
						found.push_back(c);
				}
			}
		} catch(...) {}
		const Candidate* best = nullptr;
		for(const auto& c : found) {
			if(!IsNewer(c.tag))
				continue;
			if(!best || ParseVersion(c.tag) > ParseVersion(best->tag))
				best = &c;
		}
		if(best) {
			std::lock_guard<std::mutex> lock(info_mutex);
			latest_tag = best->tag;
			asset_url = best->url;
			asset_size = best->size;
			state = AVAILABLE;
			return;
		}
	} catch(...) {}
	if(state == CHECKING)
		state = IDLE;
}

void ModUpdater::BeginInstall() {
	State expected = AVAILABLE;
	if(!state.compare_exchange_strong(expected, DOWNLOADING)) {
		expected = FAILED;
		if(!state.compare_exchange_strong(expected, DOWNLOADING))
			return;
	}
	percent = 0;
	std::thread(&ModUpdater::InstallThread).detach();
}

void ModUpdater::InstallThread() {
	std::string url;
	long long expected_size = 0;
	{
		std::lock_guard<std::mutex> lock(info_mutex);
		url = asset_url;
		expected_size = asset_size;
	}
	try {
		const auto exe = ExePath();
		if(exe.empty() || url.empty())
			throw std::runtime_error("no exe");
		const std::filesystem::path part = exe.wstring() + L".new";
		const std::filesystem::path old = exe.wstring() + L".old";
		std::error_code ec;
		std::filesystem::remove(part, ec);
		{
			FILE* file = nullptr;
#ifdef _WIN32
			file = _wfopen(part.c_str(), L"wb");
#endif
			if(!file)
				throw std::runtime_error("open");
			auto* curl = curl_easy_init();
			if(!curl) {
				fclose(file);
				throw std::runtime_error("curl");
			}
			SetCommon(curl, url);
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteFile);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);
			curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
			curl_easy_setopt(curl, CURLOPT_TIMEOUT, 900L);
			curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
			curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, Progress);
			curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &percent);
			const auto result = curl_easy_perform(curl);
			curl_easy_cleanup(curl);
			const bool closed = fclose(file) == 0;
			if(result != CURLE_OK || !closed)
				throw std::runtime_error("download");
		}
		//sanity: size, and it must be a Windows executable
		const auto size = std::filesystem::file_size(part, ec);
		if(ec || size < 5u * 1024 * 1024 || (expected_size > 0 && static_cast<long long>(size) != expected_size))
			throw std::runtime_error("size");
		{
			FILE* file = nullptr;
#ifdef _WIN32
			file = _wfopen(part.c_str(), L"rb");
#endif
			char magic[2] = {};
			if(!file || fread(magic, 1, 2, file) != 2 || magic[0] != 'M' || magic[1] != 'Z') {
				if(file)
					fclose(file);
				throw std::runtime_error("magic");
			}
			fclose(file);
		}
		//swap: the running exe can be renamed
		std::filesystem::remove(old, ec);
		std::filesystem::rename(exe, old, ec);
		if(ec)
			throw std::runtime_error("rename");
		std::filesystem::rename(part, exe, ec);
		if(ec) {
			std::error_code ignore;
			std::filesystem::rename(old, exe, ignore);
			throw std::runtime_error("replace");
		}
		percent = 100;
		state = RESTART;
	} catch(...) {
		std::error_code ignore;
		std::filesystem::remove(std::filesystem::path(ExePath().wstring() + L".new"), ignore);
		state = FAILED;
	}
}

void ModUpdater::LaunchNew() {
#ifdef _WIN32
	const auto exe = ExePath();
	wchar_t cwd[MAX_PATH * 2];
	const auto cwd_length = GetCurrentDirectoryW(static_cast<DWORD>(std::size(cwd)), cwd);
	//wait a moment so this process has closed (and saved its settings), then start the new exe in the same folder
	std::wstring command = L"cmd.exe /c ping -n 3 127.0.0.1 >nul & start \"\" \"" + exe.wstring() + L"\"";
	STARTUPINFOW startup = {};
	startup.cb = sizeof(startup);
	PROCESS_INFORMATION info = {};
	if(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, cwd_length ? cwd : nullptr, &startup, &info)) {
		CloseHandle(info.hProcess);
		CloseHandle(info.hThread);
	}
#endif
}

std::wstring ModUpdater::Label() {
	switch(state.load()) {
	case AVAILABLE: {
		std::lock_guard<std::mutex> lock(info_mutex);
		return L"Update available: " + std::wstring(latest_tag.begin(), latest_tag.end()) + L" - click to install";
	}
	case DOWNLOADING:
		return L"Downloading update... " + std::to_wstring(percent.load()) + L"%";
	case FAILED:
		return L"Update failed - click to retry";
	case RESTART:
		return L"Restarting...";
	default:
		return L"";
	}
}

// ---------------------------------------------------------------- the rarity pack
namespace {
constexpr const char* PACK_RELEASE_URL = "https://api.github.com/repos/sushifishblock/EDOproextend/releases/tags/rarity-pack";
constexpr const char* PACK_ASSET = "rarity_pack.json";
constexpr size_t PACK_MAX_SIZE = 12 * 1024 * 1024;
constexpr size_t ART_MAX_SIZE = 4 * 1024 * 1024; //one picture of the pack (after decoding)

//the pictures of the pack: only these names, written to custom_art/
bool SafeArtName(const std::string& name) {
	return name == "sleeve.png" || name == "sleeve_opponent.png" || name == "playmat.png";
}

bool DecodeBase64(const std::string& text, std::string& out) {
	out.clear();
	uint32_t bits = 0;
	int count = 0;
	for(const unsigned char c : text) {
		int value;
		if(c >= 'A' && c <= 'Z') value = c - 'A';
		else if(c >= 'a' && c <= 'z') value = c - 'a' + 26;
		else if(c >= '0' && c <= '9') value = c - '0' + 52;
		else if(c == '+') value = 62;
		else if(c == '/') value = 63;
		else if(c == '=' || c == '\n' || c == '\r') continue;
		else return false;
		bits = (bits << 6) | value;
		count += 6;
		if(count >= 8) {
			count -= 8;
			out.push_back(static_cast<char>((bits >> count) & 0xff));
			if(out.size() > ART_MAX_SIZE)
				return false;
		}
	}
	return true;
}

bool LooksLikePng(const std::string& data) {
	return data.size() > 64 && data.compare(0, 8, "\x89PNG\r\n\x1a\n", 8) == 0;
}

bool HttpGet(const std::string& url, std::string& body) {
	auto* curl = curl_easy_init();
	if(!curl)
		return false;
	body.clear();
	SetCommon(curl, url);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteBody);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
	curl_easy_setopt(curl, CURLOPT_MAXFILESIZE, static_cast<long>(PACK_MAX_SIZE));
	const auto result = curl_easy_perform(curl);
	long status = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
	curl_easy_cleanup(curl);
	return result == CURLE_OK && status == 200 && body.size() <= PACK_MAX_SIZE;
}

std::string ReadMarker() {
	std::ifstream file("rarity_pack.id");
	std::string id;
	file >> id;
	return id;
}

//a rarity file only holds words, numbers and spaces
bool SafeText(const std::string& text, size_t max_size) {
	if(text.size() > max_size)
		return false;
	for(const char c : text) {
		if(!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || c == ' ' || c == '\n' || c == '\r'))
			return false;
	}
	return true;
}

//a deck name that cannot point anywhere else than the deck folder
bool SafeDeckName(const std::string& name) {
	if(name.empty() || name.size() > 120 || name == "." || name == "..")
		return false;
	for(const unsigned char c : name) {
		if(c < 32 || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
			return false;
	}
	return name.find("..") == std::string::npos;
}

void WriteWithBackup(const std::filesystem::path& path, const std::string& content) {
	std::error_code ec;
	if(std::filesystem::exists(path, ec)) {
		std::filesystem::path backup = path;
		backup += ".backup";
		std::filesystem::copy_file(path, backup, std::filesystem::copy_options::overwrite_existing, ec);
	}
	std::filesystem::path temp = path;
	temp += ".tmp";
	{
		std::ofstream file(temp, std::ios::binary | std::ios::trunc);
		if(!file.is_open())
			throw std::runtime_error("write");
		file << content;
		if(!file.good())
			throw std::runtime_error("write");
	}
	std::filesystem::rename(temp, path, ec);
	if(ec)
		throw std::runtime_error("rename");
}

long long NowSeconds() {
	return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
}

void RarityPack::StartCheck() {
	if(check_started.exchange(true))
		return;
	std::thread(&RarityPack::CheckThread).detach();
}

void RarityPack::CheckThread() {
	try {
		state = CHECKING;
		std::string body;
		if(!HttpGet(PACK_RELEASE_URL, body)) {
			state = IDLE;
			return;
		}
		const auto root = nlohmann::json::parse(body);
		for(const auto& asset : root.at("assets")) {
			if(asset.at("name").get<std::string>() != PACK_ASSET)
				continue;
			const std::string url = asset.at("browser_download_url").get<std::string>();
			const std::string id = asset.at("id").dump();
			if(url.rfind("https://github.com/sushifishblock/EDOproextend/releases/", 0) != 0 || asset.at("size").get<long long>() > static_cast<long long>(PACK_MAX_SIZE))
				break;
			if(id == ReadMarker()) {
				state = IDLE; //already installed (or published from this very game folder)
				return;
			}
			std::lock_guard<std::mutex> lock(info_mutex);
			asset_url = url;
			asset_id = id;
			state = AVAILABLE;
			return;
		}
	} catch(...) {}
	state = IDLE;
}

void RarityPack::BeginInstall() {
	State expected = AVAILABLE;
	if(!state.compare_exchange_strong(expected, DOWNLOADING)) {
		expected = FAILED;
		if(!state.compare_exchange_strong(expected, DOWNLOADING))
			return;
	}
	std::thread(&RarityPack::InstallThread).detach();
}

void RarityPack::InstallThread() {
	try {
		std::string url, id;
		{
			std::lock_guard<std::mutex> lock(info_mutex);
			url = asset_url;
			id = asset_id;
		}
		std::string body;
		if(url.empty() || !HttpGet(url, body))
			throw std::runtime_error("download");
		const auto pack = nlohmann::json::parse(body);
		//check everything first, then write
		std::string designs, assignments;
		std::vector<std::pair<std::string, std::string>> decks;
		if(pack.contains("custom_rarities") && pack["custom_rarities"].is_object()) {
			const auto& custom = pack["custom_rarities"];
			if(!custom.contains("rarities") || !custom["rarities"].is_array() || custom["rarities"].size() > 40)
				throw std::runtime_error("designs");
			designs = custom.dump(1);
		}
		if(pack.contains("rarities_txt") && pack["rarities_txt"].is_string()) {
			assignments = pack["rarities_txt"].get<std::string>();
			if(!SafeText(assignments, 3 * 1024 * 1024))
				throw std::runtime_error("assignments");
		}
		if(pack.contains("decks") && pack["decks"].is_object()) {
			for(const auto& item : pack["decks"].items()) {
				if(!item.value().is_string() || !SafeDeckName(item.key()))
					continue;
				const std::string text = item.value().get<std::string>();
				if(!SafeText(text, 200 * 1024))
					continue;
				decks.emplace_back(item.key(), text);
			}
		}
		std::vector<std::pair<std::wstring, std::string>> tag_files;
		if(pack.contains("tags") && pack["tags"].is_object()) {
			for(const auto& item : pack["tags"].items()) {
				if(!item.value().is_string() || !SafeDeckName(item.key()))
					continue;
				const std::string text = item.value().get<std::string>();
				if(!SafeTagText(text))
					continue;
				tag_files.emplace_back(std::filesystem::u8path(item.key()).wstring(), text);
			}
		}
		std::string rules_text;
		if(pack.contains("rarity_rules") && pack["rarity_rules"].is_object()) {
			std::vector<RarityRule> check;
			const std::string text = pack["rarity_rules"].dump(1);
			if(RarityRules::Parse(text, check))
				rules_text = text;
		}
		std::vector<std::pair<std::string, std::string>> art_files;
		const bool has_art_key = pack.contains("art") && pack["art"].is_object();
		if(has_art_key) {
			for(const auto& item : pack["art"].items()) {
				std::string data;
				if(!item.value().is_string() || !SafeArtName(item.key()) || !DecodeBase64(item.value().get<std::string>(), data) || !LooksLikePng(data))
					continue;
				art_files.emplace_back(item.key(), std::move(data));
			}
		}
		if(designs.empty() && assignments.empty() && decks.empty() && tag_files.empty() && rules_text.empty() && !has_art_key)
			throw std::runtime_error("empty");
		std::error_code ec;
		if(!rules_text.empty())
			WriteWithBackup("rarity_rules.json", rules_text);
		if(has_art_key) {
			//the pack decides the whole look: a picture it does not have is removed (the default one is used again)
			std::filesystem::create_directories("custom_art", ec);
			for(const char* name : { "sleeve.png", "sleeve_opponent.png", "playmat.png" }) {
				bool in_pack = false;
				for(const auto& file : art_files)
					in_pack = in_pack || file.first == name;
				if(!in_pack)
					std::filesystem::remove(std::filesystem::path("custom_art") / name, ec);
			}
			for(const auto& file : art_files)
				WriteWithBackup(std::filesystem::path("custom_art") / file.first, file.second);
		}
		if(!designs.empty())
			WriteWithBackup("custom_rarities.json", designs);
		if(!assignments.empty())
			WriteWithBackup("rarities.txt", assignments);
		if(!decks.empty()) {
			std::filesystem::create_directories("deck", ec);
			for(const auto& deck : decks)
				WriteWithBackup(std::filesystem::path("deck") / std::filesystem::u8path(deck.first + ".rarity"), deck.second);
		}
		for(const auto& file : tag_files)
			DeckTags::WriteText(file.first, file.second);
		std::ofstream("rarity_pack.id", std::ios::trunc) << id;
		done_at = NowSeconds();
		installed = true;
		state = DONE;
	} catch(...) {
		state = FAILED;
	}
}

bool RarityPack::RecentlyDone() {
	return state.load() == DONE && NowSeconds() - done_at.load() < 8;
}

std::wstring RarityPack::Label() {
	switch(state.load()) {
	case AVAILABLE:
		return L"New rarities from the author - click to install";
	case DOWNLOADING:
		return L"Installing rarities...";
	case DONE:
		return L"Rarities installed";
	case FAILED:
		return L"Rarity install failed - click to retry";
	default:
		return L"";
	}
}

}
