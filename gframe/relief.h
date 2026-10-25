#ifndef RELIEF_H
#define RELIEF_H

#include <cstdint>
#include <vector>

namespace ygo {

//The Ultimate Rare effect: the picture of a card as an embossed relief (its shapes are raised in steps, so their outlines stand up), lit from any direction.
//Pure pixel code, no graphics library.
constexpr int RELIEF_SIZE = 128;

struct ReliefMap {
	std::vector<float> gx, gy; //slope of the relief, RELIEF_SIZE x RELIEF_SIZE
	bool Empty() const { return gx.empty(); }
};

//luma: RELIEF_SIZE x RELIEF_SIZE brightness values 0..1 of the card picture
ReliefMap MakeRelief(const std::vector<float>& luma);
//RGBA bytes (RELIEF_SIZE * RELIEF_SIZE * 4, not premultiplied) of the relief lit from `angle` (radians):
//bright rims on the sides that face the light (tinted by the way they face), dark rims on the sides away from it
void ShadeRelief(const ReliefMap& relief, float angle, std::vector<uint8_t>& rgba);

}

#endif
