#ifndef GENESYS_H
#define GENESYS_H

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include "deck.h"
#include "data_manager.h"

namespace ygo {

//Genesys format: every card has a point cost (most cost nothing), the Main, Extra and Side Deck together may not
//exceed the point cap, Link and Pendulum monsters can't be used and the normal ban list does not apply
//(three copies of everything). The costs are read from genesys_points.json (made by update_genesys.py).
class Genesys {
public:
	static inline bool enabled = false;
	static inline int cap = 100;
	static void Load();
	//downloads the current point costs in the background (once per start); the new list is applied by ApplyUpdate
	static void StartUpdate();
	//main thread, once per frame: true when a new list was just applied
	static bool ApplyUpdate();
	//true once the saved list was missing or out of date and a new one was saved: the game restarts to use it
	static bool RestartWanted() { return restart_wanted.load(); }
	static int Points(const CardDataC* card);
	static bool Illegal(const CardDataC* card);
	static bool HasList() { return !points.empty(); }
	struct Totals {
		int points = 0;
		int main = 0;
		int extra = 0;
		int side = 0;
		int illegal = 0;
	};
	static Totals Count(const Deck& deck);
private:
	static void UpdateThread();
	static inline std::mutex pending_mutex;
	static inline std::unordered_map<uint32_t, int> pending_points;
	static inline std::atomic<bool> has_pending{ false };
	static inline std::atomic<bool> update_started{ false };
	static inline std::atomic<bool> restart_wanted{ false };
	static inline bool started_with_list = false;
	static inline std::unordered_map<uint32_t, int> points;
};

}

#endif
