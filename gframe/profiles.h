#ifndef PROFILES_H
#define PROFILES_H

#include <atomic>
#include <memory>
#include <string>
#include "rarity.h"

namespace ygo {

//Friends who use this mod see each other's rarities and sleeves in a duel.
//Nothing extra is sent: every player's name carries a small id as trailing spaces (a plain space is a 0, a no-break space is a 1),
//which looks like the normal name to everybody else. The id points to the rarity pack of that friend, which is kept in profiles/<id>.json
//when it is installed. A player without the mod, or a friend whose pack is not installed, simply shows nothing.
class Profiles {
public:
	//my id: made and saved in profile.id the first time it is asked for
	static int OwnId();
	//the name with my id hidden behind it; unchanged when there is no room for it or profile.off exists
	static std::wstring SignName(const std::wstring& name);
	//the id hidden in a name, 0 if there is none
	static int IdOfName(const std::wstring& name);

	//keep an installed rarity pack as the profile of its author
	static void Save(int id, const std::string& pack_json);
	//a duel starts: look for the opponent's profile again
	static void ResetOpponent();
	//the profile of the opponent in this duel, nullptr if there is none
	static std::shared_ptr<RarityProfile> Opponent();
private:
	static inline std::atomic<bool> resolved{ false };
	static inline std::shared_ptr<RarityProfile> opponent;
};

}

#endif
