#include "../../gframe/profile_name.h"
#include <cstdio>

using namespace ygo;

static int failures = 0;
#define CHECK(cond) do { if(!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++failures; } } while(0)

int main() {
	//every id comes back out of a signed name
	for(int id = 1; id <= 255; ++id) {
		const auto signed_name = SignProfileName(L"Alex", id);
		CHECK(signed_name.size() == 4 + PROFILE_SIGN_LENGTH);
		CHECK(signed_name.substr(0, 4) == L"Alex");
		CHECK(ProfileIdOfName(signed_name) == id);
	}
	//the added part is only spaces, so the name looks the same
	for(size_t i = 4; i < SignProfileName(L"Alex", 77).size(); ++i) {
		const wchar_t c = SignProfileName(L"Alex", 77)[i];
		CHECK(c == L' ' || c == L' ');
	}
	//no room (a name of 10 or more characters), or a bad id: the name is left alone
	CHECK(SignProfileName(L"AbcdefghIj", 5) == L"AbcdefghIj");
	CHECK(SignProfileName(L"Alex", 0) == L"Alex");
	CHECK(SignProfileName(L"Alex", 256) == L"Alex");
	CHECK(SignProfileName(L"Abcdefghi", 5).size() == 19); //9 + 10 fits exactly
	//ordinary names are not mistaken for a signed one
	CHECK(ProfileIdOfName(L"Alex") == 0);
	CHECK(ProfileIdOfName(L"Alex          ") == 0); //ten plain spaces: id 0 does not exist
	CHECK(ProfileIdOfName(L"") == 0);
	CHECK(ProfileIdOfName(L"Alex   ") == 0);
	//a flipped bit breaks the check
	auto broken = SignProfileName(L"Alex", 77);
	broken[6] = broken[6] == L' ' ? L' ' : L' ';
	CHECK(ProfileIdOfName(broken) != 77);
	if(!failures)
		std::printf("ALL OK\n");
	return failures ? 1 : 0;
}
