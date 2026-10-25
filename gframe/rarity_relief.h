#ifndef RARITY_RELIEF_H
#define RARITY_RELIEF_H

#include <cstdint>
#include <irrlicht.h>

namespace ygo {

//The embossed relief of a card's picture (Ultimate Rare), lit from 8 directions as small textures.
//Built the first time a card is shown with an OUTLINE layer, never for cards that do not have such a rarity.
struct ReliefSet {
	irr::video::ITexture* direction[8] = {};
};

//nullptr while the picture of the card is not loaded yet (asked again a little later)
const ReliefSet* ReliefFor(irr::video::IVideoDriver* driver, uint32_t code, uint32_t now_ms);
//the textures are gone (the video driver was reset) or should be freed
void ReleaseReliefs(irr::video::IVideoDriver* driver_or_null);

}

#endif
