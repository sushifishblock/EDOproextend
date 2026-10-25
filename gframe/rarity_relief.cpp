#include "rarity_relief.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>
#include "game.h"
#include "image_manager.h"
#include "relief.h"
#include "texture_rgb.h"

namespace ygo {

namespace {
using namespace irr;

//the picture area of a card as parts of the whole card (the same as the ART region of the rarity effects)
constexpr float ART_X0 = 0.112f, ART_X1 = 0.888f, ART_Y0 = 0.180f, ART_Y1 = 0.711f;
constexpr size_t MAX_CARDS = 96;

struct Entry {
	ReliefSet set;
	bool built = false;
	const video::ITexture* source = nullptr; //the card texture it was made from
	u32 source_width = 0;
	uint32_t next_try = 0;
	uint64_t last_use = 0;
};
std::unordered_map<uint32_t, Entry> cache;
uint64_t use_tick = 0;
int texture_counter = 0;

void Free(video::IVideoDriver* driver, Entry& entry) {
	for(auto& texture : entry.set.direction) {
		if(texture && driver)
			driver->removeTexture(texture);
		texture = nullptr;
	}
	entry.built = false;
}

//the brightness of the picture area, RELIEF_SIZE x RELIEF_SIZE
bool ReadLuma(video::ITexture* texture, std::vector<float>& luma) {
	RgbImage card;
	if(!ReadTextureRgb(texture, card) || card.width < 24 || card.height < 24)
		return false;
	const float x0 = card.width * ART_X0, y0 = card.height * ART_Y0;
	const float cell_w = card.width * (ART_X1 - ART_X0) / RELIEF_SIZE, cell_h = card.height * (ART_Y1 - ART_Y0) / RELIEF_SIZE;
	const int nx = std::clamp(static_cast<int>(std::ceil(cell_w)), 1, 4), ny = std::clamp(static_cast<int>(std::ceil(cell_h)), 1, 4);
	luma.assign(static_cast<size_t>(RELIEF_SIZE) * RELIEF_SIZE, 0.0f);
	for(int cy = 0; cy < RELIEF_SIZE; ++cy) {
		for(int cx = 0; cx < RELIEF_SIZE; ++cx) {
			float sum = 0;
			for(int j = 0; j < ny; ++j) {
				for(int i = 0; i < nx; ++i) {
					const int sx = std::clamp(static_cast<int>(x0 + (cx + (i + 0.5f) / nx) * cell_w), 0, card.width - 1);
					const int sy = std::clamp(static_cast<int>(y0 + (cy + (j + 0.5f) / ny) * cell_h), 0, card.height - 1);
					const uint8_t* p = &card.rgb[(static_cast<size_t>(sy) * card.width + sx) * 3];
					sum += 0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2];
				}
			}
			luma[static_cast<size_t>(cy) * RELIEF_SIZE + cx] = sum / (nx * ny) / 255.0f;
		}
	}
	return true;
}

void Build(video::IVideoDriver* driver, uint32_t code, Entry& entry, video::ITexture* source) {
	std::vector<float> luma;
	if(!ReadLuma(source, luma))
		return;
	const ReliefMap relief = MakeRelief(luma);
	if(relief.Empty())
		return;
	Free(driver, entry);
	const bool mips = driver->getTextureCreationFlag(video::ETCF_CREATE_MIP_MAPS);
	driver->setTextureCreationFlag(video::ETCF_CREATE_MIP_MAPS, false);
	std::vector<uint8_t> rgba;
	bool ok = true;
	for(int k = 0; k < 8 && ok; ++k) {
		ShadeRelief(relief, k * 3.14159265f / 4.0f, rgba);
		auto* image = driver->createImage(video::ECF_A8R8G8B8, core::dimension2d<u32>(RELIEF_SIZE, RELIEF_SIZE));
		if(!image) {
			ok = false;
			break;
		}
		for(int y = 0; y < RELIEF_SIZE; ++y)
			for(int x = 0; x < RELIEF_SIZE; ++x) {
				const uint8_t* p = &rgba[(static_cast<size_t>(y) * RELIEF_SIZE + x) * 4];
				image->setPixel(x, y, video::SColor(p[3], p[0], p[1], p[2]));
			}
		const std::string name = "relief_" + std::to_string(code) + "_" + std::to_string(++texture_counter);
		entry.set.direction[k] = driver->addTexture(io::path(name.c_str()), image);
		image->drop();
		ok = entry.set.direction[k] != nullptr;
	}
	driver->setTextureCreationFlag(video::ETCF_CREATE_MIP_MAPS, mips);
	if(!ok) {
		Free(driver, entry);
		return;
	}
	entry.built = true;
	entry.source = source;
	entry.source_width = source->getOriginalSize().Width;
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

const ReliefSet* ReliefFor(video::IVideoDriver* driver, uint32_t code, uint32_t now_ms) {
	if(!code || !driver || !mainGame)
		return nullptr;
	auto& entry = cache[code];
	entry.last_use = ++use_tick;
	if(now_ms < entry.next_try)
		return entry.built ? &entry.set : nullptr;
	auto* source = mainGame->imageManager.GetTextureCard(code, imgType::ART);
	if(!source || source == mainGame->imageManager.tUnknown) {
		entry.next_try = now_ms + 250; //the picture is still loading
		return entry.built ? &entry.set : nullptr;
	}
	//made from the first picture there is; made again when a much sharper picture of the card has been loaded
	if(!entry.built || (source != entry.source && source->getOriginalSize().Width > entry.source_width * 12 / 10)) {
		Build(driver, code, entry, source);
		entry.next_try = now_ms + (entry.built ? 500 : 1000);
		Trim(driver);
	}
	return entry.built ? &entry.set : nullptr;
}

void ReleaseReliefs(video::IVideoDriver* driver_or_null) {
	for(auto& item : cache)
		Free(driver_or_null, item.second);
	cache.clear();
}

}
