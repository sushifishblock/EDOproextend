#ifndef FULLART_H
#define FULLART_H

#include <cstdint>
#include <vector>

namespace ygo {

//The Full Art Rare effect: the card's picture is enlarged over the whole card (behind the name, the level stars and the frame),
//only the name, the stars and the type line stay on top of it. The text box is left alone.
//Pure pixel code, no graphics library.
struct RgbImage {
	int width = 0, height = 0;
	std::vector<uint8_t> rgb; //3 bytes per pixel, R G B
};

struct RgbaImage {
	int width = 0, height = 0;
	std::vector<uint8_t> rgba; //4 bytes per pixel, not premultiplied
	bool Empty() const { return rgba.empty(); }
};

//card: the whole card as it is printed (frame, name, picture, text box). Gives the same card with the enlarged picture,
//transparent where the card is not changed (the text box and the outer edge). Empty if the card is too small.
RgbaImage MakeFullArt(const RgbImage& card, int out_width);

}

#endif
