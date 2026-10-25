#include "../../gframe/fullart.h"
#include <cstdio>
#include <cstdlib>

using namespace ygo;

static int failures = 0;
#define CHECK(cond) do { if(!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++failures; } } while(0)

//a fake card 400 x 583: orange frame, blue picture area, black "name letters", beige text box
static RgbImage FakeCard() {
	RgbImage img;
	img.width = 400; img.height = 583;
	img.rgb.assign(static_cast<size_t>(400) * 583 * 3, 0);
	auto put = [&](int x, int y, int r, int g, int b) {
		uint8_t* p = &img.rgb[(static_cast<size_t>(y) * 400 + x) * 3];
		p[0] = r; p[1] = g; p[2] = b;
	};
	for(int y = 0; y < 583; ++y)
		for(int x = 0; x < 400; ++x) {
			if(y > 0.743 * 583) put(x, y, 240, 225, 200); //text box
			else if(x > 0.112 * 400 && x < 0.888 * 400 && y > 0.18 * 583 && y < 0.711 * 583) put(x, y, 30, 60, 200); //picture
			else put(x, y, 200, 120, 40); //frame
		}
	for(int y = 40; y < 56; ++y)
		for(int x = 40; x < 200; ++x)
			put(x, y, 0, 0, 0); //the name
	return img;
}

int main() {
	CHECK(MakeFullArt(RgbImage(), 300).Empty());
	const RgbImage card = FakeCard();
	const RgbaImage out = MakeFullArt(card, 400);
	CHECK(!out.Empty());
	CHECK(out.width == 400 && out.height == 583);
	auto px = [&](int x, int y) { return &out.rgba[(static_cast<size_t>(y) * out.width + x) * 4]; };
	//the frame beside the picture is now the picture (blue)
	const uint8_t* side = px(25, 300);
	CHECK(side[2] > 150 && side[0] < 80 && side[3] == 255);
	//the name letters are still black
	const uint8_t* name = px(100, 48);
	CHECK(name[0] < 40 && name[1] < 40 && name[2] < 40);
	//the text box and the card's outer edge are not changed
	CHECK(px(200, 500)[3] == 0);
	CHECK(px(0, 0)[3] == 0);
	if(!failures)
		std::printf("ALL OK\n");
	return failures ? 1 : 0;
}
