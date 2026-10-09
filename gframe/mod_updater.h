#ifndef MOD_UPDATER_H
#define MOD_UPDATER_H

#include <atomic>
#include <mutex>
#include <string>

namespace ygo {

//One-click updater for this mod: checks the latest GitHub release in the background, and on a click downloads the
//EDOPro-x64.exe asset, swaps it in for the running exe (a running exe can be renamed) and starts the new one.
class ModUpdater {
public:
	static constexpr const char* VERSION = "1.3";
	enum State { IDLE, CHECKING, AVAILABLE, DOWNLOADING, FAILED, RESTART };
	//deletes the exe left over from the previous update
	static void CleanupOld();
	static void StartCheck();
	static void BeginInstall();
	//starts the new exe (after a short delay so this one has closed); call once State is RESTART
	static void LaunchNew();
	static State GetState() { return state.load(); }
	static int Percent() { return percent.load(); }
	static std::wstring Label();
private:
	static void CheckThread();
	static void InstallThread();
	static inline std::atomic<State> state{ IDLE };
	static inline std::atomic<int> percent{ 0 };
	static inline std::atomic<bool> check_started{ false };
	static inline std::mutex info_mutex;
	static inline std::string latest_tag;
	static inline std::string asset_url;
	static inline long long asset_size = 0;
};

}

#endif
