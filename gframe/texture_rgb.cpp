#include "texture_rgb.h"

namespace ygo {

bool ReadTextureRgb(irr::video::ITexture* texture, RgbImage& out) {
	const auto size = texture->getOriginalSize();
	if(size.Width < 48 || size.Height < 64)
		return false;
	const auto format = texture->getColorFormat();
	if(format != irr::video::ECF_A8R8G8B8 && format != irr::video::ECF_R8G8B8 && format != irr::video::ECF_A1R5G5B5 && format != irr::video::ECF_R5G6B5)
		return false;
	const auto* data = static_cast<const uint8_t*>(texture->lock(irr::video::ETLM_READ_ONLY));
	if(!data)
		return false;
	const irr::u32 pitch = texture->getPitch();
	out.width = static_cast<int>(size.Width);
	out.height = static_cast<int>(size.Height);
	out.rgb.resize(static_cast<size_t>(out.width) * out.height * 3);
	for(int y = 0; y < out.height; ++y) {
		const uint8_t* row = data + static_cast<size_t>(y) * pitch;
		uint8_t* o = &out.rgb[static_cast<size_t>(y) * out.width * 3];
		for(int x = 0; x < out.width; ++x, o += 3) {
			switch(format) {
			case irr::video::ECF_A8R8G8B8:
				o[0] = row[x * 4 + 2]; o[1] = row[x * 4 + 1]; o[2] = row[x * 4];
				break;
			case irr::video::ECF_R8G8B8:
				o[0] = row[x * 3 + 2]; o[1] = row[x * 3 + 1]; o[2] = row[x * 3];
				break;
			case irr::video::ECF_A1R5G5B5: {
				const uint16_t v = reinterpret_cast<const uint16_t*>(row)[x];
				o[0] = ((v >> 10) & 31) * 255 / 31; o[1] = ((v >> 5) & 31) * 255 / 31; o[2] = (v & 31) * 255 / 31;
				break;
			}
			default: {
				const uint16_t v = reinterpret_cast<const uint16_t*>(row)[x];
				o[0] = ((v >> 11) & 31) * 255 / 31; o[1] = ((v >> 5) & 63) * 255 / 63; o[2] = (v & 31) * 255 / 31;
				break;
			}
			}
		}
	}
	texture->unlock();
	return true;
}

}
