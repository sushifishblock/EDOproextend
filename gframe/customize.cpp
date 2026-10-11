#include "game.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <irrlicht.h>
#include "art_fit.h"
#include "data_manager.h"
#include "menu_handler.h"
#include "rarity.h"
#include "utils.h"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <commdlg.h>
#pragma comment(lib, "comdlg32.lib")
#endif

namespace ygo {

//The Customize window of the main menu: rarity rules (every card of an archetype / type... gets a rarity) and your own sleeves and playmat
//(pick any picture, it is cut to the right size). The files it writes are the same ones the Rarity Studio writes.

namespace {
using namespace irr;

// ------------------------------------------------------------------ shared helpers
std::string ToUtf8(const wchar_t* text) {
	return std::filesystem::path(std::wstring(text)).u8string();
}
std::wstring FromUtf8(const std::string& text) {
	return std::filesystem::u8path(text).wstring();
}
std::wstring Lower(std::wstring text) {
	for(auto& c : text)
		c = static_cast<wchar_t>(::towlower(c));
	return text;
}
constexpr uint32_t KEEP = 0xffffffffu; //item data of "(several, kept)": the mask the rule has stays as it is

struct Choice {
	const wchar_t* name;
	uint32_t bit;
};
const Choice KINDS[] = { { L"Monster", 0x1 }, { L"Spell", 0x2 }, { L"Trap", 0x4 } };
const Choice SUBTYPES[] = { { L"Normal", 0x10 }, { L"Effect", 0x20 }, { L"Fusion", 0x40 }, { L"Ritual", 0x80 }, { L"Spirit", 0x200 }, { L"Union", 0x400 },
							{ L"Gemini", 0x800 }, { L"Tuner", 0x1000 }, { L"Synchro", 0x2000 }, { L"Flip", 0x200000 }, { L"Toon", 0x400000 }, { L"Xyz", 0x800000 },
							{ L"Pendulum", 0x1000000 }, { L"Link", 0x4000000 }, { L"Quick-Play", 0x10000 }, { L"Continuous", 0x20000 }, { L"Equip", 0x40000 },
							{ L"Field", 0x80000 }, { L"Counter", 0x100000 } };
const Choice ATTRIBUTES[] = { { L"EARTH", 0x1 }, { L"WATER", 0x2 }, { L"FIRE", 0x4 }, { L"WIND", 0x8 }, { L"LIGHT", 0x10 }, { L"DARK", 0x20 }, { L"DIVINE", 0x40 } };
const Choice RACES[] = { { L"Warrior", 0x1 }, { L"Spellcaster", 0x2 }, { L"Fairy", 0x4 }, { L"Fiend", 0x8 }, { L"Zombie", 0x10 }, { L"Machine", 0x20 }, { L"Aqua", 0x40 },
						 { L"Pyro", 0x80 }, { L"Rock", 0x100 }, { L"Winged Beast", 0x200 }, { L"Plant", 0x400 }, { L"Insect", 0x800 }, { L"Thunder", 0x1000 },
						 { L"Dragon", 0x2000 }, { L"Beast", 0x4000 }, { L"Beast-Warrior", 0x8000 }, { L"Dinosaur", 0x10000 }, { L"Fish", 0x20000 },
						 { L"Sea Serpent", 0x40000 }, { L"Reptile", 0x80000 }, { L"Psychic", 0x100000 }, { L"Divine-Beast", 0x200000 }, { L"Creator God", 0x400000 },
						 { L"Wyrm", 0x800000 }, { L"Cyberse", 0x1000000 }, { L"Illusion", 0x2000000 } };

template<size_t N>
uint32_t MaskOf(const Choice (&table)[N]) {
	uint32_t mask = 0;
	for(const auto& c : table)
		mask |= c.bit;
	return mask;
}
int BitCount(uint32_t v) {
	int n = 0;
	for(; v; v &= v - 1)
		++n;
	return n;
}

template<size_t N>
void FillMaskCombo(gui::IGUIComboBox* box, const wchar_t* any, const Choice (&table)[N], uint32_t current) {
	box->clear();
	box->addItem(any, 0);
	int selected = 0;
	const uint32_t part = current & MaskOf(table);
	for(size_t i = 0; i < N; ++i) {
		box->addItem(table[i].name, table[i].bit);
		if(part == table[i].bit)
			selected = static_cast<int>(i) + 1;
	}
	if(BitCount(part) > 1) {
		box->addItem(L"(several, kept)", KEEP);
		selected = static_cast<int>(N) + 1;
	}
	box->setSelected(selected);
}
template<size_t N>
uint32_t ReadMaskCombo(gui::IGUIComboBox* box, const Choice (&table)[N], uint32_t old_value) {
	const uint32_t data = box->getItemData(box->getSelected());
	const uint32_t table_mask = MaskOf(table);
	return (old_value & ~table_mask) | (data == KEEP ? (old_value & table_mask) : data);
}
template<size_t N>
std::wstring NamesOf(const Choice (&table)[N], uint32_t mask) {
	std::wstring text;
	for(const auto& c : table) {
		if(mask & c.bit)
			text += (text.empty() ? L"" : L"/") + std::wstring(c.name);
	}
	return text;
}

// ------------------------------------------------------------------ rules tab
struct RulesUi {
	gui::IGUIListBox *list = nullptr, *arch = nullptr;
	gui::IGUIComboBox *rarity = nullptr, *kind = nullptr, *subtype = nullptr, *attr = nullptr, *race = nullptr;
	std::vector<uint32_t> arch_codes; //the set code of every line of the archetype list (0 = any archetype)
	gui::IGUIEditBox *arch_filter = nullptr, *level_min = nullptr, *level_max = nullptr, *name = nullptr;
	gui::IGUIStaticText* result = nullptr;
	std::vector<std::pair<uint32_t, std::wstring>> set_names;
	bool set_names_loaded = false;
	int selected = -1;
	bool refreshing = false;
} rules_ui;

std::wstring SetNameOf(uint32_t code) {
	for(const auto& entry : rules_ui.set_names) {
		if(entry.first == code)
			return entry.second;
	}
	wchar_t text[32];
	swprintf(text, 32, L"archetype 0x%x", code);
	return text;
}

std::wstring Describe(const RarityRule& rule) {
	std::wstring text;
	const auto add = [&](const std::wstring& part) {
		if(!part.empty())
			text += (text.empty() ? L"" : L", ") + part;
	};
	if(rule.setcode)
		add(SetNameOf(rule.setcode));
	add(NamesOf(KINDS, rule.type_all));
	add(NamesOf(SUBTYPES, rule.type_all));
	add(NamesOf(ATTRIBUTES, rule.attribute_any));
	add(NamesOf(RACES, static_cast<uint32_t>(rule.race_any)));
	if(rule.level_min || rule.level_max)
		add(L"level " + (rule.level_min ? std::to_wstring(rule.level_min) : std::wstring(L"any")) + L"-" + (rule.level_max ? std::to_wstring(rule.level_max) : std::wstring(L"any")));
	if(!rule.name.empty())
		add(L"name has \"" + FromUtf8(rule.name) + L"\"");
	if(text.empty())
		text = L"(no condition yet)";
	Rarity rarity = Rarity::NORMAL;
	for(int i = 1; i < static_cast<int>(Rarity::COUNT); ++i) {
		if(rule.rarity == RarityFx::Id(static_cast<Rarity>(i)))
			rarity = static_cast<Rarity>(i);
	}
	return text + L"  ->  " + RarityFx::Name(rarity);
}

//the archetype list: "(any archetype)", the chosen one, then every archetype that contains the search text
void FillArchList(uint32_t current) {
	auto* box = rules_ui.arch;
	box->clear();
	rules_ui.arch_codes.clear();
	box->addItem(L"(any archetype)");
	rules_ui.arch_codes.push_back(0);
	int selected = 0;
	if(current) {
		box->addItem((SetNameOf(current) + L"   [chosen]").c_str());
		rules_ui.arch_codes.push_back(current);
		selected = 1;
	}
	const std::wstring filter = Lower(rules_ui.arch_filter->getText());
	int shown = 0;
	for(const auto& entry : rules_ui.set_names) {
		if(entry.first == current)
			continue;
		if(!filter.empty() && Lower(entry.second).find(filter) == std::wstring::npos)
			continue;
		if(++shown > 400)
			break;
		box->addItem(entry.second.c_str());
		rules_ui.arch_codes.push_back(entry.first);
	}
	box->setSelected(selected);
}

void RefillRuleList() {
	const bool was = rules_ui.refreshing;
	rules_ui.refreshing = true;
	rules_ui.list->clear();
	for(const auto& rule : RarityFx::Rules()) {
		const std::wstring text = std::to_wstring(rules_ui.list->getItemCount() + 1) + L". " + Describe(rule);
		rules_ui.list->addItem(text.c_str());
	}
	if(rules_ui.selected >= 0 && rules_ui.selected < static_cast<int>(rules_ui.list->getItemCount()))
		rules_ui.list->setSelected(rules_ui.selected);
	rules_ui.refreshing = was;
}

void ShowRuleResult() {
	const auto& rules = RarityFx::Rules();
	if(rules_ui.selected < 0 || rules_ui.selected >= static_cast<int>(rules.size())) {
		rules_ui.result->setText(L"No rules yet. Press \"Add rule\", pick a rarity and what the cards have in common.");
		return;
	}
	const auto& rule = rules[rules_ui.selected];
	if(!rule.setcode && !rule.type_all && !rule.type_none && !rule.attribute_any && !rule.race_any && !rule.level_min && !rule.level_max && rule.name.empty()) {
		rules_ui.result->setText(L"This rule has no condition yet, so it does nothing. Pick an archetype, a type, an attribute...");
		return;
	}
	std::vector<std::wstring> sample;
	const size_t count = RarityFx::CountCaught(rules, rules_ui.selected, sample, 6);
	std::wstring text = L"This rule is the first to catch " + std::to_wstring(count) + L" cards";
	if(!sample.empty()) {
		text += L":\n";
		for(size_t i = 0; i < sample.size(); ++i)
			text += (i ? L", " : L"") + sample[i];
		if(count > sample.size())
			text += L" ...";
	}
	rules_ui.result->setText(text.c_str());
}

void LoadRuleEditor() {
	rules_ui.refreshing = true;
	const auto& rules = RarityFx::Rules();
	const bool has = rules_ui.selected >= 0 && rules_ui.selected < static_cast<int>(rules.size());
	for(auto* element : std::initializer_list<gui::IGUIElement*>{ rules_ui.rarity, rules_ui.arch, rules_ui.arch_filter, rules_ui.kind, rules_ui.subtype, rules_ui.attr, rules_ui.race, rules_ui.level_min, rules_ui.level_max, rules_ui.name })
		element->setEnabled(has);
	if(has) {
		const auto& rule = rules[rules_ui.selected];
		rules_ui.rarity->clear();
		int selected = 0;
		for(int i = 1; i < static_cast<int>(Rarity::COUNT); ++i) {
			const auto rarity = static_cast<Rarity>(i);
			if(!RarityFx::Defined(rarity))
				continue;
			if(rule.rarity == RarityFx::Id(rarity))
				selected = static_cast<int>(rules_ui.rarity->getItemCount());
			rules_ui.rarity->addItem(RarityFx::Name(rarity), i);
		}
		rules_ui.rarity->setSelected(selected);
		FillArchList(rule.setcode);
		FillMaskCombo(rules_ui.kind, L"Any card", KINDS, rule.type_all);
		FillMaskCombo(rules_ui.subtype, L"Any", SUBTYPES, rule.type_all);
		FillMaskCombo(rules_ui.attr, L"Any", ATTRIBUTES, rule.attribute_any);
		FillMaskCombo(rules_ui.race, L"Any", RACES, static_cast<uint32_t>(rule.race_any));
		rules_ui.level_min->setText(std::to_wstring(rule.level_min).c_str());
		rules_ui.level_max->setText(std::to_wstring(rule.level_max).c_str());
		rules_ui.name->setText(FromUtf8(rule.name).c_str());
	}
	rules_ui.refreshing = false;
	ShowRuleResult();
}

//the editor widgets -> the selected rule
void ApplyRuleEdits() {
	auto rules = RarityFx::Rules();
	if(rules_ui.refreshing || rules_ui.selected < 0 || rules_ui.selected >= static_cast<int>(rules.size()))
		return;
	auto& rule = rules[rules_ui.selected];
	if(rules_ui.rarity->getSelected() >= 0)
		rule.rarity = RarityFx::Id(static_cast<Rarity>(rules_ui.rarity->getItemData(rules_ui.rarity->getSelected())));
	const int arch_line = rules_ui.arch->getSelected();
	if(arch_line >= 0 && arch_line < static_cast<int>(rules_ui.arch_codes.size()))
		rule.setcode = rules_ui.arch_codes[arch_line];
	rule.type_all = ReadMaskCombo(rules_ui.kind, KINDS, rule.type_all);
	rule.type_all = ReadMaskCombo(rules_ui.subtype, SUBTYPES, rule.type_all);
	rule.attribute_any = ReadMaskCombo(rules_ui.attr, ATTRIBUTES, rule.attribute_any);
	rule.race_any = ReadMaskCombo(rules_ui.race, RACES, static_cast<uint32_t>(rule.race_any));
	rule.level_min = static_cast<uint32_t>(std::clamp(_wtoi(rules_ui.level_min->getText()), 0, 13));
	rule.level_max = static_cast<uint32_t>(std::clamp(_wtoi(rules_ui.level_max->getText()), 0, 13));
	rule.name = ToUtf8(rules_ui.name->getText());
	if(rule.name.size() > 60)
		rule.name.resize(60);
	RarityFx::SetRules(std::move(rules));
	RefillRuleList();
	ShowRuleResult();
}

// ------------------------------------------------------------------ sleeves and playmat tab
struct Slot {
	const char* file;
	const wchar_t* title;
	int width, height;
	bool sleeve;
	std::wstring path; //the picture that was chosen
	ArtParams params;
	Pixels source; //the chosen picture, at most 2048 pixels on the long side
	bool loaded = false;
};
Slot slots[3] = { { "sleeve.png", L"My sleeve", 480, 700, true }, { "sleeve_opponent.png", L"Opponent's sleeve", 480, 700, true }, { "playmat.png", L"Playmat", 1024, 820, false } };

struct ArtUi {
	gui::IGUIComboBox *slot = nullptr, *fit = nullptr;
	gui::IGUIScrollBar *zoom = nullptr, *x = nullptr, *y = nullptr, *bright = nullptr, *border = nullptr;
	gui::IGUIStaticText *file_text = nullptr, *border_label = nullptr, *note = nullptr;
	gui::IGUIImage* preview = nullptr;
	video::ITexture* preview_texture = nullptr;
	int current = 0;
	bool refreshing = false;
	bool dirty = false;
	uint32_t dirty_time = 0;
	int texture_counter = 0;
	bool settings_loaded = false;
	bool sleeve_changed = false; //a sleeve picture was written or removed since the window was opened
} art_ui;

const char* SETTINGS_FILE = "custom_art/settings.txt";

void SaveSettings() {
	std::error_code ec;
	std::filesystem::create_directories("custom_art", ec);
	std::ostringstream out;
	for(int i = 0; i < 3; ++i) {
		const auto& s = slots[i];
		out << i << '\t' << (s.params.fit ? 1 : 0) << '\t' << s.params.zoom << '\t' << s.params.ox << '\t' << s.params.oy << '\t' << s.params.bright << '\t' << s.params.border << '\t'
			<< std::filesystem::path(s.path).u8string() << '\n';
	}
	std::ofstream("custom_art/settings.txt", std::ios::binary | std::ios::trunc) << out.str();
}

void LoadSettings() {
	if(art_ui.settings_loaded)
		return;
	art_ui.settings_loaded = true;
	std::ifstream file(SETTINGS_FILE, std::ios::binary);
	std::string line;
	while(std::getline(file, line)) {
		std::istringstream in(line);
		int index = -1, fit = 0;
		ArtParams p;
		if(!(in >> index >> fit >> p.zoom >> p.ox >> p.oy >> p.bright >> p.border) || index < 0 || index > 2)
			continue;
		in.get(); //the tab
		std::string path;
		std::getline(in, path);
		p.fit = fit != 0;
		p.zoom = std::clamp(p.zoom, 0.5f, 4.0f);
		p.ox = std::clamp(p.ox, -1.0f, 1.0f);
		p.oy = std::clamp(p.oy, -1.0f, 1.0f);
		p.bright = std::clamp(p.bright, 0.3f, 1.5f);
		p.border = std::clamp(p.border, 0.0f, 0.06f);
		slots[index].params = p;
		slots[index].path = FromUtf8(path);
	}
}

Pixels ReadPicture(video::IVideoDriver* driver, const std::wstring& path) {
	Pixels pixels;
	const auto native = Utils::ToPathString(path);
	auto* image = driver->createImageFromFile(io::path{ native.data(), static_cast<u32>(native.size()) });
	if(!image)
		return pixels;
	const auto size = image->getDimension();
	pixels.w = static_cast<int>(size.Width);
	pixels.h = static_cast<int>(size.Height);
	pixels.rgba.resize(static_cast<size_t>(pixels.w) * pixels.h * 4);
	for(int y = 0; y < pixels.h; ++y) {
		for(int x = 0; x < pixels.w; ++x) {
			const video::SColor c = image->getPixel(x, y);
			const u32 a = c.getAlpha(); //a see-through picture is laid on black
			uint8_t* p = &pixels.rgba[(static_cast<size_t>(y) * pixels.w + x) * 4];
			p[0] = static_cast<uint8_t>(c.getRed() * a / 255);
			p[1] = static_cast<uint8_t>(c.getGreen() * a / 255);
			p[2] = static_cast<uint8_t>(c.getBlue() * a / 255);
			p[3] = 255;
		}
	}
	image->drop();
	return ShrinkTo(pixels, 2048);
}

video::IImage* MakeImage(video::IVideoDriver* driver, const Pixels& pixels) {
	auto* image = driver->createImage(video::ECF_A8R8G8B8, core::dimension2d<u32>(pixels.w, pixels.h));
	if(!image)
		return nullptr;
	for(int y = 0; y < pixels.h; ++y) {
		for(int x = 0; x < pixels.w; ++x) {
			const uint8_t* p = &pixels.rgba[(static_cast<size_t>(y) * pixels.w + x) * 4];
			image->setPixel(x, y, video::SColor(255, p[0], p[1], p[2]));
		}
	}
	return image;
}

void EnsureLoaded(video::IVideoDriver* driver, Slot& slot) {
	if(slot.loaded)
		return;
	slot.loaded = true;
	if(!slot.path.empty()) {
		std::error_code ec;
		if(std::filesystem::exists(slot.path, ec))
			slot.source = ReadPicture(driver, slot.path);
	}
}

void RenderPreview(video::IVideoDriver* driver, const Pixels* fallback_unused = nullptr) {
	(void)fallback_unused;
	auto& slot = slots[art_ui.current];
	const int pw = slot.sleeve ? 140 : 220;
	const int ph = slot.sleeve ? 204 : 176;
	art_ui.preview->setRelativePosition(core::recti(390, 80, 390 + pw, 80 + ph));
	video::ITexture* texture = nullptr;
	if(!slot.source.Empty()) {
		const Pixels fitted = FitArt(slot.source, pw * 2, ph * 2, slot.params);
		if(auto* image = MakeImage(driver, fitted)) {
			const std::string name = "customize_preview_" + std::to_string(++art_ui.texture_counter);
			texture = driver->addTexture(io::path(name.c_str()), image);
			image->drop();
		}
	}
	art_ui.preview->setImage(texture);
	art_ui.preview->setScaleImage(true);
	if(art_ui.preview_texture)
		driver->removeTexture(art_ui.preview_texture);
	art_ui.preview_texture = texture;
}

void ShowSlotWidgets() {
	auto& slot = slots[art_ui.current];
	art_ui.refreshing = true;
	art_ui.fit->setSelected(slot.params.fit ? 1 : 0);
	art_ui.zoom->setPos(static_cast<s32>(std::lround(slot.params.zoom * 100)));
	art_ui.x->setPos(static_cast<s32>(std::lround(slot.params.ox * 100)));
	art_ui.y->setPos(static_cast<s32>(std::lround(slot.params.oy * 100)));
	art_ui.bright->setPos(static_cast<s32>(std::lround(slot.params.bright * 100)));
	art_ui.border->setPos(static_cast<s32>(std::lround(slot.params.border * 1000)));
	art_ui.border->setVisible(slot.sleeve);
	art_ui.border_label->setVisible(slot.sleeve);
	art_ui.refreshing = false;
	if(slot.path.empty())
		art_ui.file_text->setText(L"No picture chosen: the game's own one is used.");
	else
		art_ui.file_text->setText(std::filesystem::path(slot.path).filename().wstring().c_str());
}

#ifdef _WIN32
std::wstring PickPictureFile() {
	wchar_t buffer[2 * MAX_PATH] = {};
	OPENFILENAMEW dialog{};
	dialog.lStructSize = sizeof(dialog);
	dialog.hwndOwner = GetForegroundWindow();
	dialog.lpstrFilter = L"Pictures\0*.png;*.jpg;*.jpeg;*.bmp;*.tga\0All files\0*.*\0";
	dialog.lpstrFile = buffer;
	dialog.nMaxFile = 2 * MAX_PATH;
	dialog.lpstrTitle = L"Pick a picture";
	dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
	if(!GetOpenFileNameW(&dialog))
		return L"";
	return buffer;
}
#else
std::wstring PickPictureFile() { return L""; }
#endif

//writes the picture cut to the size of the slot into custom_art/ (the game reads it from there)
bool ApplySlot(video::IVideoDriver* driver, Slot& slot) {
	if(slot.source.Empty())
		return false;
	std::error_code ec;
	std::filesystem::create_directories("custom_art", ec);
	const Pixels cut = FitArt(slot.source, slot.width, slot.height, slot.params);
	auto* image = MakeImage(driver, cut);
	if(!image)
		return false;
	const std::string target = std::string("custom_art/") + slot.file;
	const std::string temp = std::string("custom_art/") + (slot.sleeve ? "tmp_sleeve.png" : "tmp_playmat.png");
	const bool written = driver->writeImageToFile(image, io::path(temp.c_str()));
	image->drop();
	if(!written)
		return false;
	std::filesystem::rename(temp, target, ec);
	if(ec) {
		std::filesystem::remove(target, ec);
		std::filesystem::rename(temp, target, ec);
	}
	return !ec;
}

void SetArtNote(const wchar_t* text) {
	art_ui.note->setText(text);
}

//a chosen (or dropped) picture becomes the picture of the selected slot
bool UsePicture(video::IVideoDriver* driver, const std::wstring& path) {
	auto& slot = slots[art_ui.current];
	Pixels picture = ReadPicture(driver, path);
	if(picture.Empty()) {
		SetArtNote(L"That picture could not be opened (png, jpg, bmp and tga work).");
		return false;
	}
	slot.path = path;
	slot.source = std::move(picture);
	slot.loaded = true;
	slot.params = ArtParams{ false, 1.0f, 0.0f, 0.0f, 1.0f, slot.sleeve ? 0.02f : 0.0f };
	ShowSlotWidgets();
	RenderPreview(driver);
	art_ui.dirty = true;
	art_ui.dirty_time = 0; //a new picture is applied at once
	return true;
}

void MarkChanged() {
	if(art_ui.refreshing)
		return;
	art_ui.dirty = true;
	SetArtNote(L"Changes are applied to the game in a moment...");
}
}

void Game::CreateCustomizeWindow() {
	wCustomize = env->addWindow(Scale(130, 50, 890, 570), false, L"Customize");
	wCustomize->getCloseButton()->setVisible(false);
	wCustomize->setVisible(false);
	auto* tabs = env->addTabControl(Scale(8, 28, 752, 470), wCustomize, true, true);
	auto* rules_tab = tabs->addTab(L"Rarity rules");
	auto* art_tab = tabs->addTab(L"Sleeves & playmat");
	env->addButton(Scale(640, 476, 752, 502), wCustomize, BUTTON_CUSTOMIZE_CLOSE, L"Close");

	//asked when the Customize window is closed after a sleeve was changed
	wSleeveRestart = env->addWindow(Scale(300, 200, 720, 350), false, L"New sleeve");
	wSleeveRestart->getCloseButton()->setVisible(false);
	wSleeveRestart->setVisible(false);
	env->addStaticText(L"Your new sleeve is saved. The game reads sleeves when it starts.\n\nRestart now to see it, or keep playing: it will show up the next time you open the game.",
					   Scale(15, 32, 405, 100), false, true, wSleeveRestart);
	env->addButton(Scale(40, 110, 190, 138), wSleeveRestart, BUTTON_SLEEVE_RESTART, L"Restart now");
	env->addButton(Scale(230, 110, 380, 138), wSleeveRestart, BUTTON_SLEEVE_LATER, L"Keep playing");

	//rarity rules: the list of rules on the left, what the selected rule does on the right
	env->addStaticText(L"Your rules (the first one that fits a card wins)", Scale(10, 6, 320, 24), false, false, rules_tab);
	rules_ui.list = env->addListBox(Scale(10, 28, 320, 300), rules_tab, COMBOBOX_RULE_LIST, true);
	env->addButton(Scale(10, 306, 110, 332), rules_tab, BUTTON_RULE_ADD, L"+ Add rule");
	env->addButton(Scale(115, 306, 190, 332), rules_tab, BUTTON_RULE_DELETE, L"Delete");
	env->addButton(Scale(195, 306, 250, 332), rules_tab, BUTTON_RULE_UP, L"Up");
	env->addButton(Scale(255, 306, 320, 332), rules_tab, BUTTON_RULE_DOWN, L"Down");
	env->addStaticText(L"1. Press \"+ Add rule\".\n2. Say what the cards have in common (right side).\n3. Pick the rarity they get.\nEverything saves by itself. A rarity you gave a card yourself always wins over a rule.",
					   Scale(10, 342, 320, 420), false, true, rules_tab);
	env->addStaticText(L"Gives this rarity", Scale(335, 6, 445, 26), false, false, rules_tab);
	rules_ui.rarity = env->addComboBox(Scale(450, 4, 735, 26), rules_tab, COMBOBOX_RULE_RARITY);
	env->addStaticText(L"Archetype", Scale(335, 36, 420, 54), false, false, rules_tab);
	rules_ui.arch_filter = env->addEditBox(L"", Scale(425, 32, 735, 54), true, rules_tab, EDITBOX_RULE_ARCH_FILTER);
	rules_ui.arch = env->addListBox(Scale(335, 58, 735, 172), rules_tab, COMBOBOX_RULE_ARCH, true);
	env->addStaticText(L"Type in the box above to search the list, then click an archetype.", Scale(335, 174, 735, 190), false, false, rules_tab);
	env->addStaticText(L"Card is a", Scale(335, 200, 405, 220), false, false, rules_tab);
	rules_ui.kind = env->addComboBox(Scale(410, 198, 520, 220), rules_tab, COMBOBOX_RULE_KIND);
	env->addStaticText(L"Subtype", Scale(530, 200, 590, 220), false, false, rules_tab);
	rules_ui.subtype = env->addComboBox(Scale(595, 198, 735, 220), rules_tab, COMBOBOX_RULE_TYPE);
	env->addStaticText(L"Attribute", Scale(335, 230, 405, 250), false, false, rules_tab);
	rules_ui.attr = env->addComboBox(Scale(410, 228, 520, 250), rules_tab, COMBOBOX_RULE_ATTR);
	env->addStaticText(L"Race", Scale(530, 230, 590, 250), false, false, rules_tab);
	rules_ui.race = env->addComboBox(Scale(595, 228, 735, 250), rules_tab, COMBOBOX_RULE_RACE);
	env->addStaticText(L"Level/Rank", Scale(335, 260, 405, 280), false, false, rules_tab);
	rules_ui.level_min = env->addEditBox(L"0", Scale(410, 258, 445, 280), true, rules_tab, EDITBOX_RULE_LEVEL_MIN);
	env->addStaticText(L"to", Scale(452, 260, 470, 280), false, false, rules_tab);
	rules_ui.level_max = env->addEditBox(L"0", Scale(475, 258, 510, 280), true, rules_tab, EDITBOX_RULE_LEVEL_MAX);
	env->addStaticText(L"(0 = any)", Scale(515, 260, 575, 280), false, false, rules_tab);
	env->addStaticText(L"Name has", Scale(580, 260, 640, 280), false, false, rules_tab);
	rules_ui.name = env->addEditBox(L"", Scale(640, 258, 735, 280), true, rules_tab, EDITBOX_RULE_NAME);
	rules_ui.result = env->addStaticText(L"", Scale(335, 294, 735, 400), false, true, rules_tab);

	//sleeves and playmat
	env->addStaticText(L"Picture for", Scale(10, 12, 90, 32), false, false, art_tab);
	art_ui.slot = env->addComboBox(Scale(95, 10, 300, 32), art_tab, COMBOBOX_ART_SLOT);
	for(const auto& slot : slots)
		art_ui.slot->addItem(slot.title);
	env->addButton(Scale(10, 44, 140, 68), art_tab, BUTTON_ART_PICK, L"Choose picture...");
	env->addButton(Scale(145, 44, 270, 68), art_tab, BUTTON_ART_REMOVE, L"Use the default");
	art_ui.file_text = env->addStaticText(L"", Scale(280, 48, 610, 68), false, false, art_tab);
	env->addStaticText(L"Shape", Scale(10, 82, 100, 102), false, false, art_tab);
	art_ui.fit = env->addComboBox(Scale(105, 80, 340, 102), art_tab, COMBOBOX_ART_FIT);
	art_ui.fit->addItem(L"Fill the area (crops the edges)");
	art_ui.fit->addItem(L"Show the whole picture");
	const auto slider = [&](const wchar_t* label, int y, int id, int low, int high) {
		env->addStaticText(label, Scale(10, y, 105, y + 18), false, false, art_tab);
		auto* bar = env->addScrollBar(true, Scale(110, y + 1, 340, y + 17), art_tab, id);
		bar->setMin(low);
		bar->setMax(high);
		bar->setSmallStep(1);
		bar->setLargeStep(10);
		return bar;
	};
	art_ui.zoom = slider(L"Zoom", 116, SCROLL_ART_ZOOM, 50, 400);
	art_ui.x = slider(L"Move sideways", 144, SCROLL_ART_X, -100, 100);
	art_ui.y = slider(L"Move up / down", 172, SCROLL_ART_Y, -100, 100);
	art_ui.bright = slider(L"Brightness", 200, SCROLL_ART_BRIGHT, 30, 150);
	art_ui.border = slider(L"Black border", 228, SCROLL_ART_BORDER, 0, 60);
	art_ui.border_label = env->addStaticText(L"", Scale(0, 0, 0, 0), false, false, art_tab);
	art_ui.preview = env->addImage(Scale(390, 80, 530, 284), art_tab, -1, nullptr, false);
	art_ui.preview->setScaleImage(true);
	art_ui.note = env->addStaticText(L"Pick a picture: it is cut to the right shape for you. You can also drag a picture file onto the game window. Sleeves are read when the game starts, the playmat shows up in the next duel. (png, jpg, bmp, tga)",
									 Scale(10, 262, 380, 330), false, true, art_tab);
	art_ui.slot->setSelected(0);
}

void Game::OpenCustomizeWindow() {
	art_ui.sleeve_changed = false;
	if(!rules_ui.set_names_loaded) {
		rules_ui.set_names_loaded = true;
		rules_ui.set_names = gDataManager->GetSetNames();
		std::sort(rules_ui.set_names.begin(), rules_ui.set_names.end(), [](const auto& a, const auto& b) { return Lower(a.second) < Lower(b.second); });
	}
	rules_ui.selected = RarityFx::Rules().empty() ? -1 : 0;
	rules_ui.refreshing = true;
	rules_ui.arch_filter->setText(L"");
	rules_ui.refreshing = false;
	RefillRuleList();
	LoadRuleEditor();
	LoadSettings();
	art_ui.current = std::clamp(art_ui.slot->getSelected(), 0, 2);
	EnsureLoaded(driver, slots[art_ui.current]);
	ShowSlotWidgets();
	RenderPreview(driver);
	PopupElement(wCustomize);
}

//called every frame: applies the changes of the sleeve / playmat controls a moment after the last one
void Game::CustomizeTick() {
	if(!art_ui.dirty)
		return;
	const uint32_t now = device->getTimer()->getTime();
	if(now - art_ui.dirty_time < 600)
		return;
	art_ui.dirty = false;
	auto& slot = slots[art_ui.current];
	SaveSettings();
	if(slot.source.Empty())
		return;
	if(ApplySlot(driver, slot)) {
		if(slot.sleeve)
			art_ui.sleeve_changed = true;
		SetArtNote(slot.sleeve ? L"Applied. When you close this window you can restart to see the new sleeve." : L"Applied. The playmat shows up in the next duel (or right now, if one is on).");
	} else
		SetArtNote(L"The picture could not be saved.");
}

//a picture file dropped on the game window while the Customize window is open
bool Game::UseDroppedPicture(const std::wstring& path) {
	if(!wCustomize || !wCustomize->isVisible())
		return false;
	std::wstring extension = Lower(std::filesystem::path(path).extension().wstring());
	if(extension != L".png" && extension != L".jpg" && extension != L".jpeg" && extension != L".bmp" && extension != L".tga")
		return false;
	UsePicture(driver, path);
	return true;
}

bool Game::OnCustomizeEvent(const irr::SEvent& event) {
	if(event.EventType != irr::EET_GUI_EVENT)
		return false;
	const int id = event.GUIEvent.Caller->getID();
	if(id < BUTTON_CUSTOMIZE || id > SCROLL_ART_BORDER)
		return false;
	auto& slot = slots[art_ui.current];
	switch(event.GUIEvent.EventType) {
	case irr::gui::EGET_BUTTON_CLICKED: {
		auto rules = RarityFx::Rules();
		switch(id) {
		case BUTTON_CUSTOMIZE:
			OpenCustomizeWindow();
			return true;
		case BUTTON_CUSTOMIZE_CLOSE:
			if(art_ui.dirty) {
				art_ui.dirty_time = 0;
				CustomizeTick();
			}
			wCustomize->setVisible(false);
			if(art_ui.sleeve_changed) {
				art_ui.sleeve_changed = false;
				PopupElement(wSleeveRestart);
			}
			return true;
		case BUTTON_SLEEVE_RESTART:
			wSleeveRestart->setVisible(false);
			restart = true;
			return true;
		case BUTTON_SLEEVE_LATER:
			wSleeveRestart->setVisible(false);
			return true;
		case BUTTON_RULE_ADD:
			if(rules.size() < 100) {
				RarityRule rule;
				for(int i = 1; i < static_cast<int>(Rarity::COUNT); ++i) {
					if(RarityFx::Defined(static_cast<Rarity>(i))) {
						rule.rarity = RarityFx::Id(static_cast<Rarity>(i));
						break;
					}
				}
				rules.push_back(rule);
				rules_ui.selected = static_cast<int>(rules.size()) - 1;
				RarityFx::SetRules(std::move(rules));
				RefillRuleList();
				LoadRuleEditor();
			}
			return true;
		case BUTTON_RULE_DELETE:
			if(rules_ui.selected >= 0 && rules_ui.selected < static_cast<int>(rules.size())) {
				rules.erase(rules.begin() + rules_ui.selected);
				rules_ui.selected = std::min(rules_ui.selected, static_cast<int>(rules.size()) - 1);
				RarityFx::SetRules(std::move(rules));
				RefillRuleList();
				LoadRuleEditor();
			}
			return true;
		case BUTTON_RULE_UP:
		case BUTTON_RULE_DOWN: {
			const int to = rules_ui.selected + (id == BUTTON_RULE_UP ? -1 : 1);
			if(rules_ui.selected >= 0 && to >= 0 && to < static_cast<int>(rules.size())) {
				std::swap(rules[rules_ui.selected], rules[to]);
				rules_ui.selected = to;
				RarityFx::SetRules(std::move(rules));
				RefillRuleList();
				ShowRuleResult();
			}
			return true;
		}
		case BUTTON_ART_PICK: {
			const std::wstring path = PickPictureFile();
			if(path.empty())
				SetArtNote(L"No picture was picked. If no file window opened, drag a picture file onto the game window instead.");
			else
				UsePicture(driver, path);
			return true;
		}
		case BUTTON_ART_REMOVE: {
			std::error_code ec;
			std::filesystem::remove(std::string("custom_art/") + slot.file, ec);
			slot.path.clear();
			slot.source = Pixels{};
			art_ui.dirty = false;
			SaveSettings();
			ShowSlotWidgets();
			RenderPreview(driver);
			if(slot.sleeve)
				art_ui.sleeve_changed = true;
			SetArtNote(L"The game's own picture is used again (sleeves: after a restart).");
			return true;
		}
		default:
			return true;
		}
	}
	case irr::gui::EGET_COMBO_BOX_CHANGED:
		if(rules_ui.refreshing && id != COMBOBOX_ART_SLOT && id != COMBOBOX_ART_FIT)
			return true;
		switch(id) {
		case COMBOBOX_RULE_RARITY:
		case COMBOBOX_RULE_KIND:
		case COMBOBOX_RULE_TYPE:
		case COMBOBOX_RULE_ATTR:
		case COMBOBOX_RULE_RACE:
			ApplyRuleEdits();
			return true;
		case COMBOBOX_ART_SLOT:
			art_ui.current = std::clamp(art_ui.slot->getSelected(), 0, 2);
			EnsureLoaded(driver, slots[art_ui.current]);
			ShowSlotWidgets();
			RenderPreview(driver);
			return true;
		case COMBOBOX_ART_FIT:
			if(!art_ui.refreshing) {
				slot.params.fit = art_ui.fit->getSelected() == 1;
				RenderPreview(driver);
				MarkChanged();
				art_ui.dirty_time = device->getTimer()->getTime();
			}
			return true;
		default:
			return true;
		}
	case irr::gui::EGET_LISTBOX_CHANGED:
	case irr::gui::EGET_LISTBOX_SELECTED_AGAIN:
		if(rules_ui.refreshing)
			return true;
		if(id == COMBOBOX_RULE_LIST) {
			if(rules_ui.list->getSelected() >= 0 && rules_ui.list->getSelected() != rules_ui.selected) {
				rules_ui.selected = rules_ui.list->getSelected();
				LoadRuleEditor();
			}
		} else if(id == COMBOBOX_RULE_ARCH) {
			ApplyRuleEdits();
			//the list now shows the chosen archetype on top: show that
			const auto& rules = RarityFx::Rules();
			if(rules_ui.selected >= 0 && rules_ui.selected < static_cast<int>(rules.size())) {
				rules_ui.refreshing = true;
				FillArchList(rules[rules_ui.selected].setcode);
				rules_ui.refreshing = false;
			}
		}
		return true;
	case irr::gui::EGET_EDITBOX_CHANGED:
		if(id == EDITBOX_RULE_ARCH_FILTER) {
			if(!rules_ui.refreshing) {
				const auto& rules = RarityFx::Rules();
				FillArchList(rules_ui.selected >= 0 && rules_ui.selected < static_cast<int>(rules.size()) ? rules[rules_ui.selected].setcode : 0);
			}
			return true;
		}
		if(id == EDITBOX_RULE_LEVEL_MIN || id == EDITBOX_RULE_LEVEL_MAX || id == EDITBOX_RULE_NAME)
			ApplyRuleEdits();
		return true;
	case irr::gui::EGET_SCROLL_BAR_CHANGED: {
		if(art_ui.refreshing)
			return true;
		auto* bar = static_cast<irr::gui::IGUIScrollBar*>(event.GUIEvent.Caller);
		const float v = bar->getPos();
		switch(id) {
		case SCROLL_ART_ZOOM: slot.params.zoom = v / 100.0f; break;
		case SCROLL_ART_X: slot.params.ox = v / 100.0f; break;
		case SCROLL_ART_Y: slot.params.oy = v / 100.0f; break;
		case SCROLL_ART_BRIGHT: slot.params.bright = v / 100.0f; break;
		case SCROLL_ART_BORDER: slot.params.border = v / 1000.0f; break;
		default: return true;
		}
		RenderPreview(driver);
		MarkChanged();
		art_ui.dirty_time = device->getTimer()->getTime();
		return true;
	}
	default:
		return true;
	}
}

}
