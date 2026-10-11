#include "deck_history.h"

namespace ygo {

void DeckHistory::Reset(const DeckSnapshot& initial) {
	states.assign(1, initial);
	index = 0;
}

bool DeckHistory::Record(const DeckSnapshot& now) {
	if(states.empty()) {
		Reset(now);
		return false;
	}
	if(states[index] == now)
		return false;
	states.resize(index + 1); //a new change ends the redo steps
	states.push_back(now);
	if(states.size() > MAX_STEPS + 1)
		states.erase(states.begin());
	index = states.size() - 1;
	return true;
}

const DeckSnapshot* DeckHistory::Undo() {
	if(!CanUndo())
		return nullptr;
	return &states[--index];
}

const DeckSnapshot* DeckHistory::Redo() {
	if(!CanRedo())
		return nullptr;
	return &states[++index];
}

std::vector<size_t> VersionsToPrune(const std::vector<VersionFile>& newest_first, size_t max_count, uint64_t max_bytes) {
	std::vector<size_t> prune;
	uint64_t total = 0;
	for(size_t i = 0; i < newest_first.size(); ++i) {
		total += newest_first[i].size;
		if(i > 0 && (i >= max_count || total > max_bytes))
			prune.push_back(i);
	}
	return prune;
}

}
