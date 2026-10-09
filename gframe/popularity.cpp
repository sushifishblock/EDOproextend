#include "popularity.h"
#include <climits>
#include <filesystem>
#include <fstream>
#include <thread>
#include "curl.h"
#include "config.h"
#include "game_config.h"
#include "utils.h"
#include <nlohmann/json.hpp>

namespace ygo {

namespace {
constexpr int OCG_ONLY_OFFSET = 100000; //cards only ranked in the OCG come after every TCG ranked card
constexpr int PAGE_SIZE = 5000;

size_t WriteBody(char* data, size_t size, size_t count, void* user) {
	static_cast<std::string*>(user)->append(data, size * count);
	return size * count;
}

bool Download(const std::string& url, std::string& body) {
	auto* curl = curl_easy_init();
	if(!curl)
		return false;
	body.clear();
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "edopro-popularity-updater");
	curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteBody);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 180L);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	if(ygo::gGameConfig->ssl_certificate_path.size() && Utils::FileExists(Utils::ToPathString(ygo::gGameConfig->ssl_certificate_path)))
		curl_easy_setopt(curl, CURLOPT_CAINFO, ygo::gGameConfig->ssl_certificate_path.data());
	const auto result = curl_easy_perform(curl);
	long status = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
	curl_easy_cleanup(curl);
	return result == CURLE_OK && status == 200;
}

int RankFrom(const nlohmann::json& card, const char* key) {
	if(card.contains(key) && card[key].is_number()) {
		const double value = card[key].get<double>();
		if(value > 0 && value < 1e9)
			return static_cast<int>(value);
	}
	return 0;
}
}

void Popularity::Load() {
	ranks.clear();
	has_pending = false;
	try {
		std::ifstream file("popularity.json", std::ios::binary);
		if(!file.is_open())
			return;
		const auto root = nlohmann::json::parse(file);
		for(const auto& item : root.at("ranks").items()) {
			if(item.value().is_number())
				ranks[static_cast<uint32_t>(std::stoul(item.key()))] = item.value().get<int>();
		}
	} catch(...) {
		ranks.clear();
	}
}

void Popularity::StartUpdate() {
	if(update_started.exchange(true))
		return;
	std::thread(&Popularity::UpdateThread).detach();
}

void Popularity::UpdateThread() {
	try {
		std::unordered_map<uint32_t, int> fresh;
		std::string body;
		for(int skip = 0;; skip += PAGE_SIZE) {
			if(!Download("https://www.yugiohmeta.com/api/v1/cards?limit=" + std::to_string(PAGE_SIZE) + "&skip=" + std::to_string(skip), body)) {
				std::ofstream("popularity_update.log", std::ios::trunc) << "download failed at card " << skip << std::endl;
				return;
			}
			const auto page = nlohmann::json::parse(body);
			for(const auto& card : page) {
				if(!card.contains("konamiID"))
					continue;
				uint32_t code = 0;
				try {
					code = static_cast<uint32_t>(std::stoul(card["konamiID"].get<std::string>()));
				} catch(...) {
					continue;
				}
				int rank = RankFrom(card, "tcgPopRank");
				if(!rank) {
					if(const int ocg = RankFrom(card, "ocgPopRank"))
						rank = OCG_ONLY_OFFSET + ocg;
				}
				if(code && rank) {
					auto it = fresh.find(code);
					if(it == fresh.end() || rank < it->second)
						fresh[code] = rank;
				}
			}
			if(page.size() < static_cast<size_t>(PAGE_SIZE))
				break;
		}
		//a ranking this small means the answer was not what we expected
		if(fresh.size() < 1000 || fresh == ranks)
			return;
		nlohmann::json out = nlohmann::json::object();
		for(const auto& entry : fresh)
			out[std::to_string(entry.first)] = entry.second;
		{
			std::ofstream file("popularity.json.tmp", std::ios::binary | std::ios::trunc);
			file << nlohmann::json{ { "ranks", out } }.dump();
		}
		std::error_code ec;
		std::filesystem::rename("popularity.json.tmp", "popularity.json", ec);
		if(ec) {
			std::filesystem::remove("popularity.json", ec);
			std::filesystem::rename("popularity.json.tmp", "popularity.json", ec);
		}
		std::lock_guard<std::mutex> lock(pending_mutex);
		pending_ranks = std::move(fresh);
		has_pending = true;
	} catch(...) {
	}
}

bool Popularity::ApplyUpdate() {
	if(!has_pending.load())
		return false;
	std::lock_guard<std::mutex> lock(pending_mutex);
	ranks = std::move(pending_ranks);
	pending_ranks.clear();
	has_pending = false;
	return true;
}

int Popularity::Rank(const CardDataC* card) {
	if(!card)
		return INT_MAX;
	auto it = ranks.find(card->code);
	if(it == ranks.end() && card->alias)
		it = ranks.find(card->alias);
	return it == ranks.end() ? INT_MAX : it->second;
}

bool Popularity::Less(const CardDataC* a, const CardDataC* b) {
	const int ra = Rank(a), rb = Rank(b);
	if(ra != rb)
		return ra < rb;
	return DataManager::deck_sort_name(a, b);
}

}
