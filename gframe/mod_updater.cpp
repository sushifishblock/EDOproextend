#include "mod_updater.h"
#include <cstdio>
#include <filesystem>
#include <thread>
#include <vector>
#include "curl.h"
#include "config.h"
#include "game_config.h"
#include "utils.h"
#include <nlohmann/json.hpp>
#ifdef _WIN32
#include <Windows.h>
#endif

namespace ygo {

namespace {
constexpr const char* RELEASE_URL = "https://api.github.com/repos/sushifishblock/EDOproextend/releases/latest";
constexpr const char* ASSET_NAME = "EDOPro-x64.exe";

size_t WriteBody(char* data, size_t size, size_t count, void* user) {
	static_cast<std::string*>(user)->append(data, size * count);
	return size * count;
}
size_t WriteFile(char* data, size_t size, size_t count, void* user) {
	return fwrite(data, size, count, static_cast<FILE*>(user)) ;
}
int Progress(void* user, curl_off_t total, curl_off_t now, curl_off_t, curl_off_t) {
	if(total > 0)
		static_cast<std::atomic<int>*>(user)->store(static_cast<int>(now * 100 / total));
	return 0;
}

void SetCommon(CURL* curl, const std::string& url) {
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "edopro-mod-updater");
	curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
	curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
	curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	if(ygo::gGameConfig->ssl_certificate_path.size() && Utils::FileExists(Utils::ToPathString(ygo::gGameConfig->ssl_certificate_path)))
		curl_easy_setopt(curl, CURLOPT_CAINFO, ygo::gGameConfig->ssl_certificate_path.data());
}

//"v1.2" -> {1, 2}
std::vector<int> ParseVersion(const std::string& text) {
	std::vector<int> parts;
	int value = 0;
	bool have = false;
	for(const char c : text) {
		if(c == '-' || c == '+')
			break; //"1.4.0-beta" is not newer than "1.4.0"
		if(c >= '0' && c <= '9') {
			if(value < 100000)
				value = value * 10 + (c - '0');
			have = true;
		} else if(c == '.') {
			parts.push_back(value);
			value = 0;
			have = false;
		}
	}
	if(have)
		parts.push_back(value);
	return parts;
}
bool IsNewer(const std::string& tag) {
	auto a = ParseVersion(tag), b = ParseVersion(ModUpdater::VERSION);
	if(a.empty())
		return false;
	a.resize(std::max(a.size(), b.size()));
	b.resize(a.size());
	return a > b;
}

std::filesystem::path ExePath() {
#ifdef _WIN32
	wchar_t buffer[MAX_PATH * 2];
	const auto length = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
	if(length == 0 || length >= std::size(buffer))
		return {};
	return std::filesystem::path(std::wstring(buffer, length));
#else
	return {};
#endif
}
}

void ModUpdater::CleanupOld() {
	std::error_code ec;
	auto exe = ExePath();
	if(exe.empty())
		return;
	std::filesystem::remove(exe.wstring() + L".old", ec);
	std::filesystem::remove(exe.wstring() + L".new", ec);
}

void ModUpdater::StartCheck() {
	if(check_started.exchange(true))
		return;
	std::thread(&ModUpdater::CheckThread).detach();
}

void ModUpdater::CheckThread() {
	try {
		state = CHECKING;
		auto* curl = curl_easy_init();
		if(!curl) {
			state = IDLE;
			return;
		}
		std::string body;
		SetCommon(curl, RELEASE_URL);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteBody);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
		const auto result = curl_easy_perform(curl);
		long status = 0;
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
		curl_easy_cleanup(curl);
		if(result != CURLE_OK || status != 200) {
			state = IDLE;
			return;
		}
		const auto root = nlohmann::json::parse(body);
		const std::string tag = root.at("tag_name").get<std::string>();
		if(!IsNewer(tag)) {
			state = IDLE;
			return;
		}
		for(const auto& asset : root.at("assets")) {
			if(asset.at("name").get<std::string>() == ASSET_NAME) {
				std::lock_guard<std::mutex> lock(info_mutex);
				const std::string url = asset.at("browser_download_url").get<std::string>();
				const long long size = asset.at("size").get<long long>();
				//only a file of this project's releases, with a plausible size
				if(url.rfind("https://github.com/sushifishblock/EDOproextend/releases/", 0) != 0 || size < 5ll * 1024 * 1024 || size > 200ll * 1024 * 1024)
					break;
				latest_tag = tag;
				asset_url = url;
				asset_size = size;
				state = AVAILABLE;
				return;
			}
		}
	} catch(...) {}
	if(state == CHECKING)
		state = IDLE;
}

void ModUpdater::BeginInstall() {
	State expected = AVAILABLE;
	if(!state.compare_exchange_strong(expected, DOWNLOADING)) {
		expected = FAILED;
		if(!state.compare_exchange_strong(expected, DOWNLOADING))
			return;
	}
	percent = 0;
	std::thread(&ModUpdater::InstallThread).detach();
}

void ModUpdater::InstallThread() {
	std::string url;
	long long expected_size = 0;
	{
		std::lock_guard<std::mutex> lock(info_mutex);
		url = asset_url;
		expected_size = asset_size;
	}
	try {
		const auto exe = ExePath();
		if(exe.empty() || url.empty())
			throw std::runtime_error("no exe");
		const std::filesystem::path part = exe.wstring() + L".new";
		const std::filesystem::path old = exe.wstring() + L".old";
		std::error_code ec;
		std::filesystem::remove(part, ec);
		{
			FILE* file = nullptr;
#ifdef _WIN32
			file = _wfopen(part.c_str(), L"wb");
#endif
			if(!file)
				throw std::runtime_error("open");
			auto* curl = curl_easy_init();
			if(!curl) {
				fclose(file);
				throw std::runtime_error("curl");
			}
			SetCommon(curl, url);
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteFile);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);
			curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
			curl_easy_setopt(curl, CURLOPT_TIMEOUT, 900L);
			curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
			curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, Progress);
			curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &percent);
			const auto result = curl_easy_perform(curl);
			curl_easy_cleanup(curl);
			const bool closed = fclose(file) == 0;
			if(result != CURLE_OK || !closed)
				throw std::runtime_error("download");
		}
		//sanity: size, and it must be a Windows executable
		const auto size = std::filesystem::file_size(part, ec);
		if(ec || size < 5u * 1024 * 1024 || (expected_size > 0 && static_cast<long long>(size) != expected_size))
			throw std::runtime_error("size");
		{
			FILE* file = nullptr;
#ifdef _WIN32
			file = _wfopen(part.c_str(), L"rb");
#endif
			char magic[2] = {};
			if(!file || fread(magic, 1, 2, file) != 2 || magic[0] != 'M' || magic[1] != 'Z') {
				if(file)
					fclose(file);
				throw std::runtime_error("magic");
			}
			fclose(file);
		}
		//swap: the running exe can be renamed
		std::filesystem::remove(old, ec);
		std::filesystem::rename(exe, old, ec);
		if(ec)
			throw std::runtime_error("rename");
		std::filesystem::rename(part, exe, ec);
		if(ec) {
			std::error_code ignore;
			std::filesystem::rename(old, exe, ignore);
			throw std::runtime_error("replace");
		}
		percent = 100;
		state = RESTART;
	} catch(...) {
		std::error_code ignore;
		std::filesystem::remove(std::filesystem::path(ExePath().wstring() + L".new"), ignore);
		state = FAILED;
	}
}

void ModUpdater::LaunchNew() {
#ifdef _WIN32
	const auto exe = ExePath();
	wchar_t cwd[MAX_PATH * 2];
	const auto cwd_length = GetCurrentDirectoryW(static_cast<DWORD>(std::size(cwd)), cwd);
	//wait a moment so this process has closed (and saved its settings), then start the new exe in the same folder
	std::wstring command = L"cmd.exe /c ping -n 3 127.0.0.1 >nul & start \"\" \"" + exe.wstring() + L"\"";
	STARTUPINFOW startup = {};
	startup.cb = sizeof(startup);
	PROCESS_INFORMATION info = {};
	if(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, cwd_length ? cwd : nullptr, &startup, &info)) {
		CloseHandle(info.hProcess);
		CloseHandle(info.hThread);
	}
#endif
}

std::wstring ModUpdater::Label() {
	switch(state.load()) {
	case AVAILABLE: {
		std::lock_guard<std::mutex> lock(info_mutex);
		return L"Update available: " + std::wstring(latest_tag.begin(), latest_tag.end()) + L" - click to install";
	}
	case DOWNLOADING:
		return L"Downloading update... " + std::to_wstring(percent.load()) + L"%";
	case FAILED:
		return L"Update failed - click to retry";
	case RESTART:
		return L"Restarting...";
	default:
		return L"";
	}
}

}
