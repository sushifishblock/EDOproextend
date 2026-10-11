#ifndef ART_FIT_H
#define ART_FIT_H

#include <cstdint>
#include <vector>

namespace ygo {

//Cutting a picture to the shape of a sleeve or playmat (pure pixel code, no graphics library).
struct Pixels {
	int w = 0;
	int h = 0;
	std::vector<uint8_t> rgba; //4 bytes per pixel: R, G, B, A
	bool Empty() const { return w <= 0 || h <= 0 || rgba.size() != static_cast<size_t>(w) * h * 4; }
};

struct ArtParams {
	bool fit = false; //false: fill the whole area (the edges are cropped), true: show the whole picture (blurred background)
	float zoom = 1.0f; //0.5 - 4
	float ox = 0.0f; //-1 .. 1: slide the picture sideways inside the frame
	float oy = 0.0f; //-1 .. 1: slide up / down
	float bright = 1.0f; //0.3 - 1.5
	float border = 0.0f; //black border as part of the width (0 = none)
};

//the picture cut to out_w x out_h. Everything is relative to the size, so a small preview looks like the real result.
Pixels FitArt(const Pixels& src, int out_w, int out_h, const ArtParams& params);
//the same picture with its longest side at most `max_side` (never enlarged)
Pixels ShrinkTo(const Pixels& src, int max_side);

}

#endif
