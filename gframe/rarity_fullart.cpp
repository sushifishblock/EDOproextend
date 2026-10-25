#include "rarity_fullart.h"
#include <algorithm>
#include <string>
#include <unordered_map>
#include <filesystem>
#include <IImage.h>
#include <IReadFile.h>
#include "fmt.h"
#include "fullart.h"
#include "texture_rgb.h"
#include "game.h"
#include "image_downloader.h"
#include "image_manager.h"
#include "utils.h"

namespace ygo {

namespace {
using namespace irr;

constexpr size_t MAX_CARDS = 24;
constexpr int MAX_WIDTH = 1024; //the card's own resolution, up to this

struct Entry {
	video::ITexture* texture = nullptr;
	const video::ITexture* source = nullptr; //the card texture it was made from
	u32 source_width = 0;
	bool original = false; //made from the picture file itself, not from the (smaller) texture the game shows
	uint32_t next_try = 0;
	uint64_t last_use = 0;
};
std::unordered_map<uint32_t, Entry> cache;
uint64_t use_tick = 0;
int texture_counter = 0;

void Free(video::IVideoDriver* driver, Entry& entry) {
	if(entry.texture && driver)
		driver->removeTexture(entry.texture);
	entry.texture = nullptr;
}

//the picture file of a card at its full size (nullptr if there is none yet)
video::IImage* OpenOriginal(video::IVideoDriver* driver, uint32_t code) {
	if(gImageDownloader && gImageDownloader->GetDownloadStatus(code, imgType::ART) == ImageDownloader::downloadStatus::DOWNLOADED) {
		const auto file = gImageDownloader->GetDownloadPath(code, imgType::ART);
		return driver->createImageFromFile(io::path{ file.data(), static_cast<u32>(file.size()) });
	}
	for(const auto& dir : mainGame->pic_dirs) {
		for(auto extension : { EPRO_TEXT(".png"), EPRO_TEXT(".jpg") }) {
			if(dir == EPRO_TEXT("archives")) {
				auto* archive_file = Utils::FindFileInArchives(EPRO_TEXT("pics/"), epro::format(EPRO_TEXT("{}{}"), code, extension));
				if(!archive_file)
					continue;
				auto* image = driver->createImageFromFile(archive_file);
				archive_file->drop();
				if(image)
					return image;
			} else {
				const auto file = epro::format(EPRO_TEXT("{}{}{}"), dir, code, extension);
				std::error_code ec;
				if(!std::filesystem::exists(std::filesystem::path(file), ec))
					continue;
				if(auto* image = driver->createImageFromFile(io::path{ file.data(), static_cast<u32>(file.size()) }))
					return image;
			}
		}
	}
	return nullptr;
}

bool ReadRgb(video::IImage* image, RgbImage& out) {
	const auto size = image->getDimension();
	if(size.Width < 48 || size.Height < 64)
		return false;
	out.width = static_cast<int>(size.Width);
	out.height = static_cast<int>(size.Height);
	out.rgb.resize(static_cast<size_t>(out.width) * out.height * 3);
	uint8_t* o = out.rgb.data();
	if(image->getColorFormat() == video::ECF_A8R8G8B8) {
		const auto* data = static_cast<const uint8_t*>(image->getData());
		const u32 pitch = image->getPitch();
		for(int y = 0; y < out.height; ++y) {
			const uint8_t* row = data + static_cast<size_t>(y) * pitch;
			for(int x = 0; x < out.width; ++x, o += 3) {
				o[0] = row[x * 4 + 2]; o[1] = row[x * 4 + 1]; o[2] = row[x * 4];
			}
		}
		return true;
	}
	for(int y = 0; y < out.height; ++y)
		for(int x = 0; x < out.width; ++x, o += 3) {
			const video::SColor c = image->getPixel(x, y);
			o[0] = static_cast<uint8_t>(c.getRed()); o[1] = static_cast<uint8_t>(c.getGreen()); o[2] = static_cast<uint8_t>(c.getBlue());
		}
	return true;
}

//card: the card's picture; from_file: it came from the picture file at full size
void Build(video::IVideoDriver* driver, uint32_t code, Entry& entry, const RgbImage& card, bool from_file) {
	const RgbaImage made = MakeFullArt(card, std::min(card.width, MAX_WIDTH));
	if(made.Empty())
		return;
	auto* image = driver->createImage(video::ECF_A8R8G8B8, core::dimension2d<u32>(made.width, made.height));
	if(!image)
		return;
	for(int y = 0; y < made.height; ++y)
		for(int x = 0; x < made.width; ++x) {
			const uint8_t* p = &made.rgba[(static_cast<size_t>(y) * made.width + x) * 4];
			image->setPixel(x, y, video::SColor(p[3], p[0], p[1], p[2]));
		}
	const bool mips = driver->getTextureCreationFlag(video::ETCF_CREATE_MIP_MAPS);
	driver->setTextureCreationFlag(video::ETCF_CREATE_MIP_MAPS, true);
	const std::string name = "fullart_" + std::to_string(code) + "_" + std::to_string(++texture_counter);
	auto* texture = driver->addTexture(io::path(name.c_str()), image);
	driver->setTextureCreationFlag(video::ETCF_CREATE_MIP_MAPS, mips);
	image->drop();
	if(!texture)
		return;
	Free(driver, entry);
	entry.texture = texture;
	entry.original = from_file;
}

void Trim(video::IVideoDriver* driver) {
	while(cache.size() > MAX_CARDS) {
		auto oldest = cache.begin();
		for(auto it = cache.begin(); it != cache.end(); ++it)
			if(it->second.last_use < oldest->second.last_use)
				oldest = it;
		Free(driver, oldest->second);
		cache.erase(oldest);
	}
}
}

video::ITexture* FullArtFor(video::IVideoDriver* driver, uint32_t code, uint32_t now_ms) {
	if(!code || !driver || !mainGame)
		return nullptr;
	auto& entry = cache[code];
	entry.last_use = ++use_tick;
	if(now_ms < entry.next_try)
		return entry.texture;
	//a picture takes about 0.2 s to make: only one in a while, so a screen full of new cards fills in over a few seconds instead of freezing
	static uint32_t last_build = 0;
	if(now_ms - last_build < 60 && last_build != 0)
		return entry.texture;
	if(!entry.original) {
		//the picture file at full size, when there is one
		if(auto* image = OpenOriginal(driver, code)) {
			RgbImage card;
			const bool read = ReadRgb(image, card);
			image->drop();
			if(read) {
				Build(driver, code, entry, card, true);
				last_build = now_ms;
				entry.next_try = entry.original ? 0xffffffffu : now_ms + 5000; //made at full size: nothing better to wait for
				Trim(driver);
				return entry.texture;
			}
		}
	}
	auto* source = mainGame->imageManager.GetTextureCard(code, imgType::ART);
	if(!source || source == mainGame->imageManager.tUnknown) {
		entry.next_try = now_ms + 250; //the picture is still loading
		return entry.texture;
	}
	//no file to read: made from the texture the game shows, and again when a much sharper one has been loaded
	entry.next_try = now_ms + 1000;
	if(!entry.texture || (source != entry.source && source->getOriginalSize().Width > entry.source_width * 12 / 10)) {
		RgbImage card;
		if(ReadTextureRgb(source, card)) {
			Build(driver, code, entry, card, false);
			last_build = now_ms;
			entry.source = source;
			entry.source_width = source->getOriginalSize().Width;
		}
		Trim(driver);
	}
	return entry.texture;
}

void ReleaseFullArts(video::IVideoDriver* driver_or_null) {
	for(auto& item : cache)
		Free(driver_or_null, item.second);
	cache.clear();
}

}
