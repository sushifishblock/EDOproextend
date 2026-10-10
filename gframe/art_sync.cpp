#include "art_sync.h"
#include <algorithm>
#include <chrono>
#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include "curl.h"
#include "config.h"
#include "game_config.h"
#include "utils.h"

namespace ygo {

namespace {
constexpr const char* PRIMARY_URL = "https://images.ygoprodeck.com/images/cards/";
constexpr const char* FALLBACK_URL = "https://pics.projectignis.org:2096/pics/";
constexpr size_t PARALLEL = 128; //connections at the same time
constexpr double REQUESTS_PER_SECOND = 600.0; //well above the 20 per second YGOPRODeck asks for; slows down by itself when the server pushes back
constexpr double MIN_REQUESTS_PER_SECOND = 20.0;
constexpr int MAX_ATTEMPTS = 4;
constexpr size_t MAX_BODY = 12u * 1024 * 1024;
constexpr const char* DONE_FILE = "cardcache/art_done.txt";
constexpr const char* NOTFOUND_FILE = "cardcache/art_notfound.txt";

std::unordered_set<uint32_t> LoadList(const char* path) {
	std::unordered_set<uint32_t> out;
	std::ifstream file(path);
	uint32_t code;
	while(file >> code)
		out.insert(code);
	return out;
}

void AppendList(const char* path, const std::vector<uint32_t>& codes) {
	if(codes.empty())
		return;
	std::error_code ec;
	std::filesystem::create_directories("cardcache", ec);
	std::ofstream file(path, std::ios::app);
	for(const auto code : codes)
		file << code << "\n";
}

bool LooksLikeJpeg(const std::string& body) {
	//start and end markers: a download cut short is not saved
	return body.size() > 2000 && static_cast<uint8_t>(body[0]) == 0xff && static_cast<uint8_t>(body[1]) == 0xd8
		&& static_cast<uint8_t>(body[body.size() - 2]) == 0xff && static_cast<uint8_t>(body[body.size() - 1]) == 0xd9;
}

size_t WriteBody(char* data, size_t size, size_t count, void* user) {
	auto* body = static_cast<std::string*>(user);
	if(body->size() + size * count > MAX_BODY)
		return 0;
	body->append(data, size * count);
	return size * count;
}

struct Transfer {
	CURL* easy = nullptr;
	ArtSync::Job job;
	bool fallback = false; //second source
	int attempts = 0;
	std::string body;
	char error[CURL_ERROR_SIZE] = {};
};
}

std::vector<ArtSync::Job> ArtSync::FindJobs(const std::vector<std::pair<uint32_t, uint64_t>>& cards) {
	const auto done = LoadList(DONE_FILE);
	const auto not_found = LoadList(NOTFOUND_FILE);
	std::vector<Job> out;
	for(const auto& card : cards) {
		const bool has_local = card.second != UINT64_MAX;
		if(has_local && (card.second == 0 || card.second >= HIGH_RES_MIN_BYTES))
			continue; //already high resolution (or inside an archive, which can't be judged)
		if(done.count(card.first))
			continue;
		if(not_found.count(card.first) && has_local)
			continue; //the high resolution source doesn't have it, the picture we have is all there is
		out.push_back(Job{ card.first, has_local, not_found.count(card.first) != 0 });
	}
	return out;
}

ArtSync::ArtSync(std::vector<Job> job_list) : jobs(std::move(job_list)), total(jobs.size()), started(std::chrono::steady_clock::now()) {
	worker = std::thread(&ArtSync::Run, this);
}

ArtSync::~ArtSync() {
	stop = true;
	if(worker.joinable())
		worker.join();
}

double ArtSync::ElapsedSeconds() const {
	return std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
}

void ArtSync::Run() {
	try {
		RunInner();
	} catch(...) {
		finished = true;
	}
}

void ArtSync::RunInner() {
	using clock = std::chrono::steady_clock;
	std::error_code ec;
	std::filesystem::create_directories("pics/temp", ec);
	std::filesystem::create_directories("cardcache", ec);
	CURLM* multi = curl_multi_init();
	if(!multi) {
		finished = true;
		return;
	}
	curl_multi_setopt(multi, CURLMOPT_MAXCONNECTS, static_cast<long>(PARALLEL * 2));
	std::deque<Transfer*> waiting;
	struct Delayed {
		Transfer* transfer;
		clock::time_point ready;
	};
	std::vector<Delayed> delayed;
	std::vector<std::unique_ptr<Transfer>> storage;
	std::unordered_map<CURL*, Transfer*> active;
	for(const auto& job : jobs) {
		storage.push_back(std::make_unique<Transfer>());
		auto* transfer = storage.back().get();
		transfer->job = job;
		transfer->fallback = job.skip_primary;
		waiting.push_back(transfer);
	}
	const std::string cert = gGameConfig->ssl_certificate_path;
	const bool use_cert = !cert.empty() && Utils::FileExists(Utils::ToPathString(cert));
	std::vector<uint32_t> done_codes, notfound_codes;
	auto flush_lists = [&]() {
		AppendList(DONE_FILE, done_codes);
		AppendList(NOTFOUND_FILE, notfound_codes);
		done_codes.clear();
		notfound_codes.clear();
	};
	clock::time_point next_start = clock::now();
	clock::time_point last_success = clock::now();
	clock::time_point last_flush = clock::now();
	int network_failures = 0;
	double rate = REQUESTS_PER_SECOND;
	clock::time_point last_halved = clock::now() - std::chrono::seconds(60);
	auto interval = std::chrono::duration_cast<clock::duration>(std::chrono::duration<double>(1.0 / rate));

	auto finish_job = [&](Transfer* transfer) {
		processed++;
		//the connection stays in the multi handle's pool; the handle and the picture buffer are not needed any more
		if(transfer->easy) {
			curl_easy_cleanup(transfer->easy);
			transfer->easy = nullptr;
		}
		std::string().swap(transfer->body);
	};
	auto retry_or_fail = [&](Transfer* transfer, bool network_error) {
		if(network_error)
			network_failures++;
		if(++transfer->attempts < MAX_ATTEMPTS) {
			delayed.push_back({ transfer, clock::now() + std::chrono::seconds(2 * transfer->attempts) });
		} else {
			failed++;
			finish_job(transfer);
		}
	};

	while(!stop && (processed.load() < total)) {
		const auto now = clock::now();
		for(auto it = delayed.begin(); it != delayed.end();) {
			if(it->ready <= now) {
				waiting.push_front(it->transfer);
				it = delayed.erase(it);
			} else {
				++it;
			}
		}
		//start transfers, at most REQUESTS_PER_SECOND per second
		while(active.size() < PARALLEL && !waiting.empty() && clock::now() >= next_start) {
			auto* transfer = waiting.front();
			waiting.pop_front();
			next_start = std::max(clock::now(), next_start) + interval;
			if(!transfer->easy) {
				transfer->easy = curl_easy_init();
				if(!transfer->easy) {
					failed++;
					processed++;
					continue;
				}
			}
			auto* easy = transfer->easy;
			transfer->body.clear();
			const std::string url = std::string(transfer->fallback ? FALLBACK_URL : PRIMARY_URL) + std::to_string(transfer->job.code) + ".jpg";
			curl_easy_setopt(easy, CURLOPT_URL, url.c_str());
			curl_easy_setopt(easy, CURLOPT_USERAGENT, "EDOproextend-art-sync/1.0");
			curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, WriteBody);
			curl_easy_setopt(easy, CURLOPT_WRITEDATA, &transfer->body);
			curl_easy_setopt(easy, CURLOPT_ERRORBUFFER, transfer->error);
			curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT, 15L);
			curl_easy_setopt(easy, CURLOPT_LOW_SPEED_LIMIT, 2000L);
			curl_easy_setopt(easy, CURLOPT_LOW_SPEED_TIME, 20L);
			curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);
			curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
			curl_easy_setopt(easy, CURLOPT_TCP_KEEPALIVE, 1L);
			if(use_cert)
				curl_easy_setopt(easy, CURLOPT_CAINFO, cert.c_str());
			curl_easy_setopt(easy, CURLOPT_PRIVATE, transfer);
			curl_multi_add_handle(multi, easy);
			active[easy] = transfer;
		}
		int running = 0;
		curl_multi_perform(multi, &running);
		int numfds = 0;
		curl_multi_wait(multi, nullptr, 0, 40, &numfds);
		int left = 0;
		while(CURLMsg* message = curl_multi_info_read(multi, &left)) {
			if(message->msg != CURLMSG_DONE)
				continue;
			auto* easy = message->easy_handle;
			auto found = active.find(easy);
			if(found == active.end())
				continue;
			auto* transfer = found->second;
			active.erase(found);
			curl_multi_remove_handle(multi, easy);
			long status = 0;
			curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &status);
			const auto result = message->data.result;
			if(result == CURLE_OK && status == 200 && LooksLikeJpeg(transfer->body)) {
				const auto temp = std::filesystem::path("pics/temp") / (std::to_string(transfer->job.code) + ".dl");
				const auto dest = std::filesystem::path("pics") / (std::to_string(transfer->job.code) + ".jpg");
				bool saved = false;
				{
					std::ofstream file(temp, std::ios::binary | std::ios::trunc);
					file.write(transfer->body.data(), static_cast<std::streamsize>(transfer->body.size()));
					saved = file.good();
				}
				if(saved) {
					std::error_code rename_ec;
					std::filesystem::rename(temp, dest, rename_ec);
					saved = !rename_ec;
				}
				if(saved) {
					bytes += transfer->body.size();
					downloaded++;
					network_failures = 0;
					last_success = clock::now();
					if(rate < REQUESTS_PER_SECOND) {
						//slowly speed up again after a slow-down
						rate = std::min(REQUESTS_PER_SECOND, rate * 1.01);
						interval = std::chrono::duration_cast<clock::duration>(std::chrono::duration<double>(1.0 / rate));
					}
					if(!transfer->fallback)
						done_codes.push_back(transfer->job.code);
					finish_job(transfer);
				} else {
					retry_or_fail(transfer, false);
				}
			} else if(result == CURLE_OK && (status == 404 || status == 410)) {
				//the server doesn't have this card
				network_failures = 0;
				last_success = clock::now();
				if(!transfer->fallback) {
					notfound_codes.push_back(transfer->job.code);
					if(!transfer->job.has_local) {
						transfer->fallback = true;
						transfer->attempts = 0;
						waiting.push_back(transfer);
						continue;
					}
				}
				missing++;
				finish_job(transfer);
			} else {
				if(result == CURLE_OK && (status == 429 || status == 403 || status == 503 || status == 200)) {
					//the server is pushing back (or sent an error page): pause and halve the speed, once per burst of errors
					if(clock::now() - last_halved > std::chrono::seconds(8)) {
						last_halved = clock::now();
						next_start = clock::now() + std::chrono::seconds(10);
						rate = std::max(MIN_REQUESTS_PER_SECOND, rate / 2);
						interval = std::chrono::duration_cast<clock::duration>(std::chrono::duration<double>(1.0 / rate));
					}
				}
				retry_or_fail(transfer, result != CURLE_OK);
			}
		}
		if(network_failures >= 30 && clock::now() - last_success > std::chrono::seconds(15))
			offline = true;
		else if(network_failures == 0)
			offline = false;
		if(clock::now() - last_flush > std::chrono::seconds(5)) {
			flush_lists();
			last_flush = clock::now();
		}
		if(waiting.empty() && active.empty() && delayed.empty() && processed.load() < total)
			break; //nothing left that could finish (should not happen)
		if(running == 0 && active.empty() && !waiting.empty() && clock::now() < next_start)
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		else if(active.empty() && !delayed.empty())
			std::this_thread::sleep_for(std::chrono::milliseconds(20)); //only waiting for retries: do not spin
	}
	flush_lists();
	for(auto& entry : active)
		curl_multi_remove_handle(multi, entry.first);
	for(auto& transfer : storage) {
		if(transfer->easy)
			curl_easy_cleanup(transfer->easy);
	}
	curl_multi_cleanup(multi);
	finished = true;
}

}
