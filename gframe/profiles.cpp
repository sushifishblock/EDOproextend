#include "profiles.h"
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include "game.h"
#include "profile_name.h"

namespace ygo {

namespace {
std::string ProfilePath(int id) {
	return "profiles/" + std::to_string(id) + ".json";
}
}

int Profiles::OwnId() {
	static int id = 0;
	if(id)
		return id;
	std::ifstream in("profile.id");
	int value = 0;
	if(in >> value && value >= 1 && value <= 255) {
		id = value;
		return id;
	}
	std::random_device random;
	id = static_cast<int>(random() % 255) + 1;
	std::ofstream("profile.id", std::ios::trunc) << id;
	return id;
}

std::wstring Profiles::SignName(const std::wstring& name) {
	std::error_code ec;
	if(std::filesystem::exists("profile.off", ec))
		return name;
	return SignProfileName(name, OwnId());
}

int Profiles::IdOfName(const std::wstring& name) {
	return ProfileIdOfName(name);
}

void Profiles::Save(int id, const std::string& pack_json) {
	if(id < 1 || id > 255)
		return;
	std::error_code ec;
	std::filesystem::create_directories("profiles", ec);
	std::ofstream(ProfilePath(id), std::ios::binary | std::ios::trunc) << pack_json;
	resolved = false;
}

void Profiles::ResetOpponent() {
	resolved = false;
}

std::shared_ptr<RarityProfile> Profiles::Opponent() {
	if(resolved)
		return opponent;
	resolved = true;
	opponent = nullptr;
	if(!mainGame)
		return nullptr;
	for(const auto& name : mainGame->dInfo.opponames) {
		const int id = IdOfName(name);
		if(!id)
			continue;
		std::ifstream in(ProfilePath(id), std::ios::binary);
		if(!in.is_open())
			continue;
		std::ostringstream text;
		text << in.rdbuf();
		if((opponent = RarityFx::LoadProfile(text.str())))
			break;
	}
	return opponent;
}

}
