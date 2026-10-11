#include "art_fit.h"
#include <algorithm>
#include <cmath>

namespace ygo {

namespace {
//bilinear sample (pixel centers are at +0.5)
void Sample(const Pixels& s, float x, float y, float* rgb) {
	const float u = std::clamp(x - 0.5f, 0.0f, static_cast<float>(s.w - 1));
	const float v = std::clamp(y - 0.5f, 0.0f, static_cast<float>(s.h - 1));
	const int x0 = static_cast<int>(u), y0 = static_cast<int>(v);
	const int x1 = std::min(x0 + 1, s.w - 1), y1 = std::min(y0 + 1, s.h - 1);
	const float fx = u - x0, fy = v - y0;
	const uint8_t* p00 = &s.rgba[(static_cast<size_t>(y0) * s.w + x0) * 4];
	const uint8_t* p10 = &s.rgba[(static_cast<size_t>(y0) * s.w + x1) * 4];
	const uint8_t* p01 = &s.rgba[(static_cast<size_t>(y1) * s.w + x0) * 4];
	const uint8_t* p11 = &s.rgba[(static_cast<size_t>(y1) * s.w + x1) * 4];
	for(int c = 0; c < 3; ++c) {
		const float top = p00[c] + (p10[c] - p00[c]) * fx;
		const float bottom = p01[c] + (p11[c] - p01[c]) * fx;
		rgb[c] = top + (bottom - top) * fy;
	}
}

//the part (sx0,sy0)-(sx1,sy1) of the source (in source pixels) as a dw x dh picture; averages several samples per pixel when shrinking
Pixels Resample(const Pixels& src, float sx0, float sy0, float sx1, float sy1, int dw, int dh) {
	Pixels out;
	out.w = dw;
	out.h = dh;
	out.rgba.assign(static_cast<size_t>(dw) * dh * 4, 255);
	const float stepx = (sx1 - sx0) / dw, stepy = (sy1 - sy0) / dh;
	const int nx = std::clamp(static_cast<int>(std::ceil(stepx)), 1, 6);
	const int ny = std::clamp(static_cast<int>(std::ceil(stepy)), 1, 6);
	for(int y = 0; y < dh; ++y) {
		for(int x = 0; x < dw; ++x) {
			float acc[3] = { 0, 0, 0 };
			for(int j = 0; j < ny; ++j) {
				for(int i = 0; i < nx; ++i) {
					float rgb[3];
					Sample(src, sx0 + (x + (i + 0.5f) / nx) * stepx, sy0 + (y + (j + 0.5f) / ny) * stepy, rgb);
					acc[0] += rgb[0];
					acc[1] += rgb[1];
					acc[2] += rgb[2];
				}
			}
			uint8_t* p = &out.rgba[(static_cast<size_t>(y) * dw + x) * 4];
			for(int c = 0; c < 3; ++c)
				p[c] = static_cast<uint8_t>(std::clamp(acc[c] / (nx * ny) + 0.5f, 0.0f, 255.0f));
		}
	}
	return out;
}
}

Pixels ShrinkTo(const Pixels& src, int max_side) {
	if(src.Empty() || std::max(src.w, src.h) <= max_side)
		return src;
	const float k = static_cast<float>(max_side) / std::max(src.w, src.h);
	return Resample(src, 0, 0, static_cast<float>(src.w), static_cast<float>(src.h), std::max(1, static_cast<int>(src.w * k)), std::max(1, static_cast<int>(src.h * k)));
}

Pixels FitArt(const Pixels& src, int out_w, int out_h, const ArtParams& p) {
	Pixels out;
	out.w = out_w;
	out.h = out_h;
	out.rgba.assign(static_cast<size_t>(out_w) * out_h * 4, 0);
	for(size_t i = 3; i < out.rgba.size(); i += 4)
		out.rgba[i] = 255;
	if(src.Empty() || out_w < 1 || out_h < 1)
		return out;
	const float fill_scale = std::max(static_cast<float>(out_w) / src.w, static_cast<float>(out_h) / src.h);
	const float scale = (p.fit ? std::min(static_cast<float>(out_w) / src.w, static_cast<float>(out_h) / src.h) : fill_scale) * std::clamp(p.zoom, 0.1f, 8.0f);
	const int nw = std::max(1, static_cast<int>(std::lround(src.w * scale)));
	const int nh = std::max(1, static_cast<int>(std::lround(src.h * scale)));
	if(p.fit) {
		//a blurred, darker copy of the picture fills what the whole picture does not cover
		const float visible_w = out_w / fill_scale, visible_h = out_h / fill_scale;
		const float left = (src.w - visible_w) / 2, top = (src.h - visible_h) / 2;
		const Pixels tiny = Resample(src, left, top, left + visible_w, top + visible_h, std::max(4, out_w / 16), std::max(4, out_h / 16));
		out = Resample(tiny, 0, 0, static_cast<float>(tiny.w), static_cast<float>(tiny.h), out_w, out_h);
		for(size_t i = 0; i < out.rgba.size(); i += 4)
			for(int c = 0; c < 3; ++c)
				out.rgba[i + c] = static_cast<uint8_t>(out.rgba[i + c] * 0.55f);
	}
	const int x = static_cast<int>(std::lround((out_w - nw) / 2.0f + p.ox * std::abs(nw - out_w) / 2.0f));
	const int y = static_cast<int>(std::lround((out_h - nh) / 2.0f + p.oy * std::abs(nh - out_h) / 2.0f));
	const int dx0 = std::max(0, x), dx1 = std::min(out_w, x + nw);
	const int dy0 = std::max(0, y), dy1 = std::min(out_h, y + nh);
	if(dx1 > dx0 && dy1 > dy0) {
		const Pixels part = Resample(src, (dx0 - x) / scale, (dy0 - y) / scale, (dx1 - x) / scale, (dy1 - y) / scale, dx1 - dx0, dy1 - dy0);
		for(int row = 0; row < part.h; ++row)
			std::copy_n(&part.rgba[static_cast<size_t>(row) * part.w * 4], static_cast<size_t>(part.w) * 4, &out.rgba[(static_cast<size_t>(dy0 + row) * out_w + dx0) * 4]);
	}
	if(std::abs(p.bright - 1.0f) > 0.01f) {
		for(size_t i = 0; i < out.rgba.size(); i += 4)
			for(int c = 0; c < 3; ++c)
				out.rgba[i + c] = static_cast<uint8_t>(std::clamp(out.rgba[i + c] * p.bright + 0.5f, 0.0f, 255.0f));
	}
	if(p.border > 0.0f) {
		const int width = std::max(1, static_cast<int>(std::lround(out_w * p.border)));
		for(int row = 0; row < out_h; ++row) {
			for(int col = 0; col < out_w; ++col) {
				if(col < width || row < width || col >= out_w - width || row >= out_h - width) {
					uint8_t* px = &out.rgba[(static_cast<size_t>(row) * out_w + col) * 4];
					px[0] = px[1] = px[2] = 0;
				}
			}
		}
	}
	return out;
}

}
