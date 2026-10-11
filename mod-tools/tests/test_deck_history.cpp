#include "../../gframe/deck_history.h"
#include <cstdio>
using namespace ygo;
static int failures = 0;
static void Expect(const char* name, bool ok) {
	std::printf("%s %s\n", ok ? "ok  " : "FAIL", name);
	if(!ok)
		++failures;
}
static DeckSnapshot Snap(std::vector<uint32_t> main) {
	DeckSnapshot s;
	s.main = main;
	return s;
}
int main() {
	DeckHistory h;
	h.Reset(Snap({ 1 }));
	Expect("nothing to undo at the start", !h.CanUndo() && !h.CanRedo() && h.Undo() == nullptr);
	Expect("same state is not a step", !h.Record(Snap({ 1 })) && h.Size() == 1);
	Expect("a change is a step", h.Record(Snap({ 1, 2 })) && h.Record(Snap({ 1, 2, 3 })) && h.Size() == 3);
	const auto* back = h.Undo();
	Expect("undo gives the previous state", back && back->main == std::vector<uint32_t>({ 1, 2 }) && h.CanRedo());
	Expect("recording the undone state again is not a step", !h.Record(*back) && h.Size() == 3);
	const auto* fwd = h.Redo();
	Expect("redo gives the next state", fwd && fwd->main == std::vector<uint32_t>({ 1, 2, 3 }) && !h.CanRedo());
	h.Undo();
	h.Undo();
	Expect("a new change after undo drops redo", h.Record(Snap({ 9 })) && !h.CanRedo() && h.Size() == 2);
	Expect("undo after that goes back to the first", h.Undo()->main == std::vector<uint32_t>({ 1 }));
	h.Reset(Snap({ 0 }));
	for(uint32_t i = 1; i <= 250; ++i)
		h.Record(Snap({ i }));
	size_t steps = 0;
	while(h.Undo())
		++steps;
	Expect("history is limited to 100 steps", steps == DeckHistory::MAX_STEPS);
	//saved versions: newest first, limits of count and bytes
	std::vector<VersionFile> files;
	for(int i = 0; i < 10; ++i)
		files.push_back({ "v" + std::to_string(i), 100 });
	Expect("count limit prunes the oldest", VersionsToPrune(files, 4, 1000000).size() == 6 && VersionsToPrune(files, 4, 1000000)[0] == 4);
	Expect("byte limit prunes the oldest", VersionsToPrune(files, 100, 350).size() == 7);
	Expect("the newest is always kept", VersionsToPrune({ { "big", 5000000 } }, 10, 1000).empty());
	Expect("nothing to prune within limits", VersionsToPrune(files, 100, 1000000).empty());
	std::printf(failures ? "FAILED\n" : "ALL OK\n");
	return failures ? 1 : 0;
}
