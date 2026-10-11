#include "../gframe/hand_sim.h"
#include <cmath>
#include <cstdio>
using namespace ygo;
static int failures = 0;
static void Check(const char* name, double got, double want, double tol) {
	const bool ok = std::fabs(got - want) <= tol;
	std::printf("%s %s: got %.3f want %.3f\n", ok ? "ok  " : "FAIL", name, got, want);
	if(!ok)
		++failures;
}
int main() {
	{ //one starter in 40, hand 5: 5/40 = 12.5 %
		std::vector<Tag> deck(40, Tag::NONE);
		deck[0] = Tag::STARTER;
		const auto r = Simulate(deck, 5, 200000, {}, 1);
		Check("1 in 40 hand 5 at least one", r.Percent(r.runs - r.counts[1][0]), 12.5, 0.5);
	}
	{ //three starters in 40, hand 5: 1 - C(37,5)/C(40,5) = 33.8 %
		std::vector<Tag> deck(40, Tag::NONE);
		for(int i = 0; i < 3; ++i)
			deck[i] = Tag::STARTER;
		const auto r = Simulate(deck, 5, 200000, {}, 2);
		Check("3 in 40 hand 5 at least one", r.Percent(r.runs - r.counts[1][0]), 33.8, 0.5);
	}
	{ //rule: starter >= 1 and brick == 0 with 3 starters and 2 bricks in 40, hand 5
		std::vector<Tag> deck(40, Tag::NONE);
		for(int i = 0; i < 3; ++i)
			deck[i] = Tag::STARTER;
		deck[3] = Tag::BRICK;
		deck[4] = Tag::BRICK;
		Rule rule;
		rule.name = "good";
		rule.conditions = { { Tag::STARTER, false, 1 }, { Tag::BRICK, true, 0 } };
		const auto r = Simulate(deck, 5, 200000, { rule }, 3);
		//exact: P(B=0) - P(S=0 and B=0) = C(38,5)/C(40,5) - C(35,5)/C(40,5)
		const double c40 = 658008.0, c38 = 501942.0, c35 = 324632.0;
		Check("rule starter>=1 brick==0", r.Percent(r.rule_hits[0]), 100.0 * (c38 - c35) / c40, 0.5);
	}
	{ //a deck smaller than the hand gives no hands
		std::vector<Tag> deck(3, Tag::NONE);
		const auto r = Simulate(deck, 5, 1000, {}, 4);
		Check("deck smaller than hand -> 0 runs", r.runs, 0, 0);
	}
	std::printf(failures ? "FAILED\n" : "ALL OK\n");
	return failures ? 1 : 0;
}
