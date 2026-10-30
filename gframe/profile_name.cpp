#include "profile_name.h"

namespace ygo {

namespace {
constexpr wchar_t ZERO = L' ', ONE = L' ';
constexpr int CHECK_BITS = 2;

int Check(int id) {
	return (id ^ (id >> 2) ^ (id >> 4) ^ (id >> 6)) & 3;
}
}

std::wstring SignProfileName(const std::wstring& name, int id) {
	if(id < 1 || id > 255 || name.size() + PROFILE_SIGN_LENGTH > PROFILE_NAME_LIMIT)
		return name;
	const int bits = (id << CHECK_BITS) | Check(id);
	std::wstring out = name;
	for(int i = static_cast<int>(PROFILE_SIGN_LENGTH) - 1; i >= 0; --i)
		out.push_back(((bits >> i) & 1) ? ONE : ZERO);
	return out;
}

int ProfileIdOfName(const std::wstring& name) {
	if(name.size() < PROFILE_SIGN_LENGTH)
		return 0;
	int bits = 0;
	for(size_t i = name.size() - PROFILE_SIGN_LENGTH; i < name.size(); ++i) {
		if(name[i] != ZERO && name[i] != ONE)
			return 0;
		bits = (bits << 1) | (name[i] == ONE ? 1 : 0);
	}
	const int id = bits >> CHECK_BITS;
	return id >= 1 && (bits & 3) == Check(id) ? id : 0;
}

}
