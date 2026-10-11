#include "rarity_rules.h"
#include <algorithm>
#include <cwctype>
#include <nlohmann/json.hpp>

namespace ygo {

namespace {
std::wstring DecodeUtf8Lower(const std::string& text) {
	std::wstring out;
	for(size_t i = 0; i < text.size();) {
		const unsigned char c = text[i];
		uint32_t code = c;
		size_t extra = 0;
		if(c >= 0xf0) { code = c & 0x07; extra = 3; }
		else if(c >= 0xe0) { code = c & 0x0f; extra = 2; }
		else if(c >= 0xc0) { code = c & 0x1f; extra = 1; }
		++i;
		for(; extra && i < text.size(); --extra, ++i)
			code = (code << 6) | (static_cast<unsigned char>(text[i]) & 0x3f);
		if(code > 0xffff)
			continue; //(the card names the game shows are in the basic plane)
		out.push_back(static_cast<wchar_t>(std::towlower(static_cast<wint_t>(code))));
	}
	return out;
}

template<typename T>
T Number(const nlohmann::json& object, const char* key) {
	const auto it = object.find(key);
	if(it == object.end() || !it->is_number_unsigned())
		return 0;
	return it->get<T>();
}
}

bool RarityRule::HasCondition() const {
	return setcode || type_all || type_none || attribute_any || race_any || level_min || level_max || !name.empty();
}

bool RarityRule::Matches(const CardFacts& card) const {
	if(!HasCondition())
		return false; //(it would turn every card into that rarity)
	if(setcode) {
		const uint16_t base = setcode & 0xfff;
		const uint16_t sub = setcode & 0xf000;
		bool found = false;
		for(const auto sc : card.setcodes) {
			if((sc & 0xfff) == base && (sc & 0xf000 & sub) == sub) {
				found = true;
				break;
			}
		}
		if(!found)
			return false;
	}
	if((card.type & type_all) != type_all || (card.type & type_none) != 0)
		return false;
	if(attribute_any && !(card.attribute & attribute_any))
		return false;
	if(race_any && !(card.race & race_any))
		return false;
	if(level_min && card.level < level_min)
		return false;
	if(level_max && card.level > level_max)
		return false;
	if(!name.empty()) {
		std::wstring lower = card.name;
		for(auto& c : lower)
			c = static_cast<wchar_t>(std::towlower(static_cast<wint_t>(c)));
		if(lower.find(DecodeUtf8Lower(name)) == std::wstring::npos)
			return false;
	}
	return true;
}

bool RarityRules::Parse(const std::string& json, std::vector<RarityRule>& rules) {
	rules.clear();
	try {
		const auto root = nlohmann::json::parse(json);
		if(!root.is_object() || !root.contains("rules") || !root["rules"].is_array())
			return false;
		for(const auto& item : root["rules"]) {
			if(!item.is_object() || !item.contains("rarity") || !item["rarity"].is_string())
				continue;
			RarityRule rule;
			rule.rarity = item["rarity"].get<std::string>();
			rule.setcode = Number<uint32_t>(item, "setcode");
			rule.type_all = Number<uint32_t>(item, "type_all");
			rule.type_none = Number<uint32_t>(item, "type_none");
			rule.attribute_any = Number<uint32_t>(item, "attribute_any");
			rule.race_any = Number<uint64_t>(item, "race_any");
			rule.level_min = Number<uint32_t>(item, "level_min");
			rule.level_max = Number<uint32_t>(item, "level_max");
			if(item.contains("name") && item["name"].is_string())
				rule.name = item["name"].get<std::string>();
			//(a rule with no condition is kept, so one that is being made can be saved; it never matches a card)
			rules.push_back(std::move(rule));
			if(rules.size() >= 200)
				break;
		}
		return true;
	} catch(...) {
		rules.clear();
		return false;
	}
}

std::string RarityRules::ToJson(const std::vector<RarityRule>& rules) {
	nlohmann::json list = nlohmann::json::array();
	for(const auto& rule : rules) {
		nlohmann::json item = { { "rarity", rule.rarity } };
		if(rule.setcode) item["setcode"] = rule.setcode;
		if(rule.type_all) item["type_all"] = rule.type_all;
		if(rule.type_none) item["type_none"] = rule.type_none;
		if(rule.attribute_any) item["attribute_any"] = rule.attribute_any;
		if(rule.race_any) item["race_any"] = rule.race_any;
		if(rule.level_min) item["level_min"] = rule.level_min;
		if(rule.level_max) item["level_max"] = rule.level_max;
		if(!rule.name.empty()) item["name"] = rule.name;
		list.push_back(std::move(item));
	}
	return nlohmann::json{ { "version", 1 }, { "rules", list } }.dump(1);
}

const RarityRule* RarityRules::Find(const std::vector<RarityRule>& rules, const CardFacts& card) {
	for(const auto& rule : rules) {
		if(rule.Matches(card))
			return &rule;
	}
	return nullptr;
}

}
