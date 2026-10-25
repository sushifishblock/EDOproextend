#ifndef RARITY_FULLART_H
#define RARITY_FULLART_H

#include <cstdint>
#include <irrlicht.h>

namespace ygo {

//The enlarged-picture version of a card (Full Art Rare), as one texture the size of the card.
//Built the first time a card is shown with a FULLART layer, never for cards that do not have such a rarity.
//nullptr while the picture of the card is not loaded yet (asked again a little later)
irr::video::ITexture* FullArtFor(irr::video::IVideoDriver* driver, uint32_t code, uint32_t now_ms);
//the textures are gone (the video driver was reset) or should be freed
void ReleaseFullArts(irr::video::IVideoDriver* driver_or_null);

}

#endif
