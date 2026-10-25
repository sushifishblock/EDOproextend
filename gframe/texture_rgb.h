#ifndef TEXTURE_RGB_H
#define TEXTURE_RGB_H

#include <irrlicht.h>
#include "fullart.h"

namespace ygo {

//the pixels of a card texture as RGB; false if it is too small for a card or has a format that is not read
bool ReadTextureRgb(irr::video::ITexture* texture, RgbImage& out);

}

#endif
