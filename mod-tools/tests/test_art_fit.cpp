#include "../../gframe/art_fit.h"
#include <cstdio>
using namespace ygo;
static int failures = 0;
static void Expect(const char* name, bool ok) {
	std::printf("%s %s\n", ok ? "ok  " : "FAIL", name);
	if(!ok)
		++failures;
}
//a picture, left half red, right half blue
static Pixels TwoColors(int w, int h) {
	Pixels p;
	p.w = w;
	p.h = h;
	p.rgba.resize(static_cast<size_t>(w) * h * 4, 255);
	for(int y = 0; y < h; ++y)
		for(int x = 0; x < w; ++x) {
			uint8_t* px = &p.rgba[(static_cast<size_t>(y) * w + x) * 4];
			px[0] = x < w / 2 ? 255 : 0;
			px[1] = 0;
			px[2] = x < w / 2 ? 0 : 255;
		}
	return p;
}
static const uint8_t* At(const Pixels& p, int x, int y) { return &p.rgba[(static_cast<size_t>(y) * p.w + x) * 4]; }
int main() {
	const Pixels src = TwoColors(400, 200); //wide
	ArtParams fill;
	const Pixels a = FitArt(src, 100, 140, fill);
	Expect("size is the wanted size", a.w == 100 && a.h == 140 && !a.Empty());
	Expect("fill covers everything (no black)", At(a, 2, 2)[0] == 255 && At(a, 97, 137)[2] == 255);
	ArtParams zoomed = fill;
	zoomed.zoom = 2.0f;
	const Pixels b = FitArt(src, 100, 140, zoomed);
	Expect("zoom keeps the middle", At(b, 40, 70)[0] > 200 && At(b, 60, 70)[2] > 200);
	ArtParams moved = fill;
	moved.ox = 1.0f; //the picture slides right: the left edge of the frame shows more of the left
	const Pixels c = FitArt(src, 100, 140, moved);
	Expect("moving shows another part", At(c, 50, 70)[0] > 200 && At(a, 50, 70)[2] > 200);
	ArtParams fit;
	fit.fit = true;
	const Pixels d = FitArt(src, 100, 140, fit);
	Expect("fit shows the whole picture in the middle", At(d, 10, 70)[0] > 200 && At(d, 90, 70)[2] > 200);
	Expect("fit fills the top with a darker blurred copy", At(d, 10, 5)[0] > 20 && At(d, 10, 5)[0] < 200);
	ArtParams dark = fill;
	dark.bright = 0.5f;
	const Pixels e = FitArt(src, 100, 140, dark);
	Expect("brightness", At(e, 10, 70)[0] > 120 && At(e, 10, 70)[0] < 135);
	ArtParams framed = fill;
	framed.border = 0.05f;
	const Pixels f = FitArt(src, 100, 140, framed);
	Expect("border is black, inside is not", At(f, 0, 70)[0] == 0 && At(f, 4, 70)[0] == 0 && At(f, 10, 70)[0] == 255);
	const Pixels small = ShrinkTo(src, 100);
	Expect("shrink keeps the shape", small.w == 100 && small.h == 50);
	Expect("shrink never enlarges", ShrinkTo(small, 500).w == 100);
	Expect("an empty picture gives a black picture of the right size", FitArt(Pixels{}, 10, 10, fill).w == 10);
	std::printf(failures ? "FAILED\n" : "ALL OK\n");
	return failures ? 1 : 0;
}
