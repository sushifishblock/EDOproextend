#include "genesys.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>
#include "curl.h"
#include "config.h"
#include "game_config.h"
#include "utils.h"
#include <nlohmann/json.hpp>

namespace ygo {

void Genesys::Load() {
	points.clear();
	//a restart of the game loads the list again, so a pending restart request is done
	restart_wanted = false;
	has_pending = false;
	try {
		std::ifstream file("genesys_points.json", std::ios::binary);
		if(!file.is_open())
			return;
		const auto root = nlohmann::json::parse(file);
		if(root.contains("cap") && root["cap"].is_number())
			cap = root["cap"].get<int>();
		if(root.contains("points") && root["points"].is_object()) {
			for(const auto& item : root["points"].items()) {
				if(item.value().is_number())
					points[static_cast<uint32_t>(std::stoul(item.key()))] = item.value().get<int>();
			}
		}
	} catch(...) {
		points.clear();
	}
}

namespace {
size_t WriteBody(char* data, size_t size, size_t count, void* user) {
	static_cast<std::string*>(user)->append(data, size * count);
	return size * count;
}
}

void Genesys::StartUpdate() {
	if(update_started.exchange(true))
		return;
	std::thread(&Genesys::UpdateThread).detach();
}

//the list comes from the YGOPRODeck card database; if anything fails the list that is already there stays in use
void Genesys::UpdateThread() {
	try {
		auto* curl = curl_easy_init();
		if(!curl)
			return;
		std::string body;
		curl_easy_setopt(curl, CURLOPT_URL, "https://db.ygoprodeck.com/api/v7/cardinfo.php?format=genesys&misc=yes");
		curl_easy_setopt(curl, CURLOPT_USERAGENT, "edopro-genesys-updater");
		curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteBody);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
		curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
		curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
		if(ygo::gGameConfig->ssl_certificate_path.size() && Utils::FileExists(Utils::ToPathString(ygo::gGameConfig->ssl_certificate_path)))
			curl_easy_setopt(curl, CURLOPT_CAINFO, ygo::gGameConfig->ssl_certificate_path.data());
		const auto result = curl_easy_perform(curl);
		long status = 0;
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
		curl_easy_cleanup(curl);
		if(result != CURLE_OK || status != 200) {
			std::ofstream("genesys_update.log", std::ios::trunc) << "download failed: curl " << static_cast<int>(result) << ", http " << status << std::endl;
			return;
		}
		const auto root = nlohmann::json::parse(body);
		body.clear();
		body.shrink_to_fit();
		std::unordered_map<uint32_t, int> fresh;
		nlohmann::json out_points = nlohmann::json::object();
		for(const auto& card : root.at("data")) {
			if(!card.contains("misc_info") || !card["misc_info"].is_array() || card["misc_info"].empty())
				continue;
			const auto& info = card["misc_info"][0];
			if(!info.contains("genesys_points") || !info["genesys_points"].is_number())
				continue;
			const int value = info["genesys_points"].get<int>();
			if(value > 0) {
				fresh[card.at("id").get<uint32_t>()] = value;
				out_points[std::to_string(card.at("id").get<uint32_t>())] = value;
			}
		}
		//a list that is empty or far smaller than a real one means the answer was not what we expected
		if(fresh.size() < 100)
			return;
		//nothing to do when the list the game started with is already the current one
		if(fresh == points)
			return;
		{
			std::ofstream file("genesys_points.json.tmp", std::ios::binary | std::ios::trunc);
			file << nlohmann::json{ { "cap", cap }, { "points", out_points } }.dump();
		}
		std::error_code ec;
		std::filesystem::rename("genesys_points.json.tmp", "genesys_points.json", ec);
		if(ec) {
			std::filesystem::remove("genesys_points.json", ec);
			std::filesystem::rename("genesys_points.json.tmp", "genesys_points.json", ec);
		}
		std::lock_guard<std::mutex> lock(pending_mutex);
		pending_points = std::move(fresh);
		has_pending = true;
		restart_wanted = true;
	} catch(...) {
	}
}

bool Genesys::ApplyUpdate() {
	if(!has_pending.load())
		return false;
	std::lock_guard<std::mutex> lock(pending_mutex);
	points = std::move(pending_points);
	pending_points.clear();
	has_pending = false;
	return true;
}

int Genesys::Points(const CardDataC* card) {
	if(!card)
		return 0;
	auto it = points.find(card->code);
	if(it == points.end() && card->alias)
		it = points.find(card->alias);
	return it == points.end() ? 0 : it->second;
}

bool Genesys::Illegal(const CardDataC* card) {
	return card && (card->type & (TYPE_LINK | TYPE_PENDULUM));
}

Genesys::Totals Genesys::Count(const Deck& deck) {
	Totals totals;
	int index = 0;
	for(const auto* list : { &deck.main, &deck.extra, &deck.side }) {
		int& section = index == 0 ? totals.main : index == 1 ? totals.extra : totals.side;
		for(const auto* card : *list) {
			section += Points(card);
			if(Illegal(card))
				totals.illegal++;
		}
		totals.points += section;
		index++;
	}
	return totals;
}

}
