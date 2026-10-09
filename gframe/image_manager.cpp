#include <cstdio>
#include <IFileSystem.h>
extern "C" {
#include <jpeglib.h>
}
#include <csetjmp>
#include <shared_mutex>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#endif
#include "game_config.h"
#include "utils.h"
#include <IImage.h>
#include <IGUIImage.h>
#include <IVideoDriver.h>
#include <IrrlichtDevice.h>
#include <IReadFile.h>
#include "logging.h"
#include "image_manager.h"
#include "image_downloader.h"
#include "game.h"
#include "config.h"
#include "fmt.h"

#define BASE_PATH EPRO_TEXT("./textures/")

namespace ygo {

#define ASSERT_TEXTURE_LOADED(what, name) do { if(!what) { throw std::runtime_error("Couldn't load texture: " name); }} while(0)
#define ASSIGN_DEFAULT(obj) do { def_##obj=obj; } while(0)

namespace {
bool hasNPotSupport(irr::video::IVideoDriver* driver) {
	static const auto supported = [driver] {
		return driver->queryFeature(irr::video::EVDF_TEXTURE_NPOT);
	}();
	return supported;
}
// Compute next-higher power of 2 efficiently, e.g. for power-of-2 texture sizes.
// Public Domain: https://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2
inline irr::u32 npot2(irr::u32 orig) {
	orig--;
	orig |= orig >> 1;
	orig |= orig >> 2;
	orig |= orig >> 4;
	orig |= orig >> 8;
	orig |= orig >> 16;
	return orig + 1;
}
irr::s32 toPow2(irr::video::IVideoDriver* driver, irr::s32 size) {
	if(!hasNPotSupport(driver))
		return npot2(size);
	return size;
}
}

ImageManager::ImageManager() {
	//reading the picture cache takes a fraction of a second, do it while the rest of the game is loading
	card_cache_preload_thread = std::thread([this]() { std::call_once(card_cache_loaded, [this]() { LoadCardCachePack(); }); });
	stop_threads = false;
	obj_clear_thread = epro::thread(&ImageManager::ClearFutureObjects, this);
	//use most of the cores, but leave some for the game itself: more loader threads than that only slow the main thread down
	const int load_thread_count = std::clamp<int>(static_cast<int>(std::thread::hardware_concurrency()) - 2, 2, 12);
	load_threads.reserve(load_thread_count);
	for(int i = 0; i < load_thread_count; ++i)
		load_threads.emplace_back(&ImageManager::LoadPic, this);
}
ImageManager::~ImageManager() {
	if(card_cache_preload_thread.joinable())
		card_cache_preload_thread.join();
	FlushCardCache();
	stop_threads = true;
	obj_clear_lock.lock();
	cv_clear.notify_all();
	obj_clear_lock.unlock();
	obj_clear_thread.join();
	pic_load.lock();
	cv_load.notify_all();
	pic_load.unlock();
	for(auto& thread : load_threads)
		thread.join();
	for(auto& it : g_imgCache) {
		if(it.second)
			it.second->drop();
	}
	for(auto& it : g_txrCache) {
		if(it.second)
			driver->removeTexture(it.second);
	}
}
irr::video::ITexture* ImageManager::loadTextureFixedSize(epro::path_stringview texture_name, int width, int height) {
	width = mainGame->Scale(width);
	height = mainGame->Scale(height);
	irr::video::ITexture* ret = GetTextureFromFile(epro::format(EPRO_TEXT("{}{}.png"), textures_path, texture_name).data(), width, height);
	if(ret == nullptr)
		ret = GetTextureFromFile(epro::format(EPRO_TEXT("{}{}.jpg"), textures_path, texture_name).data(), width, height);
	return ret;
}
irr::video::ITexture* ImageManager::loadTextureAnySize(epro::path_stringview texture_name) {
	irr::video::ITexture* ret = driver->getTexture(epro::format(EPRO_TEXT("{}{}.png"), textures_path, texture_name).data());
	if(ret == nullptr)
		ret = driver->getTexture(epro::format(EPRO_TEXT("{}{}.jpg"), textures_path, texture_name).data());
	return ret;
}
bool ImageManager::Initial() {
	timestamp_id = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	textures_path = BASE_PATH;

	tCover[0] = loadTextureFixedSize(EPRO_TEXT("cover"sv), CARD_IMG_WIDTH, CARD_IMG_HEIGHT);
	ASSERT_TEXTURE_LOADED(tCover[0], "cover");

	tCover[1] = loadTextureFixedSize(EPRO_TEXT("cover2"sv), CARD_IMG_WIDTH, CARD_IMG_HEIGHT);
	if(!tCover[1])
		tCover[1] = tCover[0];

	tUnknown = loadTextureFixedSize(EPRO_TEXT("unknown"sv), CARD_IMG_WIDTH, CARD_IMG_HEIGHT);
	ASSERT_TEXTURE_LOADED(tUnknown, "unknown");

	tAct = loadTextureAnySize(EPRO_TEXT("act"sv));
	ASSERT_TEXTURE_LOADED(tAct, "act");
	ASSIGN_DEFAULT(tAct);

	tAttack = loadTextureAnySize(EPRO_TEXT("attack"sv));
	ASSERT_TEXTURE_LOADED(tAttack, "attack");
	ASSIGN_DEFAULT(tAttack);

	tChain = loadTextureAnySize(EPRO_TEXT("chain"sv));
	ASSERT_TEXTURE_LOADED(tChain, "chain");
	ASSIGN_DEFAULT(tChain);

	tNegated = loadTextureFixedSize(EPRO_TEXT("negated"sv), 128, 128);
	ASSERT_TEXTURE_LOADED(tNegated, "negated");
	ASSIGN_DEFAULT(tNegated);

	tNumber = loadTextureFixedSize(EPRO_TEXT("number"sv), 320, 256);
	ASSERT_TEXTURE_LOADED(tNumber, "number");
	ASSIGN_DEFAULT(tNumber);

	tLPBar = loadTextureAnySize(EPRO_TEXT("lp"sv));
	ASSERT_TEXTURE_LOADED(tLPBar, "lp");
	ASSIGN_DEFAULT(tLPBar);

	tLPFrame = loadTextureAnySize(EPRO_TEXT("lpf"sv));
	ASSERT_TEXTURE_LOADED(tLPFrame, "lpf");
	ASSIGN_DEFAULT(tLPFrame);

	tMask = loadTextureFixedSize(EPRO_TEXT("mask"sv), 254, 254);
	ASSERT_TEXTURE_LOADED(tMask, "mask");
	ASSIGN_DEFAULT(tMask);

	tEquip = loadTextureAnySize(EPRO_TEXT("equip"sv));
	ASSERT_TEXTURE_LOADED(tEquip, "equip");
	ASSIGN_DEFAULT(tEquip);

	tTarget = loadTextureAnySize(EPRO_TEXT("target"sv));
	ASSERT_TEXTURE_LOADED(tTarget, "target");
	ASSIGN_DEFAULT(tTarget);

	tChainTarget = loadTextureAnySize(EPRO_TEXT("chaintarget"sv));
	ASSERT_TEXTURE_LOADED(tChainTarget, "chaintarget");
	ASSIGN_DEFAULT(tChainTarget);

	tLim = loadTextureAnySize(EPRO_TEXT("lim"sv));
	ASSERT_TEXTURE_LOADED(tLim, "lim");
	ASSIGN_DEFAULT(tLim);

	tOT = loadTextureAnySize(EPRO_TEXT("ot"sv));
	ASSERT_TEXTURE_LOADED(tOT, "ot");
	ASSIGN_DEFAULT(tOT);

	tHand[0] = loadTextureFixedSize(EPRO_TEXT("f1"sv), 89, 128);
	ASSERT_TEXTURE_LOADED(tHand[0], "f1");
	ASSIGN_DEFAULT(tHand[0]);

	tHand[1] = loadTextureFixedSize(EPRO_TEXT("f2"sv), 89, 128);
	ASSERT_TEXTURE_LOADED(tHand[1], "f2");
	ASSIGN_DEFAULT(tHand[1]);

	tHand[2] = loadTextureFixedSize(EPRO_TEXT("f3"sv), 89, 128);
	ASSERT_TEXTURE_LOADED(tHand[2], "f3");
	ASSIGN_DEFAULT(tHand[2]);

	tBackGround = loadTextureAnySize(EPRO_TEXT("bg"sv));
	ASSERT_TEXTURE_LOADED(tBackGround, "bg");
	ASSIGN_DEFAULT(tBackGround);

	tBackGround_menu = loadTextureAnySize(EPRO_TEXT("bg_menu"sv));
	ASSIGN_DEFAULT(tBackGround_menu);

	tBackGround_deck = loadTextureAnySize(EPRO_TEXT("bg_deck"sv));
	ASSIGN_DEFAULT(tBackGround_deck);

	tBackGround_duel_topdown = loadTextureAnySize(EPRO_TEXT("bg_duel_topdown"sv));
	ASSIGN_DEFAULT(tBackGround_duel_topdown);

	tField[0][0] = loadTextureAnySize(EPRO_TEXT("field2"sv));
	ASSERT_TEXTURE_LOADED(tField[0][0], "field2");
	ASSIGN_DEFAULT(tField[0][0]);

	tFieldTransparent[0][0] = loadTextureAnySize(EPRO_TEXT("field-transparent2"sv));
	ASSERT_TEXTURE_LOADED(tFieldTransparent[0][0], "field-transparent2");
	ASSIGN_DEFAULT(tFieldTransparent[0][0]);

	tField[0][1] = loadTextureAnySize(EPRO_TEXT("field3"sv));
	ASSERT_TEXTURE_LOADED(tField[0][1], "field3");
	ASSIGN_DEFAULT(tField[0][1]);

	tFieldTransparent[0][1] = loadTextureAnySize(EPRO_TEXT("field-transparent3"sv));
	ASSERT_TEXTURE_LOADED(tFieldTransparent[0][1], "field-transparent3");
	ASSIGN_DEFAULT(tFieldTransparent[0][1]);

	tField[0][2] = loadTextureAnySize(EPRO_TEXT("field"sv));
	ASSERT_TEXTURE_LOADED(tField[0][2], "field");
	ASSIGN_DEFAULT(tField[0][2]);

	tFieldTransparent[0][2] = loadTextureAnySize(EPRO_TEXT("field-transparent"sv));
	ASSERT_TEXTURE_LOADED(tFieldTransparent[0][2], "field-transparent");
	ASSIGN_DEFAULT(tFieldTransparent[0][2]);

	tField[0][3] = loadTextureAnySize(EPRO_TEXT("field4"sv));
	ASSERT_TEXTURE_LOADED(tField[0][3], "field4");
	ASSIGN_DEFAULT(tField[0][3]);

	tFieldTransparent[0][3] = loadTextureAnySize(EPRO_TEXT("field-transparent4"sv));
	ASSERT_TEXTURE_LOADED(tFieldTransparent[0][3], "field-transparent4");
	ASSIGN_DEFAULT(tFieldTransparent[0][3]);

	tField[1][0] = loadTextureAnySize(EPRO_TEXT("fieldSP2"sv));
	ASSERT_TEXTURE_LOADED(tField[1][0], "fieldSP2");
	ASSIGN_DEFAULT(tField[1][0]);

	tFieldTransparent[1][0] = loadTextureAnySize(EPRO_TEXT("field-transparentSP2"sv));
	ASSERT_TEXTURE_LOADED(tFieldTransparent[1][0], "field-transparentSP2");
	ASSIGN_DEFAULT(tFieldTransparent[1][0]);

	tField[1][1] = loadTextureAnySize(EPRO_TEXT("fieldSP3"sv));
	ASSERT_TEXTURE_LOADED(tField[1][1], "fieldSP3");
	ASSIGN_DEFAULT(tField[1][1]);

	tFieldTransparent[1][1] = loadTextureAnySize(EPRO_TEXT("field-transparentSP3"sv));
	ASSERT_TEXTURE_LOADED(tFieldTransparent[1][1], "field-transparentSP3");
	ASSIGN_DEFAULT(tFieldTransparent[1][1]);

	tField[1][2] = loadTextureAnySize(EPRO_TEXT("fieldSP"sv));
	ASSERT_TEXTURE_LOADED(tField[1][2], "fieldSP");
	ASSIGN_DEFAULT(tField[1][2]);

	tFieldTransparent[1][2] = loadTextureAnySize(EPRO_TEXT("field-transparentSP"sv));
	ASSERT_TEXTURE_LOADED(tFieldTransparent[1][2], "field-transparentSP");
	ASSIGN_DEFAULT(tFieldTransparent[1][2]);

	tField[1][3] = loadTextureAnySize(EPRO_TEXT("fieldSP4"sv));
	ASSERT_TEXTURE_LOADED(tField[1][3], "fieldSP4");
	ASSIGN_DEFAULT(tField[1][3]);

	tFieldTransparent[1][3] = loadTextureAnySize(EPRO_TEXT("field-transparentSP4"sv));
	ASSERT_TEXTURE_LOADED(tFieldTransparent[1][3], "field-transparentSP4");
	ASSIGN_DEFAULT(tFieldTransparent[1][3]);

	tSettings = loadTextureAnySize(EPRO_TEXT("settings"sv));
	ASSERT_TEXTURE_LOADED(tSettings, "settings");
	ASSIGN_DEFAULT(tSettings);

	// Not required to be present
	tCheckBox[0] = loadTextureAnySize(EPRO_TEXT("checkbox_16"sv));
	ASSIGN_DEFAULT(tCheckBox[0]);

	tCheckBox[1] = loadTextureAnySize(EPRO_TEXT("checkbox_32"sv));
	ASSIGN_DEFAULT(tCheckBox[1]);

	tCheckBox[2] = loadTextureAnySize(EPRO_TEXT("checkbox_64"sv));
	ASSIGN_DEFAULT(tCheckBox[2]);


	sizes[0].first = sizes[1].first = toPow2(driver, CARD_IMG_WIDTH * gGameConfig->dpi_scale);
	sizes[0].second = sizes[1].second = toPow2(driver, CARD_IMG_HEIGHT * gGameConfig->dpi_scale);
	sizes[2].first = toPow2(driver, CARD_THUMB_WIDTH * gGameConfig->dpi_scale);
	sizes[2].second = toPow2(driver, CARD_THUMB_HEIGHT * gGameConfig->dpi_scale);
	return true;
}
void ImageManager::replaceTextureLoadingFixedSize(irr::video::ITexture*& texture, irr::video::ITexture* fallback, epro::path_stringview texture_name, int width, int height) {
	auto* tmp = loadTextureFixedSize(texture_name, width, height);
	if(!tmp)
		tmp = fallback;
	if(texture != fallback)
		driver->removeTexture(texture);
	texture = tmp;
}
void ImageManager::replaceTextureLoadingAnySize(irr::video::ITexture*& texture, irr::video::ITexture* fallback, epro::path_stringview texture_name) {
	auto* tmp = loadTextureAnySize(texture_name);
	if(!tmp)
		tmp = fallback;
	if(texture && texture != fallback)
		driver->removeTexture(texture);
	texture = tmp;
}
#define REPLACE_TEXTURE_WITH_FIXED_SIZE(obj,name,w,h) replaceTextureLoadingFixedSize(obj, def_##obj, EPRO_TEXT(name) ""sv, w, h)
#define REPLACE_TEXTURE_ANY_SIZE(obj,name) replaceTextureLoadingAnySize(obj, def_##obj, EPRO_TEXT(name) ""sv)

void ImageManager::ChangeTextures(epro::path_stringview _path) {
	if(_path == textures_path)
		return;
	textures_path.assign(_path.data(), _path.size());
	const bool is_base = textures_path == BASE_PATH;
	REPLACE_TEXTURE_ANY_SIZE(tAct, "act");
	REPLACE_TEXTURE_ANY_SIZE(tAttack, "attack");
	REPLACE_TEXTURE_ANY_SIZE(tChain, "chain");
	REPLACE_TEXTURE_WITH_FIXED_SIZE(tNegated, "negated", 128, 128);
	REPLACE_TEXTURE_WITH_FIXED_SIZE(tNumber, "number", 320, 256);
	REPLACE_TEXTURE_ANY_SIZE(tLPBar, "lp");
	REPLACE_TEXTURE_ANY_SIZE(tLPFrame, "lpf");
	REPLACE_TEXTURE_WITH_FIXED_SIZE(tMask, "mask", 254, 254);
	REPLACE_TEXTURE_ANY_SIZE(tEquip, "equip");
	REPLACE_TEXTURE_ANY_SIZE(tTarget, "target");
	REPLACE_TEXTURE_ANY_SIZE(tChainTarget, "chaintarget");
	REPLACE_TEXTURE_ANY_SIZE(tLim, "lim");
	REPLACE_TEXTURE_ANY_SIZE(tOT, "ot");
	REPLACE_TEXTURE_WITH_FIXED_SIZE(tHand[0], "f1", 89, 128);
	REPLACE_TEXTURE_WITH_FIXED_SIZE(tHand[1], "f2", 89, 128);
	REPLACE_TEXTURE_WITH_FIXED_SIZE(tHand[2], "f3", 89, 128);
	REPLACE_TEXTURE_ANY_SIZE(tBackGround, "bg");
	REPLACE_TEXTURE_ANY_SIZE(tBackGround_menu, "bg_menu");
	REPLACE_TEXTURE_ANY_SIZE(tBackGround_deck, "bg_deck");
	REPLACE_TEXTURE_ANY_SIZE(tBackGround_duel_topdown, "bg_duel_topdown");
	REPLACE_TEXTURE_ANY_SIZE(tField[0][0], "field2");
	REPLACE_TEXTURE_ANY_SIZE(tFieldTransparent[0][0], "field-transparent2");
	REPLACE_TEXTURE_ANY_SIZE(tField[0][1], "field3");
	REPLACE_TEXTURE_ANY_SIZE(tFieldTransparent[0][1], "field-transparent3");
	REPLACE_TEXTURE_ANY_SIZE(tField[0][2], "field");
	REPLACE_TEXTURE_ANY_SIZE(tFieldTransparent[0][2], "field-transparent");
	REPLACE_TEXTURE_ANY_SIZE(tField[0][3], "field4");
	REPLACE_TEXTURE_ANY_SIZE(tFieldTransparent[0][3], "field-transparent4");
	REPLACE_TEXTURE_ANY_SIZE(tField[1][0], "fieldSP2");
	REPLACE_TEXTURE_ANY_SIZE(tFieldTransparent[1][0], "field-transparentSP2");
	REPLACE_TEXTURE_ANY_SIZE(tField[1][1], "fieldSP3");
	REPLACE_TEXTURE_ANY_SIZE(tFieldTransparent[1][1], "field-transparentSP3");
	REPLACE_TEXTURE_ANY_SIZE(tField[1][2], "fieldSP");
	REPLACE_TEXTURE_ANY_SIZE(tFieldTransparent[1][2], "field-transparentSP");
	REPLACE_TEXTURE_ANY_SIZE(tField[1][3], "fieldSP4");
	REPLACE_TEXTURE_ANY_SIZE(tFieldTransparent[1][3], "field-transparentSP4");
	REPLACE_TEXTURE_ANY_SIZE(tSettings, "settings");
	REPLACE_TEXTURE_ANY_SIZE(tCheckBox[0], "checkbox_16");
	REPLACE_TEXTURE_ANY_SIZE(tCheckBox[1], "checkbox_32");
	REPLACE_TEXTURE_ANY_SIZE(tCheckBox[2], "checkbox_64");
	RefreshCovers();
}
#undef REPLACE_TEXTURE_ANY_SIZE
#undef REPLACE_TEXTURE_WITH_FIXED_SIZE
void ImageManager::ResetTextures() {
	ChangeTextures(BASE_PATH);
}
void ImageManager::SetDevice(irr::IrrlichtDevice* dev) {
	device = dev;
	driver = dev->getVideoDriver();
}
void ImageManager::ClearTexture(bool resize) {
	cache_generation++;
	auto ClearMap = [&](texture_map &map) {
		for(const auto& tit : map) {
			if(tit.second.texture) {
				driver->removeTexture(tit.second.texture);
			}
		}
		map.clear();
	};
	if(resize) {
		if(large_override_width > 0) {
			sizes[1].first = large_override_width;
			sizes[1].second = large_override_height;
		} else {
			const auto card_sizes = mainGame->imgCard->getRelativePosition().getSize();
			sizes[1].first = toPow2(driver, card_sizes.Width);
			sizes[1].second = toPow2(driver, card_sizes.Height);
		}
		sizes[2].first = toPow2(driver, CARD_THUMB_WIDTH * mainGame->window_scale.X * gGameConfig->dpi_scale);
		sizes[2].second = toPow2(driver, CARD_THUMB_HEIGHT * mainGame->window_scale.Y * gGameConfig->dpi_scale);
		RefreshCovers();
	} else
		ClearCachedTextures();
	ClearMap(tMap[0]);
	ClearMap(tMap[1]);
	ClearMap(tThumb);
	ClearMap(tCovers);
	for(const auto& tit : tFields) {
		if(tit.second) {
			driver->removeTexture(tit.second);
		}
	}
	tFields.clear();
}
void ImageManager::SetCardTextureSize(int width, int height) {
	if(width <= 0 || height <= 0) {
		width = toPow2(driver, CARD_IMG_WIDTH * gGameConfig->dpi_scale);
		height = toPow2(driver, CARD_IMG_HEIGHT * gGameConfig->dpi_scale);
	}
	if(sizes[0].first == width && sizes[0].second == height)
		return;
	sizes[0].first = width;
	sizes[0].second = height;
	ClearTexture(false);
}
void ImageManager::SetLargeCardTextureSize(int width, int height) {
	large_override_width = width;
	large_override_height = height;
	if(width > 0) {
		sizes[1].first = width;
		sizes[1].second = height;
	} else {
		const auto card_sizes = mainGame->imgCard->getRelativePosition().getSize();
		sizes[1].first = toPow2(driver, card_sizes.Width);
		sizes[1].second = toPow2(driver, card_sizes.Height);
	}
	//the large pictures (and the card backs, which use the same size) are reloaded at the new size
	for(auto* map : { &tMap[1], &tCovers }) {
		for(const auto& entry : *map) {
			if(entry.second.texture)
				driver->removeTexture(entry.second.texture);
		}
		map->clear();
	}
}
void ImageManager::ClearTexturesKeepCardArt() {
	//the small deck editor pictures keep their size, so they stay
	for(auto* map : { &tMap[1] }) {
		for(const auto& entry : *map) {
			if(entry.second.texture)
				driver->removeTexture(entry.second.texture);
		}
		map->clear();
	}
}
double ImageManager::ProcessAgeMs() {
#ifdef _WIN32
	FILETIME creation, exit_time, kernel, user, now;
	if(!GetProcessTimes(GetCurrentProcess(), &creation, &exit_time, &kernel, &user))
		return 0.0;
	GetSystemTimeAsFileTime(&now);
	ULARGE_INTEGER c, n;
	c.LowPart = creation.dwLowDateTime;
	c.HighPart = creation.dwHighDateTime;
	n.LowPart = now.dwLowDateTime;
	n.HighPart = now.dwHighDateTime;
	return static_cast<double>(n.QuadPart - c.QuadPart) / 10000.0;
#else
	return 0.0;
#endif
}
void ImageManager::WritePreloadProfile() {
	if(profile.reported)
		return;
	profile.reported = true;
	std::ofstream out("preload_profile.txt");
	const auto ms = [](const std::atomic<uint64_t>& v) { return v.load() / 1e6; };
	const double wall = std::chrono::duration<double, std::milli>(profile.last_texture - profile.start).count();
	out << "process age when the preload started: " << profile.start_age_ms << " ms (time the game needed to get to the main menu)" << std::endl;
	out << "wall time until the last texture was created: " << wall << " ms" << std::endl;
	out << "load threads: " << load_threads.size() << ", hardware threads: " << std::thread::hardware_concurrency() << std::endl;
	out << "textures created (grid size): " << profile.textures.load() << ", pictures handled by loaders: " << profile.loaded.load() << std::endl;
	out << "local picture index build: " << ms(profile.index_build_ns) << " ms, picture cache pack load: " << ms(profile.pack_load_ns) << " ms, entries in the pack at start: " << profile.cache_entries.load() << std::endl;
	out << "cache hits: " << profile.cache_hits.load() << ", fresh loads (cache misses): " << profile.cache_misses.load() << std::endl;
	out << "--- loader threads (summed over all threads) ---" << std::endl;
	out << "total in LoadCardTexture: " << ms(profile.loader_ns) << " ms" << std::endl;
	out << "  finding the picture file/stamp: " << ms(profile.stamp_ns) << " ms" << std::endl;
	out << "  reading the disk cache: " << ms(profile.cache_read_ns) << " ms" << std::endl;
	out << "  reading+decoding the jpeg: " << ms(profile.decode_ns) << " ms" << std::endl;
	out << "  scaling on the cpu: " << ms(profile.scale_ns) << " ms" << std::endl;
	out << "  writing the disk cache: " << ms(profile.cache_write_ns) << " ms" << std::endl;
	out << "  in ReadCardCache: waiting for the lock + lookup " << ms(profile.lock_wait_ns) << " ms, jpeg decode " << ms(profile.jpeg_decode_ns) << " ms, creating the image " << ms(profile.image_create_ns) << " ms" << std::endl;
	out << "loader threads waiting for work (summed): " << ms(profile.idle_ns) << " ms" << std::endl;
	if(profile.queue_samples) {
		out << "average waiting requests per frame: " << static_cast<double>(profile.queue_to_load_sum) / profile.queue_samples
			<< ", average finished pictures waiting for the main thread: " << static_cast<double>(profile.queue_finished_sum) / profile.queue_samples << std::endl;
	}
#ifdef _WIN32
	{
		FILETIME creation, exit_time, kernel, user;
		if(GetProcessTimes(GetCurrentProcess(), &creation, &exit_time, &kernel, &user)) {
			const auto to_ms = [](const FILETIME& t) { ULARGE_INTEGER v; v.LowPart = t.dwLowDateTime; v.HighPart = t.dwHighDateTime; return static_cast<double>(v.QuadPart) / 10000.0; };
			out << "process cpu time so far: user " << to_ms(user) << " ms, kernel " << to_ms(kernel) << " ms" << std::endl;
		}
	}
#endif
	out << "--- main thread ---" << std::endl;
	out << "RefreshCachedTextures total: " << ms(profile.refresh_ns) << " ms" << std::endl;
	out << "  driver->addTexture: " << ms(profile.add_texture_ns) << " ms" << std::endl;
	out << "UpdateArtPreload total: " << ms(profile.update_ns) << " ms (of which HasLocalCardPicture " << ms(profile.has_local_ns) << " ms)" << std::endl;
	out << "frames: " << profile.frames.load() << ", average frame: " << (profile.frames.load() ? wall / profile.frames.load() : 0.0) << " ms" << std::endl;
	out << "--- textures created vs time (ms) ---" << std::endl;
	for(const auto& point : profile.checkpoints)
		out << point.first << "\t" << point.second << std::endl;
}
void ImageManager::SampleQueues() {
	std::lock_guard<epro::mutex> lck(pic_load);
	profile.queue_samples++;
	profile.queue_to_load_sum += to_load.size();
	profile.queue_finished_sum += loaded_pics[0].size();
}
size_t ImageManager::PendingCardLoads() {
	std::lock_guard<epro::mutex> lck(pic_load);
	return to_load.size() + loaded_pics[0].size();
}
bool ImageManager::HasLocalCardPicture(uint32_t code) const {
	ProfileScope scope(profile.has_local_ns);
	if(local_index_ready) {
		std::shared_lock<std::shared_mutex> lock(local_index_mutex);
		return local_pictures.find(code) != local_pictures.end();
	}
	for(const auto& path : mainGame->pic_dirs) {
		for(auto extension : { EPRO_TEXT(".png"), EPRO_TEXT(".jpg") }) {
			if(path == EPRO_TEXT("archives")) {
				if(auto* archiveFile = Utils::FindFileInArchives(EPRO_TEXT("pics/"), epro::format(EPRO_TEXT("{}{}"), code, extension))) {
					archiveFile->drop();
					return true;
				}
			} else if(Utils::FileExists(epro::format(EPRO_TEXT("{}{}{}"), path, code, extension))) {
				return true;
			}
		}
	}
	return false;
}
void ImageManager::TrimCardTextures(size_t budget_bytes, const std::unordered_set<uint32_t>& keep, const std::unordered_set<uint32_t>& keep_large, uint32_t info_code) {
	auto& map = tMap[0];
	++card_use_tick;
	for(const auto code : keep)
		card_last_use[code] = card_use_tick;
	const size_t per_texture = std::max<size_t>(1, static_cast<size_t>(sizes[0].first.load()) * static_cast<size_t>(sizes[0].second.load()) * 4);
	const size_t max_loaded = std::max<size_t>(budget_bytes / per_texture, keep.size() + 16);
	size_t loaded = 0;
	std::vector<std::pair<uint64_t, uint32_t>> evictable;
	for(const auto& entry : map) {
		if(entry.second.preload_status != preloadStatus::LOADED || !entry.second.texture)
			continue;
		loaded++;
		if(keep.find(entry.first) == keep.end())
			evictable.emplace_back(card_last_use[entry.first], entry.first);
	}
	if(loaded > max_loaded) {
		//least recently seen first; free a little extra so this doesn't have to run every frame
		const size_t target = max_loaded - max_loaded / 20;
		std::sort(evictable.begin(), evictable.end());
		for(const auto& victim : evictable) {
			if(loaded <= target)
				break;
			auto it = map.find(victim.second);
			if(it == map.end())
				continue;
			driver->removeTexture(it->second.texture);
			map.erase(it);
			card_last_use.erase(victim.second);
			loaded--;
		}
	}
	//the large pictures (info panel, sharp card browser cells): keep the ones on screen and drop the rest
	auto& info_map = tMap[1];
	size_t info_loaded = 0;
	for(const auto& entry : info_map) {
		if(entry.second.preload_status == preloadStatus::LOADED && entry.second.texture)
			info_loaded++;
	}
	if(info_loaded > 24) {
		for(auto it = info_map.begin(); it != info_map.end();) {
			if(it->second.preload_status == preloadStatus::LOADED && it->second.texture && it->first != info_code && keep_large.find(it->first) == keep_large.end()) {
				driver->removeTexture(it->second.texture);
				it = info_map.erase(it);
			} else {
				++it;
			}
		}
	}
}
void ImageManager::RefreshCachedTextures() {
	ProfileScope refresh_scope(profile.refresh_ns);
	auto LoadTexture = [this](int index, texture_map& dest, auto& size, imgType type) {
		auto& src = loaded_pics[index];
		std::vector<uint32_t> readd;
		const auto frame_start = std::chrono::steady_clock::now();
		for(int i = 0; i < 2000; i++) {
			//always at least the configured number per frame, then keep going while there is time left in this frame
			if(i >= gGameConfig->maxImagesPerFrame && std::chrono::steady_clock::now() - frame_start > std::chrono::milliseconds(upload_budget_ms.load()))
				break;
			std::unique_lock<epro::mutex> lck(pic_load);
			if(src.empty())
				break;
			auto loaded = std::move(src.front());
			src.pop_front();
			lck.unlock();
			auto& map_elem = dest[loaded.code];
			if(loaded.status == loadStatus::WAIT_DOWNLOAD) {
				map_elem.preload_status = preloadStatus::WAIT_DOWNLOAD;
				continue;
			}
			auto& ret_texture = map_elem.texture;
			map_elem.preload_status = preloadStatus::LOADED;
			if(loaded.status == loadStatus::LOAD_FAIL) {
				ret_texture = nullptr;
				continue;
			}
			auto* texture = loaded.texture;
			const auto loaded_dim = texture->getDimension();
			const irr::u32 wanted_w = static_cast<irr::u32>(size.first);
			const irr::u32 wanted_h = static_cast<irr::u32>(size.second);
			const bool size_ok = (index == 0) ? (loaded_dim.Width >= wanted_w && loaded_dim.Width <= wanted_w * 2 && loaded_dim.Height >= wanted_h && loaded_dim.Height <= wanted_h * 2)
				: (loaded_dim.Width == wanted_w && loaded_dim.Height == wanted_h);
			if(!size_ok) {
				readd.push_back(loaded.code);
				ret_texture = nullptr;
				continue;
			}
			{
				ProfileScope add_scope(profile.add_texture_ns);
				ret_texture = driver->addTexture({ loaded.path.data(), static_cast<irr::u32>(loaded.path.size()) }, texture);
			}
			if(index == 0) {
				profile.last_texture = std::chrono::steady_clock::now();
				if(++profile.textures % 1000 == 0 && profile.started)
					profile.checkpoints.emplace_back(profile.textures.load(), std::chrono::duration<double, std::milli>(profile.last_texture - profile.start).count());
			}
			texture->drop();
		}
		if(readd.size()) {
			std::lock_guard<epro::mutex> lck(pic_load);
			for(auto& code : readd)
				to_load.emplace_front(code, type, index, std::ref(size.first), std::ref(size.second), timestamp_id, std::ref(timestamp_id));
			cv_load.notify_all();
		}
	};
	LoadTexture(0, tMap[0], sizes[0], imgType::ART);
	LoadTexture(1, tMap[1], sizes[1], imgType::ART);
	LoadTexture(2, tThumb, sizes[2], imgType::THUMB);
	LoadTexture(3, tCovers, sizes[1], imgType::COVER);
}
void ImageManager::ClearFutureObjects() {
	Utils::SetThreadName("ImgObjsClear");
	while(!stop_threads) {
		std::unique_lock<epro::mutex> lck(obj_clear_lock);
		while(to_clear.empty()) {
			cv_clear.wait(lck);
			if(stop_threads)
				return;
		}
		auto img = std::move(to_clear.front());
		to_clear.pop_front();
		lck.unlock();
		if(img.texture)
			img.texture->drop();
	}
}

void ImageManager::RefreshCovers() {
	const auto is_base_path = textures_path == BASE_PATH;
	auto reloadTextureWithNewSizes = [this, is_base_path, width = (int)sizes[1].first, height = (int)sizes[1].second](auto*& texture, epro::path_stringview texture_name) {
		auto new_texture = loadTextureFixedSize(texture_name, width, height);
		if(!new_texture && !is_base_path) {
			const auto old_textures_path = std::exchange(textures_path, BASE_PATH);
			new_texture = loadTextureFixedSize(texture_name, width, height);
			textures_path = old_textures_path;
		}
		if(!new_texture)
			return;
		driver->removeTexture(std::exchange(texture, new_texture));
	};
	reloadTextureWithNewSizes(tCover[0], EPRO_TEXT("cover"sv));
	driver->removeTexture(std::exchange(tCover[1], nullptr));
	reloadTextureWithNewSizes(tCover[1], EPRO_TEXT("cover2"sv));
	if(!tCover[1])
		tCover[1] = tCover[0];
	reloadTextureWithNewSizes(tUnknown, EPRO_TEXT("unknown"sv));
}
void ImageManager::LoadPic() {
	Utils::SetThreadName("PicLoader");
#ifdef _WIN32
	//the loaders must never get in the way of the thread that draws the game
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#endif
	while(!stop_threads) {
		const auto idle_begin = std::chrono::steady_clock::now();
		std::unique_lock<epro::mutex> lck(pic_load);
		while(to_load.empty()) {
			cv_load.wait(lck);
			if(stop_threads) {
				return;
			}
		}
		profile.idle_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - idle_begin).count();
		auto loaded = std::move(to_load.front());
		to_load.pop_front();
		lck.unlock();
		ProfileScope loader_scope(profile.loader_ns);
		auto load_status = LoadCardTexture(loaded.code, loaded.type, loaded.reference_width, loaded.reference_height, loaded.timestamp, loaded.reference_timestamp);
		profile.loaded++;
		lck.lock();
		loaded_pics[loaded.index].push_front(std::move(load_status));
	}
}
void ImageManager::ClearCachedTextures() {
	timestamp_id = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	std::lock_guard<epro::mutex> lck(obj_clear_lock);
	{
		std::lock_guard<epro::mutex> lck2(pic_load);
		for(auto& map : loaded_pics) {
			to_clear.insert(to_clear.end(), std::make_move_iterator(map.begin()), std::make_move_iterator(map.end()));
			map.clear();
		}
		to_load.clear();
	}
	cv_clear.notify_one();
}
// function by Warr1024, from https://github.com/minetest/minetest/issues/2419 , modified
bool ImageManager::imageScaleNNAA(irr::video::IImage* src, irr::video::IImage* dest, chrono_time timestamp_id, const std::atomic<chrono_time>& source_timestamp_id) {
	// Cache rectsngle boundaries.
	auto& sdim = src->getDimension();
	const double sw = sdim.Width;
	const double sh = sdim.Height;

	// Walk each destination image pixel.
	// Note: loop y around x for better cache locality.
	const auto& dim = dest->getDimension();
	const auto divw = sw / dim.Width;
	const auto divh = sh / dim.Height;
	irr::u32 dy = 0;
	for(; dy < dim.Height && timestamp_id == source_timestamp_id; dy++) {
		for(irr::u32 dx = 0; dx < dim.Width; dx++) {
			// Calculate floating-point source rectangle bounds.
			const double minsx = dx * divw;
			const double maxsx = minsx + divw;
			const double minsy = dy * divh;
			const double maxsy = minsy + divh;

			// Total area, and integral of r, g, b values over that area,
			// initialized to zero, to be summed up in next loops.
			double area = 0, ra = 0, ga = 0, ba = 0, aa = 0;
			irr::video::SColor pxl;
			const auto csy = std::floor(minsy);
			const auto csx = std::floor(minsx);
			// Loop over the integral pixel positions described by those bounds.
			for(double sy = csy; sy < maxsy; sy++)
				for(double sx = csx; sx < maxsx; sx++) {
					// Calculate width, height, then area of dest pixel
					// that's covered by this source pixel.
					double pw = 1;
					if(minsx > sx)
						pw += sx - minsx;
					if(maxsx < (sx + 1))
						pw += maxsx - sx - 1;
					double ph = 1;
					if(minsy > sy)
						ph += sy - minsy;
					if(maxsy < (sy + 1))
						ph += maxsy - sy - 1;
					const double pa = pw * ph;

					// Get source pixel and add it to totals, weighted
					// by covered area and alpha.
					pxl = src->getPixel(sx, sy);
					area += pa;
					ra += pa * pxl.getRed();
					ga += pa * pxl.getGreen();
					ba += pa * pxl.getBlue();
					aa += pa * pxl.getAlpha();
				}

			// Set the destination image pixel to the average color.
			if(area > 0) {
				pxl.setRed(ra / area + 0.5);
				pxl.setGreen(ga / area + 0.5);
				pxl.setBlue(ba / area + 0.5);
				pxl.setAlpha(aa / area + 0.5);
			} else {
				pxl.setRed(0);
				pxl.setGreen(0);
				pxl.setBlue(0);
				pxl.setAlpha(0);
			}
			dest->setPixel(dx, dy, pxl);
		}
	}
	return dy == dim.Height;
}
irr::video::IImage* ImageManager::GetScaledImage(irr::video::IImage* srcimg, int width, int height, chrono_time call_timestamp_id, const std::atomic<chrono_time>& source_timestamp_id) {
	ProfileScope scope(profile.scale_ns);
	if(width <= 0 || height <= 0)
		return nullptr;
	if(!srcimg || call_timestamp_id != source_timestamp_id.load())
		return nullptr;
	const irr::core::dimension2d<irr::u32> dim(width, height);
	if(srcimg->getDimension() == dim) {
		srcimg->grab();
		return srcimg;
	} else {
		irr::video::IImage* destimg = driver->createImage(srcimg->getColorFormat(), dim);
		if(call_timestamp_id != source_timestamp_id || !imageScaleNNAA(srcimg, destimg, call_timestamp_id, source_timestamp_id)) {
			destimg->drop();
			destimg = nullptr;
		}
		return destimg;
	}
}
irr::video::ITexture* ImageManager::GetTextureFromFile(const irr::io::path& file, int width, int height) {
	auto img = GetScaledImageFromFile(file, width, height);
	if(img) {
		auto texture = driver->addTexture(file, img);
		img->drop();
		if(texture)
			return texture;
	}
	return driver->getTexture(file);
}
namespace {
struct FastJpegError {
	jpeg_error_mgr pub;
	jmp_buf jump;
};
void FastJpegErrorExit(j_common_ptr cinfo) {
	longjmp(reinterpret_cast<FastJpegError*>(cinfo->err)->jump, 1);
}
void FastJpegMessage(j_common_ptr) {}
}
irr::video::IImage* ImageManager::LoadCardImageFast(irr::io::IReadFile* file, int min_w, int min_h) {
	ProfileScope scope(profile.decode_ns);
	if(!file)
		return nullptr;
	const long size = file->getSize();
	if(size < 4)
		return nullptr;
	std::vector<uint8_t> input(static_cast<size_t>(size));
	if(file->read(input.data(), static_cast<irr::u32>(size)) != size)
		return nullptr;
	if(input[0] != 0xFF || input[1] != 0xD8) {
		//not a JPEG (png, ...), use the normal loader
		file->seek(0);
		return driver->createImageFromFile(file);
	}
	jpeg_decompress_struct cinfo;
	FastJpegError jerr;
	uint8_t* volatile pixels = nullptr;
	cinfo.err = jpeg_std_error(&jerr.pub);
	jerr.pub.error_exit = FastJpegErrorExit;
	jerr.pub.output_message = FastJpegMessage;
	if(setjmp(jerr.jump)) {
		jpeg_destroy_decompress(&cinfo);
		delete[] pixels;
		return nullptr;
	}
	jpeg_create_decompress(&cinfo);
	jpeg_mem_src(&cinfo, input.data(), static_cast<unsigned long>(input.size()));
	jpeg_read_header(&cinfo, TRUE);
	if(cinfo.jpeg_color_space == JCS_CMYK || cinfo.jpeg_color_space == JCS_YCCK) {
		jpeg_destroy_decompress(&cinfo);
		file->seek(0);
		return driver->createImageFromFile(file);
	}
	//B, G, R, A in memory is Irrlicht's ECF_A8R8G8B8, the format the texture is stored in, so the texture can be
	//created without converting every pixel on the main thread
	cinfo.out_color_space = JCS_EXT_BGRA;
	cinfo.do_fancy_upsampling = FALSE;
	unsigned int denom = 1;
	while(denom < 8 && static_cast<int>(cinfo.image_width / (denom * 2)) >= min_w && static_cast<int>(cinfo.image_height / (denom * 2)) >= min_h)
		denom *= 2;
	cinfo.scale_num = 1;
	cinfo.scale_denom = denom;
	jpeg_start_decompress(&cinfo);
	const irr::u32 width = cinfo.output_width;
	const irr::u32 height = cinfo.output_height;
	const size_t rowspan = static_cast<size_t>(width) * 4;
	pixels = new uint8_t[rowspan * height];
	while(cinfo.output_scanline < height) {
		JSAMPROW row = pixels + static_cast<size_t>(cinfo.output_scanline) * rowspan;
		jpeg_read_scanlines(&cinfo, &row, 1);
	}
	jpeg_finish_decompress(&cinfo);
	jpeg_destroy_decompress(&cinfo);
	return driver->createImageFromData(irr::video::ECF_A8R8G8B8, irr::core::dimension2d<irr::u32>(width, height), pixels, true, true);
}
irr::video::IImage* ImageManager::LoadCardImageFastFromPath(const irr::io::path& path, int min_w, int min_h) {
	auto* file = device->getFileSystem()->createAndOpenFile(path);
	if(!file)
		return nullptr;
	auto* image = LoadCardImageFast(file, min_w, min_h);
	file->drop();
	return image;
}
namespace {
constexpr uint32_t CARD_PACK_MAGIC = 0x31504345; //'ECP1'
constexpr uint32_t CARD_PACK_RECORD_MAGIC = 0x31434552; //'REC1'
struct CardPackRecord {
	uint32_t magic;
	uint32_t code;
	uint32_t width;
	uint32_t height;
	uint64_t src_size;
	int64_t src_mtime;
	uint32_t length;
	uint32_t reserved;
};
struct CompressedBuffer {
	unsigned char* data = nullptr;
	unsigned long size = 0;
};
bool EncodeCacheJpeg(const uint8_t* bgra, uint32_t width, uint32_t height, std::vector<uint8_t>& out) {
	jpeg_compress_struct cinfo;
	FastJpegError jerr;
	CompressedBuffer buffer;
	cinfo.err = jpeg_std_error(&jerr.pub);
	jerr.pub.error_exit = FastJpegErrorExit;
	jerr.pub.output_message = FastJpegMessage;
	if(setjmp(jerr.jump)) {
		jpeg_destroy_compress(&cinfo);
		free(buffer.data);
		return false;
	}
	jpeg_create_compress(&cinfo);
	jpeg_mem_dest(&cinfo, &buffer.data, &buffer.size);
	cinfo.image_width = width;
	cinfo.image_height = height;
	cinfo.input_components = 4;
	cinfo.in_color_space = JCS_EXT_BGRA;
	jpeg_set_defaults(&cinfo);
	jpeg_set_quality(&cinfo, 92, TRUE);
	jpeg_start_compress(&cinfo, TRUE);
	while(cinfo.next_scanline < height) {
		JSAMPROW row = const_cast<uint8_t*>(bgra) + static_cast<size_t>(cinfo.next_scanline) * width * 4;
		jpeg_write_scanlines(&cinfo, &row, 1);
	}
	jpeg_finish_compress(&cinfo);
	out.assign(buffer.data, buffer.data + buffer.size);
	jpeg_destroy_compress(&cinfo);
	free(buffer.data);
	return true;
}
//decodes into B, G, R, A, nullptr if the data is not a usable JPEG of the expected size
uint8_t* DecodeCacheJpeg(const uint8_t* data, size_t size, uint32_t width, uint32_t height) {
	jpeg_decompress_struct cinfo;
	FastJpegError jerr;
	uint8_t* volatile pixels = nullptr;
	cinfo.err = jpeg_std_error(&jerr.pub);
	jerr.pub.error_exit = FastJpegErrorExit;
	jerr.pub.output_message = FastJpegMessage;
	if(setjmp(jerr.jump)) {
		jpeg_destroy_decompress(&cinfo);
		delete[] pixels;
		return nullptr;
	}
	jpeg_create_decompress(&cinfo);
	jpeg_mem_src(&cinfo, data, static_cast<unsigned long>(size));
	jpeg_read_header(&cinfo, TRUE);
	if(cinfo.image_width != width || cinfo.image_height != height) {
		jpeg_destroy_decompress(&cinfo);
		return nullptr;
	}
	cinfo.out_color_space = JCS_EXT_BGRA;
	cinfo.do_fancy_upsampling = FALSE;
	jpeg_start_decompress(&cinfo);
	const size_t rowspan = static_cast<size_t>(width) * 4;
	pixels = new uint8_t[rowspan * height];
	while(cinfo.output_scanline < height) {
		JSAMPROW row = pixels + static_cast<size_t>(cinfo.output_scanline) * rowspan;
		jpeg_read_scanlines(&cinfo, &row, 1);
	}
	jpeg_finish_decompress(&cinfo);
	jpeg_destroy_decompress(&cinfo);
	return pixels;
}
std::filesystem::path CardPackPath() {
	return std::filesystem::path(L"cardcache") / L"cards.pack";
}
//"<digits>.png" or "<digits>.jpg" (any case)
template<typename S>
bool ParsePictureName(const S& name, uint32_t& code, bool& is_png) {
	size_t i = 0;
	uint64_t value = 0;
	while(i < name.size() && i < 10 && name[i] >= '0' && name[i] <= '9') {
		value = value * 10 + static_cast<uint64_t>(name[i] - '0');
		++i;
	}
	if(i == 0 || i >= name.size() || name[i] != '.' || value > 0xffffffffull)
		return false;
	const auto ext_is = [&](const char* ext) {
		const size_t length = std::strlen(ext);
		if(name.size() - (i + 1) != length)
			return false;
		for(size_t k = 0; k < length; ++k) {
			auto c = name[i + 1 + k];
			if(c >= 'A' && c <= 'Z')
				c = static_cast<decltype(c)>(c + ('a' - 'A'));
			if(c != ext[k])
				return false;
		}
		return true;
	};
	if(ext_is("png"))
		is_png = true;
	else if(ext_is("jpg"))
		is_png = false;
	else
		return false;
	code = static_cast<uint32_t>(value);
	return true;
}
}
void ImageManager::BuildLocalPictureIndex() {
	ProfileScope scope(profile.index_build_ns);
	std::unordered_map<uint32_t, LocalPicture> found;
	found.reserve(32768);
	for(const auto& dir : mainGame->pic_dirs) {
		if(dir == EPRO_TEXT("archives")) {
			for(const auto& name : Utils::ListFilesInArchives(EPRO_TEXT("pics/"))) {
				uint32_t code;
				bool is_png;
				if(ParsePictureName(name, code, is_png))
					found.emplace(code, LocalPicture{ 0, 0 });
			}
			continue;
		}
		//in one folder a png wins over a jpg, an earlier folder wins over a later one (the order the pictures are looked up in)
		std::unordered_map<uint32_t, std::pair<LocalPicture, bool>> in_dir;
		std::error_code ec;
		for(auto it = std::filesystem::directory_iterator(std::filesystem::path(dir), ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
			const auto& entry = *it;
			uint32_t code;
			bool is_png;
			if(!ParsePictureName(entry.path().filename().native(), code, is_png))
				continue;
			std::error_code entry_ec;
			const auto file_size = entry.file_size(entry_ec);
			if(entry_ec)
				continue;
			const auto time = entry.last_write_time(entry_ec);
			if(entry_ec)
				continue;
			const LocalPicture picture{ static_cast<uint64_t>(file_size), static_cast<int64_t>(time.time_since_epoch().count()) };
			auto existing = in_dir.find(code);
			if(existing == in_dir.end())
				in_dir.emplace(code, std::make_pair(picture, is_png));
			else if(is_png && !existing->second.second)
				existing->second = std::make_pair(picture, true);
		}
		for(const auto& entry : in_dir)
			found.emplace(entry.first, entry.second.first);
	}
	{
		std::unique_lock<std::shared_mutex> lock(local_index_mutex);
		local_pictures = std::move(found);
	}
	local_index_dirs = mainGame->pic_dirs.size();
	local_index_ready = true;
}
void ImageManager::EnsureLocalPictureIndex() {
	if(!local_index_ready || local_index_dirs != mainGame->pic_dirs.size())
		BuildLocalPictureIndex();
}
bool ImageManager::FindCardSourceStamp(uint32_t code, uint64_t& size, int64_t& mtime) const {
	ProfileScope scope(profile.stamp_ns);
	if(local_index_ready) {
		std::shared_lock<std::shared_mutex> lock(local_index_mutex);
		const auto it = local_pictures.find(code);
		if(it != local_pictures.end()) {
			size = it->second.size;
			mtime = it->second.mtime;
			return true;
		}
	}
	//not in the index (not built yet, or a picture that was downloaded after it was built): look for the file
	for(const auto& path : mainGame->pic_dirs) {
		for(auto extension : { EPRO_TEXT(".png"), EPRO_TEXT(".jpg") }) {
			if(path == EPRO_TEXT("archives")) {
				if(auto* archiveFile = Utils::FindFileInArchives(EPRO_TEXT("pics/"), epro::format(EPRO_TEXT("{}{}"), code, extension))) {
					archiveFile->drop();
					size = 0;
					mtime = 0;
					return true;
				}
			} else {
				std::error_code ec;
				const std::filesystem::path file(epro::format(EPRO_TEXT("{}{}{}"), path, code, extension));
				const auto file_size = std::filesystem::file_size(file, ec);
				if(ec)
					continue;
				const auto time = std::filesystem::last_write_time(file, ec);
				if(ec)
					continue;
				size = file_size;
				mtime = static_cast<int64_t>(time.time_since_epoch().count());
				return true;
			}
		}
	}
	return false;
}
void ImageManager::LoadCardCachePack() {
	ProfileScope scope(profile.pack_load_ns);
	{
		std::ifstream in(CardPackPath(), std::ios::binary | std::ios::ate);
		if(in) {
			const std::streamoff size = in.tellg();
			if(size >= 8) {
				card_cache_blob.resize(static_cast<size_t>(size));
				in.seekg(0);
				in.read(reinterpret_cast<char*>(card_cache_blob.data()), size);
				if(!in)
					card_cache_blob.clear();
			}
		}
	}
	uint32_t magic = 0;
	if(card_cache_blob.size() >= 8)
		std::memcpy(&magic, card_cache_blob.data(), sizeof(magic));
	if(magic == CARD_PACK_MAGIC) {
		size_t pos = 8;
		card_cache.reserve(32768);
		while(pos + sizeof(CardPackRecord) <= card_cache_blob.size()) {
			CardPackRecord record;
			std::memcpy(&record, card_cache_blob.data() + pos, sizeof(record));
			//a record that is cut off (the game was closed while writing) ends the usable part of the file
			if(record.magic != CARD_PACK_RECORD_MAGIC || pos + sizeof(record) + record.length > card_cache_blob.size())
				break;
			card_cache[record.code] = CardCacheEntry{ record.width, record.height, record.src_size, record.src_mtime,
													  card_cache_blob.data() + pos + sizeof(record), record.length };
			pos += sizeof(record) + record.length;
		}
		card_cache_valid_length = pos;
		card_cache_file_ok = true;
	} else {
		card_cache_blob.clear();
	}
	profile.cache_entries = card_cache.size();
	//the earlier version kept one file per card in the same folder, remove them a little later in the background
	std::thread([]() {
		std::this_thread::sleep_for(std::chrono::seconds(45));
		std::error_code ec;
		for(auto it = std::filesystem::directory_iterator(L"cardcache", ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
			const auto extension = it->path().extension().native();
			if(extension == L".ecc" || extension == L".part") {
				std::error_code remove_ec;
				std::filesystem::remove(it->path(), remove_ec);
			}
		}
	}).detach();
}
bool ImageManager::OpenCardCacheForAppend() {
	//the caller holds card_cache_write_mutex
	if(card_cache_out.is_open())
		return true;
	std::error_code ec;
	std::filesystem::create_directories(L"cardcache", ec);
	const auto path = CardPackPath();
	if(card_cache_file_ok) {
		//drop a partially written record at the end, if there is one
		std::filesystem::resize_file(path, card_cache_valid_length, ec);
		if(ec)
			return false;
		card_cache_out.open(path, std::ios::binary | std::ios::app);
	} else {
		card_cache_out.open(path, std::ios::binary | std::ios::trunc);
		if(card_cache_out) {
			const uint32_t header[2] = { CARD_PACK_MAGIC, 1 };
			card_cache_out.write(reinterpret_cast<const char*>(header), sizeof(header));
			card_cache_file_ok = true;
		}
	}
	return card_cache_out.is_open();
}
void ImageManager::FlushCardCache() {
	std::lock_guard<std::mutex> lock(card_cache_write_mutex);
	if(card_cache_out.is_open())
		card_cache_out.flush();
	card_cache_unflushed = 0;
}
irr::video::IImage* ImageManager::ReadCardCache(uint32_t code, int width, int height, uint64_t src_size, int64_t src_mtime, bool any_size) {
	ProfileScope scope(profile.cache_read_ns);
	std::call_once(card_cache_loaded, [this]() { LoadCardCachePack(); });
	CardCacheEntry entry;
	{
		ProfileScope lock_scope(profile.lock_wait_ns);
		std::shared_lock<std::shared_mutex> lock(card_cache_mutex);
		const auto it = card_cache.find(code);
		if(it == card_cache.end())
			return nullptr;
		entry = it->second;
	}
	if(entry.src_size != src_size || entry.src_mtime != src_mtime)
		return nullptr;
	//same rule the texture creation uses: between 1x and 2x of the wanted size
	if(!any_size && (width <= 0 || height <= 0 || entry.width < static_cast<uint32_t>(width) || entry.height < static_cast<uint32_t>(height)
	   || entry.width > static_cast<uint32_t>(width) * 2 || entry.height > static_cast<uint32_t>(height) * 2))
		return nullptr;
	uint8_t* pixels;
	{
		ProfileScope decode_scope(profile.jpeg_decode_ns);
		pixels = DecodeCacheJpeg(entry.data, entry.length, entry.width, entry.height);
	}
	if(!pixels)
		return nullptr;
	ProfileScope create_scope(profile.image_create_ns);
	return driver->createImageFromData(irr::video::ECF_A8R8G8B8, irr::core::dimension2d<irr::u32>(entry.width, entry.height), pixels, true, true);
}
void ImageManager::WriteCardCache(uint32_t code, irr::video::IImage* image, uint64_t src_size, int64_t src_mtime) {
	ProfileScope scope(profile.cache_write_ns);
	if(!image || image->getColorFormat() != irr::video::ECF_A8R8G8B8)
		return;
	std::call_once(card_cache_loaded, [this]() { LoadCardCachePack(); });
	const auto dim = image->getDimension();
	std::vector<uint8_t> jpeg;
	if(!EncodeCacheJpeg(static_cast<const uint8_t*>(image->getData()), dim.Width, dim.Height, jpeg))
		return;
	const CardPackRecord record{ CARD_PACK_RECORD_MAGIC, code, dim.Width, dim.Height, src_size, src_mtime, static_cast<uint32_t>(jpeg.size()), 0 };
	{
		std::lock_guard<std::mutex> lock(card_cache_write_mutex);
		if(!OpenCardCacheForAppend())
			return;
		card_cache_out.write(reinterpret_cast<const char*>(&record), sizeof(record));
		card_cache_out.write(reinterpret_cast<const char*>(jpeg.data()), static_cast<std::streamsize>(jpeg.size()));
		if(++card_cache_unflushed >= 512) {
			card_cache_out.flush();
			card_cache_unflushed = 0;
		}
	}
	std::unique_lock<std::shared_mutex> lock(card_cache_mutex);
	card_cache_extra.emplace_back(std::move(jpeg));
	const auto& stored = card_cache_extra.back();
	card_cache[code] = CardCacheEntry{ dim.Width, dim.Height, src_size, src_mtime, stored.data(), static_cast<uint32_t>(stored.size()) };
}
ImageManager::load_return ImageManager::LoadCardTexture(uint32_t code, imgType type, const std::atomic<irr::s32>& _width, const std::atomic<irr::s32>& _height, chrono_time call_timestamp_id, const std::atomic<chrono_time>& source_timestamp_id) {
	int width = _width;
	int height = _height;
	const bool is_thumb = type == imgType::THUMB;
	if(type == imgType::THUMB)
		type = imgType::ART;
	load_return ret{ loadStatus::LOAD_FAIL, code };
	auto LoadImg = [&](irr::video::IImage* base_img)->irr::video::IImage* {
		if(!base_img)
			return nullptr;
		if(width != _width || height != _height) {
			width = _width;
			height = _height;
		}
		if(type == imgType::ART && &_width == &sizes[0].first && width > 0 && height > 0) {
			const auto dim = base_img->getDimension();
			if(dim.Width >= static_cast<irr::u32>(width) && dim.Height >= static_cast<irr::u32>(height)
			   && dim.Width <= static_cast<irr::u32>(width) * 2 && dim.Height <= static_cast<irr::u32>(height) * 2)
				return base_img; //already between 1x and 2x of the wanted size, the GPU scales it when it is drawn
		}
		while(const auto img = GetScaledImage(base_img, width, height, call_timestamp_id, source_timestamp_id)) {
			if(call_timestamp_id != source_timestamp_id.load()) {
				img->drop();
				base_img->drop();
				return nullptr;
			}
			if(width != _width || height != _height) {
				img->drop();
				width = _width;
				height = _height;
				continue;
			}
			base_img->drop();
			return img;
		}
		base_img->drop();
		return nullptr;
	};

	irr::video::IImage* img;
	//the card browser grid uses the small size: try the disk cache first
	const bool grid_size = (type == imgType::ART && &_width == &sizes[0].first);
	uint64_t cache_src_size = 0;
	int64_t cache_src_mtime = 0;
	const bool cache_usable = grid_size && FindCardSourceStamp(code, cache_src_size, cache_src_mtime);
	if(cache_usable && (img = ReadCardCache(code, width, height, cache_src_size, cache_src_mtime)) != nullptr) {
		profile.cache_hits++;
		ret.status = loadStatus::LOAD_OK;
		ret.path = epro::format(EPRO_TEXT("./pics/{}.jpg"), code);
		ret.texture = img;
		return ret;
	}

	//the small deck editor pictures are made from the (already small) cached picture when there is one
	if(is_thumb && FindCardSourceStamp(code, cache_src_size, cache_src_mtime)
	   && (img = ReadCardCache(code, width, height, cache_src_size, cache_src_mtime, true)) != nullptr) {
		if((img = LoadImg(img)) != nullptr) {
			ret.status = loadStatus::LOAD_OK;
			ret.path = epro::format(EPRO_TEXT("./pics/{}.jpg"), code);
			ret.texture = img;
			return ret;
		}
		if(call_timestamp_id != source_timestamp_id.load())
			return ret;
	}

	auto status = gImageDownloader->GetDownloadStatus(code, type);
	if(status == ImageDownloader::downloadStatus::DOWNLOADED) {
		if(call_timestamp_id != source_timestamp_id.load())
			return ret;
		const auto file = gImageDownloader->GetDownloadPath(code, type);
		if((img = LoadImg(LoadCardImageFastFromPath(irr::io::path{ file.data(), static_cast<irr::u32>(file.size()) }, width, height))) != nullptr) {
			ret.status = loadStatus::LOAD_OK;
			ret.path = epro::path_string{ file };
			if(cache_usable)
				{ profile.cache_misses++; WriteCardCache(code, img, cache_src_size, cache_src_mtime); }
			ret.texture = img;
		}
		return ret;
	} else if(status == ImageDownloader::downloadStatus::NONE) {
		for(auto& path : (type == imgType::ART) ? mainGame->pic_dirs : mainGame->cover_dirs) {
			for(auto extension : { EPRO_TEXT(".png"), EPRO_TEXT(".jpg") }) {
				if(call_timestamp_id != source_timestamp_id.load())
					return ret;
				irr::video::IImage* base_img = nullptr;
				epro::path_string file;
				if(path == EPRO_TEXT("archives")) {
					auto archiveFile = Utils::FindFileInArchives(
						(type == imgType::ART) ? EPRO_TEXT("pics/") : EPRO_TEXT("pics/cover/"),
						epro::format(EPRO_TEXT("{}{}"), code, extension));
					if(!archiveFile)
						continue;
					const auto& name = archiveFile->getFileName();
					file = { name.c_str(), name.size() };
					base_img = LoadCardImageFast(archiveFile, width, height);
					archiveFile->drop();
				} else {
					file = epro::format(EPRO_TEXT("{}{}{}"), path, code, extension);
					base_img = LoadCardImageFastFromPath(irr::io::path{ file.data(), static_cast<irr::u32>(file.size()) }, width, height);
				}
				if((img = LoadImg(base_img)) != nullptr) {
					ret.status = loadStatus::LOAD_OK;
					ret.path = file;
					if(cache_usable)
						{ profile.cache_misses++; WriteCardCache(code, img, cache_src_size, cache_src_mtime); }
					ret.texture = img;
					return ret;
				}
			}
		}
		gImageDownloader->AddToDownloadQueue(code, type);
		ret.status = loadStatus::WAIT_DOWNLOAD;
		return ret;
	}
	return ret;
}
irr::video::ITexture* ImageManager::GetTextureCard(uint32_t code, imgType type, bool wait, bool fit, int* chk) {
	if(chk)
		*chk = 1;
	irr::video::ITexture* ret_unk = tUnknown;
	int index;
	int size_index;
	auto& map = [&]()->texture_map& {
		switch(type) {
			case imgType::ART: {
				index = fit ? 1 : 0;
				size_index = index;
				return tMap[fit ? 1 : 0];
			}
			case imgType::THUMB: {
				index = 2;
				size_index = index;
				return tThumb;
			}
			case imgType::COVER: {
				ret_unk = tCover[0];
				index = 3;
				size_index = 0;
				return tCovers;
			}
			default:
				unreachable();
		}
	}();
	if(code == 0)
		return ret_unk;
	auto& elem = map[code];
	if(elem.preload_status != preloadStatus::LOADED) {
		auto status = gImageDownloader->GetDownloadStatus(code, type);
		if(status == ImageDownloader::downloadStatus::DOWNLOADING) {
			if(chk)
				*chk = 2;
			return ret_unk;
		}
		//pic will be loaded below instead
		/*if(status == ImageDownloader::DOWNLOADED) {
			map[code] = driver->getTexture(gImageDownloader->GetDownloadPath(code, type).data());
			return map[code] ? map[code] : ret_unk;
		}*/
		if(status == ImageDownloader::downloadStatus::DOWNLOAD_ERROR) {
			map[code].texture = nullptr;
			return ret_unk;
		}
		if(chk)
			*chk = 2;
		if(elem.preload_status == preloadStatus::NONE || (elem.preload_status == preloadStatus::WAIT_DOWNLOAD && status == ImageDownloader::downloadStatus::DOWNLOADED)) {
			elem.preload_status = preloadStatus::LOADING;
			if(wait) {
				auto load_result = LoadCardTexture(code, type, sizes[size_index].first, sizes[size_index].second, timestamp_id, timestamp_id);
				auto& rmap = map[code].texture;
				if(load_result.status == loadStatus::LOAD_OK) {
					rmap = driver->addTexture(load_result.path.data(), load_result.texture);
					load_result.texture->drop();
					if(chk)
						*chk = 1;
				} else {
					rmap = nullptr;
					if(chk)
						*chk = 0;
				}
				return (rmap) ? rmap : ret_unk;
			} else {
				std::lock_guard<epro::mutex> lck(pic_load);
				to_load.emplace_front(code, type, index, std::ref(sizes[size_index].first), std::ref(sizes[size_index].second), timestamp_id.load(), std::ref(timestamp_id));
				cv_load.notify_one();
			}
		}
		return ret_unk;
	}
	auto* texture = elem.texture;
	if(chk && texture == nullptr)
		*chk = 0;
	if(texture)
		return texture;
	return ret_unk;
}
irr::video::ITexture* ImageManager::GetTextureField(uint32_t code) {
	if(code == 0)
		return nullptr;
	auto tit = tFields.find(code);
	if(tit != tFields.end())
		return tit->second;
	auto status = gImageDownloader->GetDownloadStatus(code, imgType::FIELD);
	if(status != ImageDownloader::downloadStatus::NONE) {
		if(status == ImageDownloader::downloadStatus::DOWNLOADED) {
			const auto path = gImageDownloader->GetDownloadPath(code, imgType::FIELD);
			auto downloaded = driver->getTexture({ path.data(), static_cast<irr::u32>(path.size()) });
			tFields.emplace(code, downloaded);
			return downloaded;
		}
		return nullptr;
	}
	for(auto& path : mainGame->field_dirs) {
		for(auto extension : { EPRO_TEXT(".png"), EPRO_TEXT(".jpg") }) {
			irr::video::ITexture* img;
			if(path == EPRO_TEXT("archives")) {
				auto archiveFile = Utils::FindFileInArchives(EPRO_TEXT("pics/field/"), epro::format(EPRO_TEXT("{}{}"), code, extension));
				if(!archiveFile)
					continue;
				img = driver->getTexture(archiveFile);
				archiveFile->drop();
			} else
				img = driver->getTexture(epro::format(EPRO_TEXT("{}{}{}"), path, code, extension).data());
			if(img) {
				tFields.emplace(code, img);
				return img;
			}
		}
	}
	gImageDownloader->AddToDownloadQueue(code, imgType::FIELD);
	return nullptr;
}

irr::video::ITexture* ImageManager::GetCheckboxScaledTexture(float scale) {
	if(scale > 3.5f && tCheckBox[2])
			return tCheckBox[2];
	if(scale > 2.0f && tCheckBox[1])
		return tCheckBox[1];
	return tCheckBox[0];
}


/*
From minetest: Copyright (C) 2015 Aaron Suen <warr1024@gmail.com>
https://github.com/minetest/minetest/blob/5506e97ed897dde2d4820fe1b021a4622bae03b3/src/client/guiscalingfilter.cpp
originally under LGPL2.1+
*/



/* Fill in RGB values for transparent pixels, to correct for odd colors
 * appearing at borders when blending.  This is because many PNG optimizers
 * like to discard RGB values of transparent pixels, but when blending then
 * with non-transparent neighbors, their RGB values will shpw up nonetheless.
 *
 * This function modifies the original image in-place.
 *
 * Parameter "threshold" is the alpha level below which pixels are considered
 * transparent.  Should be 127 for 3d where alpha is threshold, but 0 for
 * 2d where alpha is blended.
 */
static void imageCleanTransparent(irr::video::IImage* src, irr::u32 threshold) {
	const auto& dim = src->getDimension();

	// Walk each pixel looking for fully transparent ones.
	// Note: loop y around x for better cache locality.
	for(irr::u32 ctry = 0; ctry < dim.Height; ctry++)
		for(irr::u32 ctrx = 0; ctrx < dim.Width; ctrx++) {

			// Ignore opaque pixels.
			auto c = src->getPixel(ctrx, ctry);
			if(c.getAlpha() > threshold)
				continue;

			// Sample size and total weighted r, g, b values.
			irr::u32 ss = 0, sr = 0, sg = 0, sb = 0;

			// Walk each neighbor pixel (clipped to image bounds).
			for(irr::u32 sy = (ctry < 1) ? 0 : (ctry - 1);
				sy <= (ctry + 1) && sy < dim.Height; sy++)
				for(irr::u32 sx = (ctrx < 1) ? 0 : (ctrx - 1);
					sx <= (ctrx + 1) && sx < dim.Width; sx++) {

				// Ignore transparent pixels.
				const auto d = src->getPixel(sx, sy);
				if(d.getAlpha() <= threshold)
					continue;

				// Add RGB values weighted by alpha.
				const auto a = d.getAlpha();
				ss += a;
				sr += a * d.getRed();
				sg += a * d.getGreen();
				sb += a * d.getBlue();
			}

			// If we found any neighbor RGB data, set pixel to average
			// weighted by alpha.
			if(ss > 0) {
				c.setRed(sr / ss);
				c.setGreen(sg / ss);
				c.setBlue(sb / ss);
				src->setPixel(ctrx, ctry, c);
			}
		}
}

/* Scale a region of an image into another image, using nearest-neighbor with
 * anti-aliasing; treat pixels as crisp rectangles, but blend them at boundaries
 * to prevent non-integer scaling ratio artifacts.  Note that this may cause
 * some blending at the edges where pixels don't line up perfectly, but this
 * filter is designed to produce the most accurate results for both upscaling
 * and downscaling.
 */
static void imageScaleNNAAUnthreaded(irr::video::IImage* src, const irr::core::rect<irr::s32>& srcrect, irr::video::IImage* dest) {
	// Cache rectangle boundaries.
	const double sox = srcrect.UpperLeftCorner.X;
	const double soy = srcrect.UpperLeftCorner.Y;
	const double sw = srcrect.getWidth();
	const double sh = srcrect.getHeight();

	// Walk each destination image pixel.
	// Note: loop y around x for better cache locality.
	const auto& dim = dest->getDimension();
	const auto divw = sw / dim.Width;
	const auto divh = sh / dim.Height;
	for(irr::u32 dy = 0; dy < dim.Height; dy++)
		for(irr::u32 dx = 0; dx < dim.Width; dx++) {

			// Calculate floating-point source rectangle bounds.
			// Do some basic clipping, and for mirrored/flipped rects,
			// make sure min/max are in the right order.
			auto minsx = std::min(std::max(sox + (dx * divw), 0.0), sw + sox);
			auto maxsx = std::min(std::max(minsx + divw, 0.0), sw + sox);
			if(minsx > maxsx)
				std::swap(minsx, maxsx);
			auto minsy = std::min(std::max(soy + (dy * divh), 0.0), sh + soy);
			auto maxsy = std::min(std::max(minsy + divh, 0.0), sh + soy);
			if(minsy > maxsy)
				std::swap(minsy, maxsy);

			const auto csy = std::floor(minsy);
			const auto csx = std::floor(minsx);

			// Total area, and integral of r, g, b values over that area,
			// initialized to zero, to be summed up in next loops.
			double area = 0, ra = 0, ga = 0, ba = 0, aa = 0;
			irr::video::SColor pxl;

			// Loop over the integral pixel positions described by those bounds.
			for(double sy = csy; sy < maxsy; sy++)
				for(double sx = csx; sx < maxsx; sx++) {
					// Calculate width, height, then area of dest pixel
					// that's covered by this source pixel.

					double pw = 1.0;
					if(minsx > sx)
						pw += sx - minsx;
					if(maxsx < (sx + 1))
						pw += maxsx - sx - 1;
					double ph = 1.0;
					if(minsy > sy)
						ph += sy - minsy;
					if(maxsy < (sy + 1))
						ph += maxsy - sy - 1;
					const double pa = pw * ph;

					// Get source pixel and add it to totals, weighted
					// by covered area and alpha.
					pxl = src->getPixel(sx, sy);
					area += pa;
					ra += pa * pxl.getRed();
					ga += pa * pxl.getGreen();
					ba += pa * pxl.getBlue();
					aa += pa * pxl.getAlpha();
				}

			// Set the destination image pixel to the average color.
			if(area > 0) {
				pxl.setRed(ra / area + 0.5);
				pxl.setGreen(ga / area + 0.5);
				pxl.setBlue(ba / area + 0.5);
				pxl.setAlpha(aa / area + 0.5);
			} else {
				pxl.setRed(0);
				pxl.setGreen(0);
				pxl.setBlue(0);
				pxl.setAlpha(0);
			}
			dest->setPixel(dx, dy, pxl);
		}
}
/* Get a cached, high-quality pre-scaled texture for display purposes.  If the
 * texture is not already cached, attempt to create it.  Returns a pre-scaled texture,
 * or the original texture if unable to pre-scale it.
 */
irr::video::ITexture* ImageManager::guiScalingResizeCached(irr::video::ITexture* src, const irr::core::rect<irr::s32> &srcrect,
											const irr::core::rect<irr::s32> &destrect) {
	if(!src)
		return src;

	const auto& origname = src->getName().getPath();
	// Calculate scaled texture name.
	const auto scale_name = epro::format(EPRO_TEXT("{}@guiScalingFilter:{}:{}:{}:{}:{}:{}"),
						 origname,
						 srcrect.UpperLeftCorner.X,
						 srcrect.UpperLeftCorner.Y,
						 srcrect.getWidth(),
						 srcrect.getHeight(),
						 destrect.getWidth(),
						 destrect.getHeight());

	// Search for existing scaled texture.
	irr::video::ITexture*& scaled = g_txrCache[scale_name];
	if(scaled)
		return scaled;

	// Try to find the texture converted to an image in the cache.
	// If the image was not found, try to extract it from the texture.
	irr::video::IImage* srcimg = g_imgCache[origname];
	if(!srcimg) {
		srcimg = driver->createImageFromData(src->getColorFormat(),
											 src->getSize(), src->lock(), false);
		src->unlock();
		g_imgCache[origname] = srcimg;
	}

	// Create a new destination image and scale the source into it.
	imageCleanTransparent(srcimg, 0);
	irr::video::IImage* destimg = driver->createImage(src->getColorFormat(),
													  irr::core::dimension2d<irr::u32>((irr::u32)destrect.getWidth(),
													 (irr::u32)destrect.getHeight()));
	imageScaleNNAAUnthreaded(srcimg, srcrect, destimg);

	// Some platforms are picky about textures being powers of 2, so expand
	// the image dimensions to the next power of 2, if necessary.
	if(!hasNPotSupport(driver)) {
		irr::video::IImage *po2img = driver->createImage(src->getColorFormat(),
														 irr::core::dimension2d<irr::u32>(npot2((irr::u32)destrect.getWidth()),
																		   npot2((irr::u32)destrect.getHeight())));
		po2img->fill(irr::video::SColor(0, 0, 0, 0));
		destimg->copyTo(po2img);
		destimg->drop();
		destimg = po2img;
	}

	// Convert the scaled image back into a texture.
	scaled = driver->addTexture({ scale_name.data(), static_cast<irr::u32>(scale_name.size()) }, destimg);
	destimg->drop();

	return scaled;
}
void ImageManager::draw2DImageFilterScaled(irr::video::ITexture* txr,
							 const irr::core::rect<irr::s32>& destrect, const irr::core::rect<irr::s32>& srcrect,
							 const irr::core::rect<irr::s32>* cliprect, const irr::video::SColor* const colors,
							 bool usealpha) {
	// Attempt to pre-scale image in software in high quality.
	irr::video::ITexture* scaled = guiScalingResizeCached(txr, srcrect, destrect);
	if(!scaled)
		return;

	// Correct source rect based on scaled image.
	const auto mysrcrect = (scaled != txr)
		? irr::core::rect<irr::s32>(0, 0, destrect.getWidth(), destrect.getHeight())
		: srcrect;

	driver->draw2DImage(scaled, destrect, mysrcrect, cliprect, colors, usealpha);
}
irr::video::IImage* ImageManager::GetScaledImageFromFile(const irr::io::path& file, int width, int height) {
	if(width <= 0 || height <= 0)
		return nullptr;

	auto* srcimg = driver->createImageFromFile(file);
	if(!srcimg)
		return nullptr;

	const irr::core::dimension2d<irr::u32> dim(width, height);
	const auto& srcdim = srcimg->getDimension();
	if(srcdim == dim) {
		return srcimg;
	} else {
		auto* destimg = driver->createImage(srcimg->getColorFormat(), dim);
		imageScaleNNAAUnthreaded(srcimg, { 0, 0, (irr::s32)srcdim.Width, (irr::s32)srcdim.Height }, destimg);
		srcimg->drop();
		return destimg;
	}
}

}
