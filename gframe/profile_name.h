#ifndef PROFILE_NAME_H
#define PROFILE_NAME_H

#include <string>

namespace ygo {

//A profile id (1..255) hidden at the end of a player name as 10 spaces: 8 bits of the id and 2 check bits,
//a plain space is a 0 and a no-break space is a 1. Pure code, no game needed.
constexpr size_t PROFILE_SIGN_LENGTH = 10;
constexpr size_t PROFILE_NAME_LIMIT = 19; //what fits into a player name of the network packets

//the name with the id behind it; unchanged when the id is not 1..255 or the result would not fit
std::wstring SignProfileName(const std::wstring& name, int id);
//the id hidden in a name, 0 if there is none (or the check bits do not fit)
int ProfileIdOfName(const std::wstring& name);

}

#endif
