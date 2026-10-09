#ifndef POPULARITY_H
#define POPULARITY_H

#include <atomic>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include "data_manager.h"

namespace ygo {

//How much each card is played in competitive decks, from the card usage ranking of YugiohMeta (1 = the most played card).
//The ranking is kept in popularity.json and refreshed in the background every time the game starts.
class Popularity {
public:
	static void Load();
	static void StartUpdate();
	//main thread, once per frame: true when a newer ranking was just applied
	static bool ApplyUpdate();
	//lower is more played; cards nobody plays get a huge number
	static int Rank(const CardDataC* card);
	static bool HasData() { return !ranks.empty(); }
	//sort order: most played first, the rest by name
	static bool Less(const CardDataC* a, const CardDataC* b);
private:
	static void UpdateThread();
	static inline std::unordered_map<uint32_t, int> ranks;
	static inline std::mutex pending_mutex;
	static inline std::unordered_map<uint32_t, int> pending_ranks;
	static inline std::atomic<bool> has_pending{ false };
	static inline std::atomic<bool> update_started{ false };
};

}

#endif
