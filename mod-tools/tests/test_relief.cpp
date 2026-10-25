#include "../../gframe/relief.h"
#include <cmath>
#include <cstdio>
using namespace ygo;
static int failures = 0;
static void Expect(const char* name, bool ok) {
	std::printf("%s %s\n", ok ? "ok  " : "FAIL", name);
	if(!ok)
		++failures;
}
int main() {
	const int n = RELIEF_SIZE;
	std::vector<float> flat(n * n, 0.5f);
	std::vector<float> step(n * n);
	for(int y = 0; y < n; ++y)
		for(int x = 0; x < n; ++x)
			step[y * n + x] = x < n / 2 ? 0.1f : 0.9f; //dark left, bright right
	Expect("a wrong size gives an empty relief", MakeRelief({ 1.0f }).Empty());
	const auto flat_map = MakeRelief(flat);
	std::vector<uint8_t> rgba;
	ShadeRelief(flat_map, 0.0f, rgba);
	bool all_clear = true;
	for(size_t i = 3; i < rgba.size(); i += 4)
		all_clear = all_clear && rgba[i] == 0;
	Expect("a flat picture has no relief", all_clear && rgba.size() == static_cast<size_t>(n) * n * 4);
	const auto map = MakeRelief(step);
	Expect("the step rises towards +x", map.gx[(n / 2) * n + n / 2] > 0.01f && std::fabs(map.gy[(n / 2) * n + n / 2]) < 0.001f);
	ShadeRelief(map, 0.0f, rgba);
	const uint8_t* edge = &rgba[((n / 2) * n + n / 2) * 4];
	Expect("lit from +x the step edge is a bright rim", edge[3] > 40 && edge[0] > 100);
	ShadeRelief(map, 3.14159265f, rgba);
	edge = &rgba[((n / 2) * n + n / 2) * 4];
	Expect("lit from -x the same edge is a dark rim", edge[3] > 10 && edge[0] == 0 && edge[1] == 0);
	const uint8_t* far_away = &rgba[((n / 2) * n + 8) * 4];
	Expect("away from the edge there is nothing", far_away[3] == 0);
	std::printf(failures ? "FAILED\n" : "ALL OK\n");
	return failures ? 1 : 0;
}
