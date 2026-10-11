#ifndef DECK_HISTORY_H
#define DECK_HISTORY_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ygo {

//The cards of a deck as plain numbers (a snapshot for undo / redo; it is tiny: about 90 numbers).
struct DeckSnapshot {
	std::vector<uint32_t> main, extra, side;
	bool operator==(const DeckSnapshot& o) const { return main == o.main && extra == o.extra && side == o.side; }
	bool operator!=(const DeckSnapshot& o) const { return !(*this == o); }
};

//Undo / redo of one deck. Kept in memory only.
class DeckHistory {
public:
	static constexpr size_t MAX_STEPS = 100;
	//start over with this deck as the only state
	void Reset(const DeckSnapshot& initial);
	//the deck now looks like this: a new step when it differs from the current one (redo steps are dropped)
	bool Record(const DeckSnapshot& now);
	bool CanUndo() const { return !states.empty() && index > 0; }
	bool CanRedo() const { return index + 1 < states.size(); }
	//moves one step and returns the state to show (nullptr when there is nothing to do)
	const DeckSnapshot* Undo();
	const DeckSnapshot* Redo();
	const DeckSnapshot* Current() const { return states.empty() ? nullptr : &states[index]; }
	size_t Size() const { return states.size(); }
private:
	std::vector<DeckSnapshot> states;
	size_t index = 0;
};

//Saved versions of a deck (deck/versions/<name>/<time>.ydk): which files to delete so a deck stays within its limits.
struct VersionFile {
	std::string name; //file name, the time sorts them
	uint64_t size = 0;
};
//`newest_first` is sorted newest first; returns the indexes of the files to delete (the oldest ones),
//so that at most `max_count` files and `max_bytes` bytes remain (the newest file is always kept)
std::vector<size_t> VersionsToPrune(const std::vector<VersionFile>& newest_first, size_t max_count, uint64_t max_bytes);

}

#endif
