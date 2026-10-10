#include "ai_assistant.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>
#include "curl.h"
#include "bufferio.h"
#include "data_manager.h"
#include "ocgapi_constants.h"
#include "logging.h"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace ygo {

namespace {

constexpr int CHAT_PORT = 8081;
constexpr int EMBED_PORT = 8082;
constexpr size_t EMBED_BATCH = 8;
constexpr size_t MAX_RESULTS = 400;
constexpr float KEYWORD_BONUS = 0.05f;
constexpr uint32_t INDEX_VERSION = 1;

std::string Lower(std::string text) {
	for(auto& c : text) {
		if(c >= 'A' && c <= 'Z')
			c = static_cast<char>(c + ('a' - 'A'));
	}
	return text;
}

uint64_t Fnv1a(const std::string& text) {
	uint64_t hash = 1469598103934665603ull;
	for(const unsigned char c : text) {
		hash ^= c;
		hash *= 1099511628211ull;
	}
	return hash;
}

bool ReadFile(const std::filesystem::path& path, std::string& out) {
	std::ifstream in(path, std::ios::binary);
	if(!in)
		return false;
	std::ostringstream buffer;
	buffer << in.rdbuf();
	out = buffer.str();
	return true;
}

std::string RaceName(uint64_t race) {
	static const char* names[] = { "Warrior", "Spellcaster", "Fairy", "Fiend", "Zombie", "Machine", "Aqua", "Pyro", "Rock", "Winged Beast",
		"Plant", "Insect", "Thunder", "Dragon", "Beast", "Beast-Warrior", "Dinosaur", "Fish", "Sea Serpent", "Reptile", "Psychic",
		"Divine-Beast", "Creator God", "Wyrm", "Cyberse", "Illusion" };
	for(size_t i = 0; i < std::size(names); ++i) {
		if(race & (1ull << i))
			return names[i];
	}
	return "";
}

std::string AttributeName(uint32_t attribute) {
	static const char* names[] = { "EARTH", "WATER", "FIRE", "WIND", "LIGHT", "DARK", "DIVINE" };
	for(size_t i = 0; i < std::size(names); ++i) {
		if(attribute & (1u << i))
			return names[i];
	}
	return "";
}

std::string TypeLine(const AiCard& card) {
	std::string line;
	const auto add = [&](const char* word) {
		if(!line.empty())
			line += " / ";
		line += word;
	};
	if(card.type & TYPE_MONSTER) {
		if(const auto race = RaceName(card.race); !race.empty())
			add(race.c_str());
		if(card.type & TYPE_FUSION) add("Fusion");
		if(card.type & TYPE_SYNCHRO) add("Synchro");
		if(card.type & TYPE_XYZ) add("Xyz");
		if(card.type & TYPE_LINK) add("Link");
		if(card.type & TYPE_RITUAL) add("Ritual");
		if(card.type & TYPE_PENDULUM) add("Pendulum");
		if(card.type & TYPE_SPIRIT) add("Spirit");
		if(card.type & TYPE_UNION) add("Union");
		if(card.type & TYPE_GEMINI) add("Gemini");
		if(card.type & TYPE_TUNER) add("Tuner");
		if(card.type & TYPE_FLIP) add("Flip");
		if(card.type & TYPE_TOON) add("Toon");
		if(card.type & TYPE_SPSUMMON) add("Special Summon");
		if(card.type & TYPE_NORMAL) add("Normal");
		if(card.type & TYPE_EFFECT) add("Effect");
		add("Monster");
	} else if(card.type & TYPE_SPELL) {
		if(card.type & TYPE_QUICKPLAY) add("Quick-Play");
		else if(card.type & TYPE_CONTINUOUS) add("Continuous");
		else if(card.type & TYPE_EQUIP) add("Equip");
		else if(card.type & TYPE_FIELD) add("Field");
		else if(card.type & TYPE_RITUAL) add("Ritual");
		else add("Normal");
		add("Spell Card");
	} else if(card.type & TYPE_TRAP) {
		if(card.type & TYPE_CONTINUOUS) add("Continuous");
		else if(card.type & TYPE_COUNTER) add("Counter");
		else add("Normal");
		add("Trap Card");
	}
	return line;
}

std::string BuildDocument(const AiCard& card) {
	std::string doc = "search_document: " + card.name + ". " + TypeLine(card) + ".";
	if(card.type & TYPE_MONSTER) {
		if(const auto attribute = AttributeName(card.attribute); !attribute.empty())
			doc += " " + attribute + ".";
		if(card.type & TYPE_LINK)
			doc += " Link-" + std::to_string(card.level) + ".";
		else if(card.type & TYPE_XYZ)
			doc += " Rank " + std::to_string(card.level) + ".";
		else
			doc += " Level " + std::to_string(card.level) + ".";
		const auto stat = [](int32_t value) { return value < 0 ? std::string("?") : std::to_string(value); };
		if(card.type & TYPE_LINK)
			doc += " ATK " + stat(card.attack) + ".";
		else
			doc += " ATK " + stat(card.attack) + " / DEF " + stat(card.defense) + ".";
	}
	{
		//cut the text at a character boundary (a cut in the middle of a UTF-8 character makes the JSON invalid)
		size_t n = std::min<size_t>(card.text.size(), 1500);
		while(n > 0 && n < card.text.size() && (static_cast<uint8_t>(card.text[n]) & 0xC0) == 0x80)
			--n;
		doc += " " + card.text.substr(0, n);
	}
	return doc;
}

size_t WriteCallback(char* data, size_t size, size_t count, void* user) {
	static_cast<std::string*>(user)->append(data, size * count);
	return size * count;
}

//a request to one of the local model servers, false if it could not be completed
bool HttpRequest(int port, const std::string& path, const std::string* body, long timeout_seconds, std::string& response, long& status) {
	response.clear();
	status = 0;
	auto* curl = curl_easy_init();
	if(!curl)
		return false;
	curl_slist* headers = nullptr;
	char error_buffer[CURL_ERROR_SIZE] = {};
	const std::string url = "http://127.0.0.1:" + std::to_string(port) + path;
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buffer);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
	curl_easy_setopt(curl, CURLOPT_NOPROXY, "*");
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_seconds);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	if(body) {
		headers = curl_slist_append(headers, "Content-Type: application/json");
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
		curl_easy_setopt(curl, CURLOPT_POST, 1L);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body->c_str());
		curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(body->size()));
	}
	const auto result = curl_easy_perform(curl);
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	return result == CURLE_OK;
}

//what the language model found in the question
struct Range {
	bool has_min = false;
	bool has_max = false;
	int64_t min = 0;
	int64_t max = 0;
	bool Check(int64_t value) const {
		return (!has_min || value >= min) && (!has_max || value <= max);
	}
	bool Any() const { return has_min || has_max; }
};

struct Criteria {
	std::vector<std::string> card_types;
	std::vector<std::string> monster_types;
	std::vector<std::string> spell_types;
	std::vector<std::string> trap_types;
	std::vector<std::string> races;
	std::vector<std::string> attributes;
	Range level, rank, link_rating, atk, def, scale;
	std::string name_contains;
	std::string effect_description;
	std::vector<std::string> keywords;
	std::string explanation;
	std::string sort_by;
	std::vector<int> exact_scales;
	int limit = 0;
};

std::vector<std::string> StringList(const nlohmann::json& j, const char* key) {
	std::vector<std::string> out;
	if(j.contains(key) && j[key].is_array()) {
		for(const auto& item : j[key]) {
			if(item.is_string())
				out.push_back(Lower(item.get<std::string>()));
		}
	}
	return out;
}

Range ParseRange(const nlohmann::json& j, const char* key) {
	Range range;
	if(!j.contains(key) || !j[key].is_object())
		return range;
	const auto& r = j[key];
	if(r.contains("min") && r["min"].is_number()) {
		range.has_min = true;
		range.min = static_cast<int64_t>(r["min"].get<double>());
	}
	if(r.contains("max") && r["max"].is_number()) {
		range.has_max = true;
		range.max = static_cast<int64_t>(r["max"].get<double>());
	}
	return range;
}

std::string StringValue(const nlohmann::json& j, const char* key) {
	if(j.contains(key) && j[key].is_string())
		return j[key].get<std::string>();
	return {};
}

Criteria ParseCriteria(const nlohmann::json& root) {
	Criteria c;
	static const nlohmann::json empty = nlohmann::json::object();
	const nlohmann::json& j = (root.contains("filters") && root["filters"].is_object()) ? root["filters"] : empty;
	if(const auto type = Lower(StringValue(j, "card_type")); !type.empty())
		c.card_types.push_back(type);
	c.monster_types = StringList(j, "monster_types");
	c.spell_types = StringList(j, "spell_trap_kinds");
	c.trap_types = c.spell_types;
	c.races = StringList(j, "races");
	c.attributes = StringList(j, "attributes");
	c.level = ParseRange(j, "level");
	c.rank = ParseRange(j, "rank");
	c.link_rating = ParseRange(j, "link_rating");
	c.atk = ParseRange(j, "atk");
	c.def = ParseRange(j, "def");
	c.scale = ParseRange(j, "pendulum_scale");
	if(j.contains("pendulum_scales") && j["pendulum_scales"].is_array()) {
		for(const auto& value : j["pendulum_scales"]) {
			if(value.is_number())
				c.exact_scales.push_back(static_cast<int>(value.get<double>()));
		}
	}
	c.name_contains = Lower(StringValue(j, "name_or_archetype"));
	c.effect_description = StringValue(root, "effect_description");
	for(auto& keyword : StringList(root, "text_keywords"))
		if(!keyword.empty())
			c.keywords.push_back(std::move(keyword));
	c.explanation = StringValue(root, "explanation");
	c.sort_by = StringValue(root, "sort_by");
	if(root.contains("result_limit") && root["result_limit"].is_number())
		c.limit = std::max(0, static_cast<int>(root["result_limit"].get<double>()));
	return c;
}

bool Contains(const std::vector<std::string>& list, const char* value) {
	return std::find(list.begin(), list.end(), value) != list.end();
}

bool Matches(const AiCard& card, const Criteria& c) {
	const bool monster = card.type & TYPE_MONSTER;
	const bool spell = card.type & TYPE_SPELL;
	const bool trap = card.type & TYPE_TRAP;
	if(!c.card_types.empty()) {
		bool ok = false;
		for(const auto& type : c.card_types) {
			if((type == "monster" && monster) || (type == "spell" && spell) || (type == "trap" && trap))
				ok = true;
		}
		if(!ok)
			return false;
	}
	if(!c.monster_types.empty()) {
		if(!monster)
			return false;
		static const struct { const char* name; uint32_t flag; } flags[] = { { "normal", TYPE_NORMAL }, { "effect", TYPE_EFFECT }, { "fusion", TYPE_FUSION },
			{ "ritual", TYPE_RITUAL }, { "spirit", TYPE_SPIRIT }, { "union", TYPE_UNION }, { "gemini", TYPE_GEMINI }, { "tuner", TYPE_TUNER },
			{ "synchro", TYPE_SYNCHRO }, { "flip", TYPE_FLIP }, { "toon", TYPE_TOON }, { "xyz", TYPE_XYZ }, { "pendulum", TYPE_PENDULUM },
			{ "link", TYPE_LINK }, { "special_summon", TYPE_SPSUMMON } };
		for(const auto& wanted : c.monster_types) {
			for(const auto& flag : flags) {
				if(wanted == flag.name && !(card.type & flag.flag))
					return false;
			}
		}
	}
	if(!c.spell_types.empty() && spell) {
		static const struct { const char* name; uint32_t flag; } flags[] = { { "quick-play", TYPE_QUICKPLAY }, { "continuous", TYPE_CONTINUOUS },
			{ "equip", TYPE_EQUIP }, { "field", TYPE_FIELD }, { "ritual", TYPE_RITUAL } };
		bool ok = false;
		for(const auto& wanted : c.spell_types) {
			if(wanted == "normal" && !(card.type & (TYPE_QUICKPLAY | TYPE_CONTINUOUS | TYPE_EQUIP | TYPE_FIELD | TYPE_RITUAL)))
				ok = true;
			for(const auto& flag : flags) {
				if(wanted == flag.name && (card.type & flag.flag))
					ok = true;
			}
		}
		if(!ok)
			return false;
	}
	if(!c.trap_types.empty() && trap) {
		bool ok = false;
		for(const auto& wanted : c.trap_types) {
			if(wanted == "normal" && !(card.type & (TYPE_CONTINUOUS | TYPE_COUNTER)))
				ok = true;
			if(wanted == "continuous" && (card.type & TYPE_CONTINUOUS))
				ok = true;
			if(wanted == "counter" && (card.type & TYPE_COUNTER))
				ok = true;
		}
		if(!ok)
			return false;
	}
	if(!c.races.empty()) {
		if(!monster)
			return false;
		const auto race = Lower(RaceName(card.race));
		bool ok = false;
		for(const auto& wanted : c.races) {
			if(wanted == race)
				ok = true;
		}
		if(!ok)
			return false;
	}
	if(!c.attributes.empty()) {
		if(!monster)
			return false;
		const auto attribute = Lower(AttributeName(card.attribute));
		bool ok = false;
		for(const auto& wanted : c.attributes) {
			if(wanted == attribute)
				ok = true;
		}
		if(!ok)
			return false;
	}
	if(c.level.Any() || c.rank.Any() || c.link_rating.Any() || c.atk.Any() || c.def.Any() || c.scale.Any()) {
		if(!monster)
			return false;
		if(c.level.Any() && ((card.type & (TYPE_XYZ | TYPE_LINK)) || !c.level.Check(card.level)))
			return false;
		if(c.rank.Any() && (!(card.type & TYPE_XYZ) || !c.rank.Check(card.level)))
			return false;
		if(c.link_rating.Any() && (!(card.type & TYPE_LINK) || !c.link_rating.Check(card.level)))
			return false;
		if(c.atk.Any() && (card.attack < 0 || !c.atk.Check(card.attack)))
			return false;
		if(c.def.Any() && ((card.type & TYPE_LINK) || card.defense < 0 || !c.def.Check(card.defense)))
			return false;
		if(c.scale.Any() && (!(card.type & TYPE_PENDULUM) || !c.scale.Check(card.lscale)))
			return false;
	}
	if(!c.exact_scales.empty()) {
		if(!(card.type & TYPE_PENDULUM))
			return false;
		for(const int wanted : c.exact_scales) {
			if(wanted != static_cast<int>(card.lscale) && wanted != static_cast<int>(card.rscale))
				return false;
		}
	}
	if(!c.name_contains.empty() && card.name_lower.find(c.name_contains) == std::string::npos)
		return false;
	return true;
}

}

//a server process started by the game. It is put in a job object so that it always ends together with the game.
struct AiAssistant::Process {
#ifdef _WIN32
	HANDLE process = nullptr;
	HANDLE job = nullptr;
	HANDLE log = nullptr;
#endif
	bool Launch(const std::wstring& exe, const std::wstring& arguments, const std::wstring& log_path) {
#ifdef _WIN32
		Kill();
		job = CreateJobObjectW(nullptr, nullptr);
		if(job) {
			JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
			info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
			SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info, sizeof(info));
		}
		SECURITY_ATTRIBUTES security{ sizeof(security), nullptr, TRUE };
		std::filesystem::create_directories(std::filesystem::path(log_path).parent_path());
		log = CreateFileW(log_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &security, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		STARTUPINFOW startup{};
		startup.cb = sizeof(startup);
		startup.dwFlags = STARTF_USESTDHANDLES;
		startup.hStdOutput = log;
		startup.hStdError = log;
		startup.hStdInput = nullptr;
		PROCESS_INFORMATION info{};
		std::wstring command = L"\"" + exe + L"\" " + arguments;
		const BOOL created = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &startup, &info);
		if(!created) {
			Kill();
			return false;
		}
		if(job)
			AssignProcessToJobObject(job, info.hProcess);
		ResumeThread(info.hThread);
		CloseHandle(info.hThread);
		process = info.hProcess;
		return true;
#else
		return false;
#endif
	}
	bool IsRunning() {
#ifdef _WIN32
		return process && WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
#else
		return false;
#endif
	}
	void Kill() {
#ifdef _WIN32
		if(process) {
			TerminateProcess(process, 0);
			WaitForSingleObject(process, 3000);
			CloseHandle(process);
			process = nullptr;
		}
		if(job) {
			CloseHandle(job);
			job = nullptr;
		}
		if(log) {
			CloseHandle(log);
			log = nullptr;
		}
#endif
	}
	~Process() { Kill(); }
};

AiAssistant::AiAssistant() {
	std::error_code ec;
	installed = std::filesystem::exists("ai/llama/llama-server.exe", ec) && std::filesystem::exists("ai/models", ec);
	if(installed)
		worker = std::thread(&AiAssistant::WorkerLoop, this);
}

AiAssistant::~AiAssistant() {
	{
		std::lock_guard<std::mutex> lock(mutex);
		quit = true;
	}
	cv.notify_all();
	if(worker.joinable())
		worker.join();
	StopServers();
}

std::vector<AiCard> AiAssistant::BuildSnapshot() {
	std::vector<AiCard> out;
	out.reserve(gDataManager->cards.size());
	for(const auto& entry : gDataManager->cards) {
		const auto& data = entry.second._data;
		if(data.type & TYPE_TOKEN || data.ot & SCOPE_HIDDEN || data.IsInArtworkOffsetRange() || data.code == 0)
			continue;
		const auto& strings = entry.second.GetStrings();
		if(strings.text.empty() || strings.name.empty())
			continue;
		AiCard card;
		card.code = data.code;
		card.type = data.type;
		card.race = data.race;
		card.attribute = data.attribute;
		card.level = data.level;
		card.lscale = data.lscale;
		card.rscale = data.rscale;
		card.attack = data.attack;
		card.defense = data.defense;
		card.link_marker = data.link_marker;
		card.ot = data.ot;
		card.name = BufferIO::EncodeUTF8(strings.name);
		card.text = BufferIO::EncodeUTF8(strings.text);
		card.name_lower = Lower(card.name);
		card.text_lower = Lower(card.text);
		card.document = BuildDocument(card);
		card.document_hash = Fnv1a(card.document);
		out.push_back(std::move(card));
	}
	std::sort(out.begin(), out.end(), [](const AiCard& a, const AiCard& b) { return a.code < b.code; });
	return out;
}

void AiAssistant::Start(std::vector<AiCard> snapshot) {
	if(!installed)
		return;
	{
		std::lock_guard<std::mutex> lock(mutex);
		pending_snapshot = std::move(snapshot);
		has_pending_snapshot = true;
		start_requested = true;
		stop_scheduled = false;
	}
	cv.notify_all();
}

void AiAssistant::ScheduleStop() {
	std::lock_guard<std::mutex> lock(mutex);
	stop_scheduled = true;
	stop_at = std::chrono::steady_clock::now() + std::chrono::minutes(3);
}

void AiAssistant::Ask(std::wstring question, bool include_unofficial) {
	if(!installed)
		return;
	{
		std::lock_guard<std::mutex> lock(mutex);
		asks.push_back(AskTask{ BufferIO::EncodeUTF8(question), include_unofficial });
		start_requested = true;
		stop_scheduled = false;
	}
	busy = true;
	cv.notify_all();
}

bool AiAssistant::PollResult(AiResult& out) {
	std::lock_guard<std::mutex> lock(mutex);
	if(!has_result)
		return false;
	out = std::move(result);
	has_result = false;
	return true;
}

std::wstring AiAssistant::GetStatus() {
	std::lock_guard<std::mutex> lock(mutex);
	return status;
}

void AiAssistant::SetStatus(std::wstring text) {
	std::lock_guard<std::mutex> lock(mutex);
	status = std::move(text);
}

bool AiAssistant::WaitForServer(int port, const std::chrono::seconds& timeout) {
	const auto deadline = std::chrono::steady_clock::now() + timeout;
	while(std::chrono::steady_clock::now() < deadline) {
		{
			std::lock_guard<std::mutex> lock(mutex);
			if(quit)
				return false;
		}
		std::string response;
		long http_status = 0;
		if(HttpRequest(port, "/health", nullptr, 3, response, http_status) && http_status == 200)
			return true;
		if((port == CHAT_PORT && chat_process && !chat_process->IsRunning()) || (port == EMBED_PORT && embed_process && !embed_process->IsRunning()))
			return false;
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
	}
	return false;
}

bool AiAssistant::EnsureServers() {
	const auto absolute = [](const char* relative) { return std::filesystem::absolute(relative).wstring(); };
	const std::wstring exe = absolute("ai/llama/llama-server.exe");
	std::string config_text;
	nlohmann::json config = nlohmann::json::object();
	if(ReadFile("ai/config.json", config_text)) {
		try {
			config = nlohmann::json::parse(config_text);
		} catch(...) {
		}
	}
	const auto text = [&](const char* key, const char* fallback) {
		return config.contains(key) && config[key].is_string() ? config[key].get<std::string>() : std::string(fallback);
	};
	const std::wstring chat_model = absolute(text("chat_model", "ai/models/qwen2.5-3b-instruct-q4_k_m.gguf").c_str());
	const std::wstring embed_model = absolute(text("embed_model", "ai/models/nomic-embed-text-v1.5.Q8_0.gguf").c_str());
	const std::wstring chat_args = BufferIO::DecodeUTF8(text("chat_args", "--host 127.0.0.1 --port 8081 -ngl 99 -c 4096 --parallel 1"));
	const std::wstring embed_args = BufferIO::DecodeUTF8(text("embed_args", "--host 127.0.0.1 --port 8082 --embedding -ngl 99 -c 2048 -ub 2048"));
	if(!chat_process)
		chat_process = std::make_unique<Process>();
	if(!embed_process)
		embed_process = std::make_unique<Process>();
	bool launched = false;
	if(!chat_process->IsRunning()) {
		SetStatus(L"Starting the AI helper...");
		if(!chat_process->Launch(exe, L"-m \"" + chat_model + L"\" " + chat_args, absolute("ai/logs") + L"\\chat.log"))
			return false;
		launched = true;
	}
	if(!embed_process->IsRunning()) {
		if(!embed_process->Launch(exe, L"-m \"" + embed_model + L"\" " + embed_args, absolute("ai/logs") + L"\\embed.log"))
			return false;
		launched = true;
	}
	if(launched)
		SetStatus(L"Loading the AI models...");
	if(!WaitForServer(EMBED_PORT, std::chrono::seconds(180)) || !WaitForServer(CHAT_PORT, std::chrono::seconds(180)))
		return false;
	return true;
}

void AiAssistant::StopServers() {
	chat_process.reset();
	embed_process.reset();
}

bool AiAssistant::EmbedTexts(const std::vector<std::string>& texts, std::vector<std::vector<float>>& out) {
	nlohmann::json body;
	body["input"] = texts;
	body["model"] = "embedding";
	const std::string request = body.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
	std::string response;
	long http_status = 0;
	if(!HttpRequest(EMBED_PORT, "/v1/embeddings", &request, 300, response, http_status) || http_status != 200)
		return false;
	try {
		const auto j = nlohmann::json::parse(response);
		out.assign(texts.size(), {});
		size_t next = 0;
		for(const auto& item : j.at("data")) {
			size_t index = next++;
			if(item.contains("index") && item["index"].is_number_unsigned())
				index = item["index"].get<size_t>();
			if(index >= out.size())
				return false;
			auto& vec = out[index];
			vec = item.at("embedding").get<std::vector<float>>();
			double norm = 0.0;
			for(const float value : vec)
				norm += static_cast<double>(value) * value;
			norm = std::sqrt(norm);
			if(norm > 0.0) {
				for(auto& value : vec)
					value = static_cast<float>(value / norm);
			}
		}
		for(const auto& vec : out) {
			if(vec.empty())
				return false;
		}
	} catch(...) {
		return false;
	}
	return true;
}

bool AiAssistant::LoadIndex() {
	embeddings.clear();
	embedding_ids.clear();
	embedding_hashes.clear();
	embedding_row.clear();
	dimension = 0;
	std::string meta_text;
	if(!ReadFile("ai/index/meta.json", meta_text))
		return false;
	size_t count = 0;
	try {
		const auto meta = nlohmann::json::parse(meta_text);
		if(meta.value("version", 0u) != INDEX_VERSION)
			return false;
		dimension = meta.value("dimension", size_t{ 0 });
		count = meta.value("count", size_t{ 0 });
	} catch(...) {
		return false;
	}
	std::string ids_text, hashes_text, vectors_text;
	if(!dimension || !ReadFile("ai/index/ids.u32", ids_text) || !ReadFile("ai/index/hashes.u64", hashes_text) || !ReadFile("ai/index/embeddings.f32", vectors_text))
		return false;
	if(ids_text.size() != count * sizeof(uint32_t) || hashes_text.size() != count * sizeof(uint64_t) || vectors_text.size() != count * dimension * sizeof(float))
		return false;
	embedding_ids.resize(count);
	embedding_hashes.resize(count);
	embeddings.resize(count * dimension);
	std::memcpy(embedding_ids.data(), ids_text.data(), ids_text.size());
	std::memcpy(embedding_hashes.data(), hashes_text.data(), hashes_text.size());
	std::memcpy(embeddings.data(), vectors_text.data(), vectors_text.size());
	for(size_t i = 0; i < count; ++i)
		embedding_row[embedding_ids[i]] = i;
	return true;
}

bool AiAssistant::SaveIndex() {
	std::error_code ec;
	std::filesystem::create_directories("ai/index", ec);
	const auto write = [&](const char* name, const void* data, size_t size) {
		const std::filesystem::path final_path = std::filesystem::path("ai/index") / name;
		auto temp_path = final_path;
		temp_path += ".part";
		{
			std::ofstream out(temp_path, std::ios::binary | std::ios::trunc);
			if(!out)
				return false;
			out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
			if(!out)
				return false;
		}
		std::filesystem::rename(temp_path, final_path, ec);
		return !ec;
	};
	const size_t count = embedding_ids.size();
	if(!write("ids.u32", embedding_ids.data(), count * sizeof(uint32_t)) || !write("hashes.u64", embedding_hashes.data(), count * sizeof(uint64_t))
	   || !write("embeddings.f32", embeddings.data(), embeddings.size() * sizeof(float)))
		return false;
	const nlohmann::json meta = { { "version", INDEX_VERSION }, { "dimension", dimension }, { "count", count } };
	const std::string meta_text = meta.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
	return write("meta.json", meta_text.data(), meta_text.size());
}

bool AiAssistant::BuildIndex() {
	if(cards.empty())
		return false;
	std::vector<size_t> todo;
	for(size_t i = 0; i < cards.size(); ++i) {
		const auto row = embedding_row.find(cards[i].code);
		if(row == embedding_row.end() || embedding_hashes[row->second] != cards[i].document_hash)
			todo.push_back(i);
	}
	if(todo.empty())
		return true;
	std::unordered_map<uint32_t, std::vector<float>> fresh;
	fresh.reserve(todo.size());
	size_t done = 0;
	for(size_t begin = 0; begin < todo.size(); begin += EMBED_BATCH) {
		{
			std::lock_guard<std::mutex> lock(mutex);
			if(quit)
				return false;
		}
		const size_t end = std::min(todo.size(), begin + EMBED_BATCH);
		std::vector<std::string> texts;
		texts.reserve(end - begin);
		for(size_t i = begin; i < end; ++i)
			texts.push_back(cards[todo[i]].document);
		std::vector<std::vector<float>> vectors;
		if(!EmbedTexts(texts, vectors))
			return false;
		for(size_t i = begin; i < end; ++i) {
			if(!dimension)
				dimension = vectors[i - begin].size();
			if(vectors[i - begin].size() != dimension)
				return false;
			fresh[cards[todo[i]].code] = std::move(vectors[i - begin]);
		}
		done = end;
		SetStatus(L"Indexing the cards for AI search: " + std::to_wstring(done * 100 / todo.size()) + L"% (only the first time)");
	}
	//one row per card, in card code order
	std::vector<uint32_t> new_ids;
	std::vector<uint64_t> new_hashes;
	std::vector<float> new_vectors;
	new_ids.reserve(cards.size());
	new_hashes.reserve(cards.size());
	new_vectors.reserve(cards.size() * dimension);
	for(const auto& card : cards) {
		const auto added = fresh.find(card.code);
		if(added != fresh.end()) {
			new_vectors.insert(new_vectors.end(), added->second.begin(), added->second.end());
		} else {
			const auto row = embedding_row.find(card.code);
			if(row == embedding_row.end())
				continue;
			const auto* source = embeddings.data() + row->second * dimension;
			new_vectors.insert(new_vectors.end(), source, source + dimension);
		}
		new_ids.push_back(card.code);
		new_hashes.push_back(card.document_hash);
	}
	embedding_ids = std::move(new_ids);
	embedding_hashes = std::move(new_hashes);
	embeddings = std::move(new_vectors);
	embedding_row.clear();
	for(size_t i = 0; i < embedding_ids.size(); ++i)
		embedding_row[embedding_ids[i]] = i;
	SaveIndex();
	return true;
}

bool AiAssistant::EnsureIndex() {
	if(index_ready)
		return true;
	if(embedding_ids.empty())
		LoadIndex();
	index_ready = BuildIndex();
	return index_ready;
}

namespace {
const char* const DEFAULT_SYSTEM_PROMPT = R"(You turn a player's English request about Yu-Gi-Oh! cards into a card search. Fill in only what the request says; use empty lists, null numbers and an empty string for everything else. "monster_types" lists card kinds that must all apply (for example synchro + tuner). Put into "effect_description" what the card's effect should do, written like Yu-Gi-Oh! card text (for example: "When this card is added to your hand (except by drawing it): you can Special Summon it"). "text_keywords" are short phrases that probably appear literally in the matching card text. "explanation" is one short sentence telling the player what you searched for.)";

const char* const DEFAULT_SCHEMA = R"({"type":"object","properties":{
"card_types":{"type":"array","items":{"type":"string","enum":["monster","spell","trap"]}},
"monster_types":{"type":"array","items":{"type":"string","enum":["normal","effect","fusion","ritual","spirit","union","gemini","tuner","synchro","flip","toon","xyz","pendulum","link","special_summon"]}},
"spell_types":{"type":"array","items":{"type":"string","enum":["normal","quick-play","continuous","equip","field","ritual"]}},
"trap_types":{"type":"array","items":{"type":"string","enum":["normal","continuous","counter"]}},
"races":{"type":"array","items":{"type":"string","enum":["warrior","spellcaster","fairy","fiend","zombie","machine","aqua","pyro","rock","winged beast","plant","insect","thunder","dragon","beast","beast-warrior","dinosaur","fish","sea serpent","reptile","psychic","divine-beast","creator god","wyrm","cyberse","illusion"]}},
"attributes":{"type":"array","items":{"type":"string","enum":["EARTH","WATER","FIRE","WIND","LIGHT","DARK","DIVINE"]}},
"level":{"type":"object","properties":{"min":{"type":["integer","null"]},"max":{"type":["integer","null"]}},"required":["min","max"]},
"rank":{"type":"object","properties":{"min":{"type":["integer","null"]},"max":{"type":["integer","null"]}},"required":["min","max"]},
"link_rating":{"type":"object","properties":{"min":{"type":["integer","null"]},"max":{"type":["integer","null"]}},"required":["min","max"]},
"atk":{"type":"object","properties":{"min":{"type":["integer","null"]},"max":{"type":["integer","null"]}},"required":["min","max"]},
"def":{"type":"object","properties":{"min":{"type":["integer","null"]},"max":{"type":["integer","null"]}},"required":["min","max"]},
"scale":{"type":"object","properties":{"min":{"type":["integer","null"]},"max":{"type":["integer","null"]}},"required":["min","max"]},
"name_contains":{"type":"string"},
"effect_description":{"type":"string"},
"text_keywords":{"type":"array","items":{"type":"string"}},
"explanation":{"type":"string"}},
"required":["card_types","monster_types","spell_types","trap_types","races","attributes","level","rank","link_rating","atk","def","scale","name_contains","effect_description","text_keywords","explanation"]})";
}

AiResult AiAssistant::HandleAsk(const AskTask& task) {
	AiResult res;
	if(cards.empty()) {
		res.message = L"The card list is not ready yet.";
		return res;
	}
	if(!EnsureServers()) {
		res.message = L"The AI helper could not start. See ai/logs for details.";
		return res;
	}
	const bool use_embeddings = EnsureIndex();
	if(system_prompt.empty() && !ReadFile("ai/prompts/system_prompt.txt", system_prompt))
		system_prompt = DEFAULT_SYSTEM_PROMPT;
	if(schema_text.empty() && !ReadFile("ai/prompts/schema.json", schema_text))
		schema_text = DEFAULT_SCHEMA;
	SetStatus(L"Thinking...");
	nlohmann::json body;
	try {
		body = {
			{ "model", "chat" },
			{ "messages", nlohmann::json::array({ { { "role", "system" }, { "content", system_prompt } }, { { "role", "user" }, { "content", task.question } } }) },
			{ "temperature", 0.1 },
			{ "max_tokens", 700 },
			{ "response_format", { { "type", "json_schema" }, { "json_schema", { { "name", "card_search" }, { "strict", true }, { "schema", nlohmann::json::parse(schema_text) } } } } }
		};
	} catch(...) {
		res.message = L"The AI search schema file is invalid.";
		return res;
	}
	const std::string request = body.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
	std::string response;
	long http_status = 0;
	if(!HttpRequest(CHAT_PORT, "/v1/chat/completions", &request, 120, response, http_status) || http_status != 200) {
		res.message = L"The AI helper did not answer.";
		return res;
	}
	Criteria criteria;
	try {
		const auto reply = nlohmann::json::parse(response);
		const auto content = reply.at("choices").at(0).at("message").at("content").get<std::string>();
		criteria = ParseCriteria(nlohmann::json::parse(content));
	} catch(...) {
		res.message = L"The AI helper's answer could not be understood.";
		return res;
	}
	std::vector<size_t> candidates;
	for(size_t i = 0; i < cards.size(); ++i) {
		const auto& card = cards[i];
		if(!task.include_unofficial && (card.ot & SCOPE_OFFICIAL) != card.ot)
			continue;
		if(Matches(card, criteria))
			candidates.push_back(i);
	}
	const std::wstring explanation = BufferIO::DecodeUTF8(criteria.explanation);
	if(candidates.empty()) {
		res.ok = true;
		res.message = L"No cards matched. " + explanation;
		return res;
	}
	std::vector<float> query;
	if(use_embeddings && !criteria.effect_description.empty()) {
		std::vector<std::vector<float>> vectors;
		if(EmbedTexts({ "search_query: " + criteria.effect_description }, vectors) && vectors[0].size() == dimension)
			query = std::move(vectors[0]);
	}
	std::vector<std::pair<float, size_t>> scored;
	scored.reserve(candidates.size());
	for(const size_t index : candidates) {
		const auto& card = cards[index];
		float score = 0.0f;
		if(!query.empty()) {
			const auto row = embedding_row.find(card.code);
			if(row != embedding_row.end()) {
				const float* vec = embeddings.data() + row->second * dimension;
				for(size_t k = 0; k < dimension; ++k)
					score += vec[k] * query[k];
			}
		}
		for(const auto& keyword : criteria.keywords) {
			if(card.text_lower.find(keyword) != std::string::npos)
				score += KEYWORD_BONUS;
		}
		scored.emplace_back(score, index);
	}
	std::stable_sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
	if(!criteria.sort_by.empty()) {
		const auto key = [&](size_t index) -> int64_t {
			const auto& card = cards[index];
			if(criteria.sort_by.rfind("atk", 0) == 0) return card.attack;
			if(criteria.sort_by.rfind("def", 0) == 0) return card.defense;
			return card.level;
		};
		const bool descending = criteria.sort_by.size() > 5 && criteria.sort_by.compare(criteria.sort_by.size() - 5, 5, "_desc") == 0;
		std::stable_sort(scored.begin(), scored.end(), [&](const auto& a, const auto& b) { return descending ? key(a.second) > key(b.second) : key(a.second) < key(b.second); });
	}
	if(criteria.limit > 0 && scored.size() > static_cast<size_t>(criteria.limit))
		scored.resize(criteria.limit);
	if(scored.size() > MAX_RESULTS)
		scored.resize(MAX_RESULTS);
	res.ok = true;
	res.ranked_by_meaning = !query.empty() || !criteria.keywords.empty() || !criteria.sort_by.empty();
	for(const auto& entry : scored)
		res.codes.push_back(cards[entry.second].code);
	res.message = explanation + L" (" + std::to_wstring(candidates.size()) + L" cards" + (candidates.size() > scored.size() ? L", showing the best " + std::to_wstring(scored.size()) : std::wstring()) + L")";
	return res;
}

void AiAssistant::WorkerLoop() {
	//an exception inside the worker would end the whole program: keep going instead
	while(true) {
		try {
			WorkerLoopBody();
			return;
		} catch(...) {
			std::unique_lock<std::mutex> lock(mutex);
			if(quit)
				return;
			cv.wait_for(lock, std::chrono::seconds(1));
		}
	}
}

void AiAssistant::WorkerLoopBody() {
	while(true) {
		AskTask task;
		bool have_task = false;
		bool do_start = false;
		bool stop_now = false;
		std::vector<AiCard> snapshot;
		{
			std::unique_lock<std::mutex> lock(mutex);
			cv.wait_for(lock, std::chrono::seconds(1), [&]() { return quit || start_requested || !asks.empty(); });
			if(quit)
				return;
			if(has_pending_snapshot) {
				snapshot = std::move(pending_snapshot);
				has_pending_snapshot = false;
			}
			if(!asks.empty()) {
				task = std::move(asks.front());
				asks.pop_front();
				have_task = true;
			}
			do_start = start_requested;
			start_requested = false;
			stop_now = stop_scheduled && asks.empty() && !do_start && std::chrono::steady_clock::now() >= stop_at;
			if(stop_now)
				stop_scheduled = false;
		}
		if(!snapshot.empty()) {
			cards = std::move(snapshot);
			index_ready = false;
		}
		if(have_task) {
			AiResult answer = HandleAsk(task);
			{
				std::lock_guard<std::mutex> lock(mutex);
				result = std::move(answer);
				has_result = true;
				status = L"AI search ready. Type what you are looking for in plain English.";
			}
			std::lock_guard<std::mutex> lock(mutex);
			busy = !asks.empty();
		} else if(do_start) {
			busy = true;
			if(EnsureServers() && !cards.empty()) {
				EnsureIndex();
				SetStatus(L"AI search ready. Type what you are looking for in plain English.");
			} else {
				SetStatus(L"The AI helper could not start. See ai/logs for details.");
			}
			busy = false;
		} else if(stop_now) {
			StopServers();
			SetStatus(L"");
		}
	}
}

}
