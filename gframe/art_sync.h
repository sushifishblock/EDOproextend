#ifndef ART_SYNC_H
#define ART_SYNC_H

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

namespace ygo {

//Bulk download of the high resolution card art (813x1185, from YGOPRODeck's image server) before the game can be used.
//One background thread drives many parallel connections (curl multi, connections are kept alive and reused) and stays under
//the request rate YGOPRODeck asks for. Cards the high resolution source doesn't have fall back to the game's own picture
//server (low resolution) when there is no picture at all. Results are remembered in cardcache\art_done.txt and
//cardcache\art_notfound.txt so a card is never asked for twice.
class ArtSync {
public:
	struct Job {
		uint32_t code = 0;
		bool has_local = false; //there is already a (low resolution) picture on disk
		bool skip_primary = false; //the high resolution source is known not to have this card
	};
	//which of the cards still need a high resolution picture
	static std::vector<Job> FindJobs(const std::vector<std::pair<uint32_t, uint64_t>>& cards_with_local_size);
	explicit ArtSync(std::vector<Job> jobs);
	~ArtSync();
	size_t Total() const { return total; }
	size_t Processed() const { return processed.load(); }
	size_t Downloaded() const { return downloaded.load(); }
	size_t NotOnServer() const { return missing.load(); }
	size_t Failed() const { return failed.load(); }
	uint64_t Bytes() const { return bytes.load(); }
	bool Finished() const { return finished.load(); }
	//no answer from the servers for a while (probably no internet)
	bool Offline() const { return offline.load(); }
	double ElapsedSeconds() const;
	//pictures below this size (bytes) are treated as low resolution
	static constexpr uint64_t HIGH_RES_MIN_BYTES = 70000;
private:
	void Run();
	std::vector<Job> jobs;
	size_t total = 0;
	std::atomic<size_t> processed{ 0 }, downloaded{ 0 }, missing{ 0 }, failed{ 0 };
	std::atomic<uint64_t> bytes{ 0 };
	std::atomic<bool> finished{ false }, offline{ false }, stop{ false };
	std::chrono::steady_clock::time_point started;
	std::thread worker;
};

}

#endif
