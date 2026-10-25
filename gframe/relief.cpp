#include "relief.h"
#include <algorithm>
#include <cmath>

namespace ygo {

namespace {
constexpr int N = RELIEF_SIZE;
constexpr float GAIN = 9.0f;

uint32_t Hash(uint32_t x) {
	x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
	return x;
}

void BoxBlur(std::vector<float>& a) {
	std::vector<float> out(a.size());
	for(int y = 0; y < N; ++y) {
		for(int x = 0; x < N; ++x) {
			float sum = 0;
			for(int dy = -1; dy <= 1; ++dy)
				for(int dx = -1; dx <= 1; ++dx)
					sum += a[std::clamp(y + dy, 0, N - 1) * N + std::clamp(x + dx, 0, N - 1)];
			out[y * N + x] = sum / 9.0f;
		}
	}
	a.swap(out);
}

void Hsv(float h, float s, float& r, float& g, float& b) {
	h = h - std::floor(h);
	const float c = s;
	const float x = c * (1 - std::fabs(std::fmod(h * 6, 2.0f) - 1));
	const float m = 1 - c;
	switch(static_cast<int>(h * 6)) {
	case 0: r = c; g = x; b = 0; break;
	case 1: r = x; g = c; b = 0; break;
	case 2: r = 0; g = c; b = x; break;
	case 3: r = 0; g = x; b = c; break;
	case 4: r = x; g = 0; b = c; break;
	default: r = c; g = 0; b = x; break;
	}
	r += m; g += m; b += m;
}
}

ReliefMap MakeRelief(const std::vector<float>& luma) {
	ReliefMap map;
	if(luma.size() != static_cast<size_t>(N) * N)
		return map;
	std::vector<float> smooth = luma;
	BoxBlur(smooth);
	BoxBlur(smooth);
	//the brightness in six steps with soft shoulders: the shapes of the picture become raised plateaus
	std::vector<float> height(smooth.size());
	for(size_t i = 0; i < smooth.size(); ++i) {
		const float x = smooth[i] * 6.0f;
		const float k = std::floor(x);
		const float f = std::clamp((x - k - 0.30f) / 0.40f, 0.0f, 1.0f);
		const float s = f * f * (3 - 2 * f);
		height[i] = 0.8f * (k + s) / 6.0f + 0.2f * smooth[i];
	}
	BoxBlur(height);
	map.gx.resize(height.size());
	map.gy.resize(height.size());
	for(int y = 0; y < N; ++y) {
		for(int x = 0; x < N; ++x) {
			const int x0 = std::max(x - 1, 0), x1 = std::min(x + 1, N - 1);
			const int y0 = std::max(y - 1, 0), y1 = std::min(y + 1, N - 1);
			map.gx[y * N + x] = (height[y * N + x1] - height[y * N + x0]) / static_cast<float>(x1 - x0);
			map.gy[y * N + x] = (height[y1 * N + x] - height[y0 * N + x]) / static_cast<float>(y1 - y0);
		}
	}
	return map;
}

void ShadeRelief(const ReliefMap& relief, float angle, std::vector<uint8_t>& rgba) {
	rgba.assign(static_cast<size_t>(N) * N * 4, 0);
	if(relief.Empty())
		return;
	const float ca = std::cos(angle), sa = std::sin(angle);
	for(int y = 0; y < N; ++y) {
		for(int x = 0; x < N; ++x) {
			const size_t i = static_cast<size_t>(y) * N + x;
			const float gx = relief.gx[i], gy = relief.gy[i];
			const float d = gx * ca + gy * sa;
			float lit = std::clamp(d * GAIN, 0.0f, 1.0f);
			const float shadow = std::clamp(-d * GAIN * 0.55f, 0.0f, 0.7f);
			const float noise = (Hash(static_cast<uint32_t>(y * N + x + 777)) & 255) / 255.0f;
			lit *= 0.7f + 0.3f * noise;
			uint8_t* p = &rgba[i * 4];
			if(lit > shadow) {
				float r, g, b;
				Hsv(std::atan2(gy, gx) / 6.2831853f + 0.5f, 0.45f, r, g, b);
				p[0] = static_cast<uint8_t>(std::clamp((r * 0.6f + 0.4f) * 255.0f, 0.0f, 255.0f));
				p[1] = static_cast<uint8_t>(std::clamp((g * 0.6f + 0.4f) * 255.0f, 0.0f, 255.0f));
				p[2] = static_cast<uint8_t>(std::clamp((b * 0.6f + 0.4f) * 255.0f, 0.0f, 255.0f));
				p[3] = static_cast<uint8_t>(lit * 255.0f);
			} else {
				p[3] = static_cast<uint8_t>(shadow * 255.0f);
			}
		}
	}
}

}
