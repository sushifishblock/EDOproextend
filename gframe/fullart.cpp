#include "fullart.h"
#include <algorithm>
#include <cmath>

namespace ygo {

namespace {
//where things are on a card, as parts of the whole card
constexpr float ART_X0 = 0.112f, ART_X1 = 0.888f, ART_Y0 = 0.180f, ART_Y1 = 0.711f;
constexpr float ART_INSET = 0.02f; //the rim of the picture is left out
constexpr float BAND = 0.18f; //the name, the stars, the badge and the type line are above this
constexpr float ZONE_X0 = 0.02f, ZONE_X1 = 0.98f, ZONE_Y0 = 0.015f, ZONE_Y1 = 0.743f; //what the enlarged picture covers

struct Plane {
	int w = 0, h = 0;
	std::vector<float> v;
	Plane(int width, int height, float fill = 0.0f) : w(width), h(height), v(static_cast<size_t>(width) * height, fill) {}
	float& at(int x, int y) { return v[static_cast<size_t>(y) * w + x]; }
	float at(int x, int y) const { return v[static_cast<size_t>(y) * w + x]; }
};

struct Pixels {
	int w = 0, h = 0;
	std::vector<float> c; //3 floats per pixel
	Pixels(int width, int height) : w(width), h(height), c(static_cast<size_t>(width) * height * 3, 0.0f) {}
	float* at(int x, int y) { return &c[(static_cast<size_t>(y) * w + x) * 3]; }
	const float* at(int x, int y) const { return &c[(static_cast<size_t>(y) * w + x) * 3]; }
};

void SourceAt(const RgbImage& src, float x, float y, float* out) {
	x = std::clamp(x - 0.5f, 0.0f, static_cast<float>(src.width - 1));
	y = std::clamp(y - 0.5f, 0.0f, static_cast<float>(src.height - 1));
	const int x0 = static_cast<int>(x), y0 = static_cast<int>(y);
	const int x1 = std::min(x0 + 1, src.width - 1), y1 = std::min(y0 + 1, src.height - 1);
	const float fx = x - x0, fy = y - y0;
	for(int k = 0; k < 3; ++k) {
		const auto p = [&](int px, int py) { return static_cast<float>(src.rgb[(static_cast<size_t>(py) * src.width + px) * 3 + k]); };
		out[k] = (p(x0, y0) * (1 - fx) + p(x1, y0) * fx) * (1 - fy) + (p(x0, y1) * (1 - fx) + p(x1, y1) * fx) * fy;
	}
}

//a part of the source (given as parts of the whole source) resampled to out_w x out_h, averaging a few samples per pixel when it shrinks
Pixels Resample(const RgbImage& src, float u0, float v0, float u1, float v1, int out_w, int out_h) {
	Pixels out(out_w, out_h);
	const float sx = (u1 - u0) * src.width / out_w, sy = (v1 - v0) * src.height / out_h;
	const int nx = std::clamp(static_cast<int>(std::ceil(sx)), 1, 3), ny = std::clamp(static_cast<int>(std::ceil(sy)), 1, 3);
	for(int y = 0; y < out_h; ++y) {
		for(int x = 0; x < out_w; ++x) {
			float sum[3] = { 0, 0, 0 };
			for(int j = 0; j < ny; ++j) {
				for(int i = 0; i < nx; ++i) {
					float p[3];
					SourceAt(src, u0 * src.width + (x + (i + 0.5f) / nx) * sx, v0 * src.height + (y + (j + 0.5f) / ny) * sy, p);
					sum[0] += p[0]; sum[1] += p[1]; sum[2] += p[2];
				}
			}
			float* o = out.at(x, y);
			o[0] = sum[0] / (nx * ny); o[1] = sum[1] / (nx * ny); o[2] = sum[2] / (nx * ny);
		}
	}
	return out;
}

//3x3 average
Plane Blur3(const Plane& in) {
	Plane out(in.w, in.h);
	for(int y = 0; y < in.h; ++y) {
		for(int x = 0; x < in.w; ++x) {
			float sum = 0;
			for(int j = -1; j <= 1; ++j)
				for(int i = -1; i <= 1; ++i)
					sum += in.at(std::clamp(x + i, 0, in.w - 1), std::clamp(y + j, 0, in.h - 1));
			out.at(x, y) = sum / 9.0f;
		}
	}
	return out;
}

//grow the set of 1s by one pixel (up, down, left, right)
Plane Grow(const Plane& in) {
	Plane out = in;
	for(int y = 0; y < in.h; ++y)
		for(int x = 0; x < in.w; ++x)
			if(in.at(x, y) > 0.5f) {
				if(x > 0) out.at(x - 1, y) = 1;
				if(x + 1 < in.w) out.at(x + 1, y) = 1;
				if(y > 0) out.at(x, y - 1) = 1;
				if(y + 1 < in.h) out.at(x, y + 1) = 1;
			}
	return out;
}

//the widths below were made for a card 400 pixels wide; bigger cards repeat the steps in proportion
int Scale(int width) {
	return std::max(1, static_cast<int>(std::lround(width / 400.0)));
}

Plane GrowBy(Plane in, int steps) {
	for(int i = 0; i < steps; ++i)
		in = Grow(in);
	return in;
}

//shrink the set of 1s by `steps` pixels
Plane ShrinkBy(const Plane& in, int steps) {
	Plane outside = in;
	for(auto& v : outside.v)
		v = v > 0.5f ? 0.0f : 1.0f;
	outside = GrowBy(outside, steps);
	for(auto& v : outside.v)
		v = v > 0.5f ? 0.0f : 1.0f;
	return outside;
}

Plane BlurBy(Plane in, int passes) {
	for(int i = 0; i < passes; ++i)
		in = Blur3(in);
	return in;
}

//Every shape of 1s (4-connected) with at least min_area pixels is replaced by filled circles (the level stars are round orbs, the detected
//shape is ragged at its dark rim): a row of orbs that touch is one shape, `pitch` pixels from one orb to the next. Smaller shapes are dropped. `shrink` makes up for the growing that was done before to close gaps.
void RoundShapes(Plane& mask, size_t min_area, float shrink, float pitch) {
	std::vector<char> seen(mask.v.size(), 0);
	std::vector<size_t> stack;
	struct Orb { float x, y, r; };
	std::vector<Orb> orbs;
	for(size_t start = 0; start < mask.v.size(); ++start) {
		if(mask.v[start] < 0.5f || seen[start])
			continue;
		size_t area = 0;
		int min_x = mask.w, max_x = 0, min_y = mask.h, max_y = 0;
		stack.assign(1, start);
		seen[start] = 1;
		while(!stack.empty()) {
			const size_t at = stack.back();
			stack.pop_back();
			const int x = static_cast<int>(at % mask.w), y = static_cast<int>(at / mask.w);
			++area;
			min_x = std::min(min_x, x); max_x = std::max(max_x, x); min_y = std::min(min_y, y); max_y = std::max(max_y, y);
			const size_t next[4] = { x > 0 ? at - 1 : at, x + 1 < mask.w ? at + 1 : at, y > 0 ? at - mask.w : at, y + 1 < mask.h ? at + mask.w : at };
			for(const size_t n : next)
				if(!seen[n] && mask.v[n] >= 0.5f) {
					seen[n] = 1;
					stack.push_back(n);
				}
		}
		if(area >= min_area) {
			const float width = static_cast<float>(max_x - min_x + 1), height = static_cast<float>(max_y - min_y + 1);
			const int count = std::max(1, static_cast<int>(std::lround((width - height) / pitch)) + 1);
			for(int i = 0; i < count; ++i)
				orbs.push_back({ count == 1 ? min_x + width / 2.0f : min_x + height / 2.0f + i * (width - height) / (count - 1), min_y + height / 2.0f, height / 2.0f - shrink });
		}
	}
	std::fill(mask.v.begin(), mask.v.end(), 0.0f);
	for(const auto& orb : orbs)
		for(int y = std::max(0, static_cast<int>(orb.y - orb.r) - 1); y <= std::min(mask.h - 1, static_cast<int>(orb.y + orb.r) + 1); ++y)
			for(int x = std::max(0, static_cast<int>(orb.x - orb.r) - 1); x <= std::min(mask.w - 1, static_cast<int>(orb.x + orb.r) + 1); ++x)
				if((x - orb.x) * (x - orb.x) + (y - orb.y) * (y - orb.y) <= orb.r * orb.r)
					mask.at(x, y) = 1;
}

float Median(std::vector<float>& values) {
	const auto mid = values.begin() + values.size() / 2;
	std::nth_element(values.begin(), mid, values.end());
	return *mid;
}

//Letters printed on a plate (the name, the type line): lifted off it, so they can be put on the picture with a soft glow of the opposite
//brightness round them. Dark or light letters is decided by which kind of strongly different pixels there are more of in the band.
struct Letters {
	Plane alpha, halo;
	float color[3] = { 30, 20, 15 };
	float halo_color[3] = { 245, 235, 215 };
	Letters(int w, int h) : alpha(w, h), halo(w, h) {}
};

Letters LiftLetters(const Pixels& card, int rows, float x0, float x1, float y0, float y1, const Plane* leave_out = nullptr) {
	Letters out(card.w, rows);
	const int ix0 = static_cast<int>(x0 * card.w), ix1 = static_cast<int>(x1 * card.w);
	const int iy0 = static_cast<int>(y0 * card.h), iy1 = static_cast<int>(y1 * card.h);
	if(ix1 <= ix0 || iy1 <= iy0)
		return out;
	const auto luma_at = [&](int x, int y) {
		const float* p = card.at(x, y);
		return 0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2];
	};
	std::vector<float> lumas;
	for(int y = iy0; y < iy1; ++y)
		for(int x = ix0; x < ix1; ++x)
			lumas.push_back(luma_at(x, y));
	const float plate = Median(lumas);
	size_t darker = 0, lighter = 0;
	for(float l : lumas) {
		if(l < plate - 45.0f) ++darker;
		else if(l > plate + 45.0f) ++lighter;
	}
	const bool dark_letters = darker >= lighter;
	if(!dark_letters) {
		out.color[0] = out.color[1] = out.color[2] = 240;
		out.halo_color[0] = 25; out.halo_color[1] = 15; out.halo_color[2] = 40;
	}
	const float span = std::max(40.0f, dark_letters ? plate - 25.0f : 240.0f - plate);
	constexpr float FLOOR = 24.0f; //the plate's own shading is not lettering
	for(int y = iy0; y < iy1; ++y)
		for(int x = ix0; x < ix1; ++x) {
			const float deviation = dark_letters ? plate - luma_at(x, y) : luma_at(x, y) - plate;
			out.alpha.at(x, y) = std::clamp((deviation - FLOOR) / (span - FLOOR), 0.0f, 1.0f) * (leave_out ? 1.0f - leave_out->at(x, y) : 1.0f);
		}
	//the glow: the letters spread out by two pixels and softened
	Plane core(card.w, rows);
	for(size_t i = 0; i < core.v.size(); ++i)
		core.v[i] = out.alpha.v[i] > 0.4f ? 1.0f : 0.0f;
	core = BlurBy(GrowBy(core, 2 * Scale(card.w)), 2 * Scale(card.w));
	for(size_t i = 0; i < core.v.size(); ++i)
		out.halo.v[i] = std::min(1.0f, core.v[i] * 1.6f) * 0.8f;
	return out;
}
}

RgbaImage MakeFullArt(const RgbImage& card, int out_width) {
	RgbaImage result;
	if(card.width < 48 || card.height < 64 || card.rgb.size() < static_cast<size_t>(card.width) * card.height * 3 || out_width < 48)
		return result;
	const int W = out_width;
	const int H = std::max(64, static_cast<int>(std::lround(static_cast<double>(W) * card.height / card.width)));
	const Pixels flat = Resample(card, 0, 0, 1, 1, W, H);
	//the picture enlarged until it covers everything above the text box (its sides are cropped a little)
	const int zone_h = static_cast<int>(ZONE_Y1 * H);
	const float au0 = ART_X0 + (ART_X1 - ART_X0) * ART_INSET, au1 = ART_X1 - (ART_X1 - ART_X0) * ART_INSET;
	const float av0 = ART_Y0 + (ART_Y1 - ART_Y0) * ART_INSET, av1 = ART_Y1 - (ART_Y1 - ART_Y0) * ART_INSET;
	const int side = std::max(zone_h, W);
	const int crop = (side - W) / 2;
	const float span = au1 - au0;
	const Pixels art = Resample(card, au0 + span * crop / side, av0, au0 + span * (crop + W) / side, av1, W, zone_h);
	//what stays of the old card on top of the picture
	const int rows = static_cast<int>(BAND * H) + 1; //everything that is kept of the old card lies in the top of it
	Plane ink(W, rows);
	//the level stars (round orbs), grown a little to take their rims
	Plane stars(W, rows);
	{
		const int x0 = static_cast<int>(0.075f * W), x1 = static_cast<int>(0.94f * W), y0 = static_cast<int>(0.120f * H), y1 = static_cast<int>(0.165f * H);
		for(int y = y0; y < y1; ++y)
			for(int x = x0; x < x1; ++x) {
				const float* p = flat.at(x, y);
				//a star is a round orb: bright orange and yellow on the lit side, deep red on the other (the plain frame is neither)
				const bool bright = p[0] > 225 && p[1] > 120, deep_red = p[0] > 100 && p[1] < 0.45f * p[0] && p[2] < 0.5f * p[0];
				if(bright || deep_red)
					stars.at(x, y) = 1;
			}
	}
	//a star is a round orb about 0.09 of the card wide: smaller bits of the same colors are the frame
	stars = GrowBy(stars, 2 * Scale(W));
	stars = GrowBy(ShrinkBy(stars, 7 * Scale(W)), 7 * Scale(W)); //thin streaks of the same colors vanish, the round orbs stay
	RoundShapes(stars, static_cast<size_t>(0.003 * W * W), 3.5f * Scale(W) - 1.5f, 0.0645f * W); //(2 pixels per scale step were added to close gaps)
	Plane star_area = GrowBy(stars, 5 * Scale(W)); //the stars and their rims are not lettering
	for(size_t i = 0; i < ink.v.size(); ++i)
		ink.v[i] = std::max(ink.v[i], stars.v[i]);
	const Letters name = LiftLetters(flat, rows, 0.065f, 0.82f, 0.053f, 0.104f);
	const Letters type_line = LiftLetters(flat, rows, 0.075f, 0.94f, 0.130f, 0.168f, &star_area);
	//the attribute: just its round badge, not the frame around it
	{
		const float cx = 0.88f * W, cy = 0.079f * H, radius = 0.048f * W;
		for(int y = std::max(0, static_cast<int>(cy - radius)); y < std::min(rows, static_cast<int>(cy + radius) + 1); ++y)
			for(int x = std::max(0, static_cast<int>(cx - radius)); x < std::min(W, static_cast<int>(cx + radius) + 1); ++x)
				if((x - cx) * (x - cx) + (y - cy) * (y - cy) <= radius * radius)
					ink.at(x, y) = 1;
	}
	Plane smooth = Blur3(ink);
	for(auto& v : smooth.v)
		v = v > 0.5f ? 1.0f : 0.0f;
	smooth = Blur3(smooth);
	//the picture covers a rectangle with a 1 pixel soft edge (the same as blurring a filled rectangle, one axis at a time)
	const auto edge = [](int i, int lo, int hi) {
		if(i >= lo + 1 && i <= hi - 2) return 1.0f;
		if(i == lo || i == hi - 1) return 2.0f / 3.0f;
		if(i == lo - 1 || i == hi) return 1.0f / 3.0f;
		return 0.0f;
	};
	const int zx0 = static_cast<int>(ZONE_X0 * W), zx1 = static_cast<int>(ZONE_X1 * W), zy0 = static_cast<int>(ZONE_Y0 * H);
	result.width = W;
	result.height = H;
	result.rgba.resize(static_cast<size_t>(W) * H * 4);
	for(int y = 0; y < H; ++y) {
		for(int x = 0; x < W; ++x) {
			const float z = edge(x, zx0, zx1) * edge(y, zy0, zone_h);
			const bool top = y < rows;
			const float a = z * (1.0f - (top ? smooth.at(x, y) : 0.0f)); //how much of the picture shows here
			const float* c = flat.at(x, y);
			float p[3];
			const float* source = y < zone_h ? art.at(x, y) : c;
			for(int k = 0; k < 3; ++k)
				p[k] = source[k];
			for(const Letters* letters : { &name, &type_line }) {
				const float glow = top ? letters->halo.at(x, y) : 0.0f, letter = top ? letters->alpha.at(x, y) : 0.0f;
				for(int k = 0; k < 3; ++k)
					p[k] = (p[k] * (1 - glow) + letters->halo_color[k] * glow) * (1 - letter) + letters->color[k] * letter;
			}
			uint8_t* o = &result.rgba[(static_cast<size_t>(y) * W + x) * 4];
			for(int k = 0; k < 3; ++k)
				o[k] = static_cast<uint8_t>(std::clamp(c[k] * (1 - a) + p[k] * a, 0.0f, 255.0f) + 0.5f);
			o[3] = static_cast<uint8_t>(z * 255.0f + 0.5f);
		}
	}
	return result;
}

}
