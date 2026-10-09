#ifndef AI_ASSISTANT_H
#define AI_ASSISTANT_H

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace ygo {

//what the assistant needs to know about a card, copied once on the main thread so the worker thread never touches the game's card data
struct AiCard {
	uint32_t code = 0;
	uint32_t type = 0;
	uint64_t race = 0;
	uint32_t attribute = 0;
	uint32_t level = 0;
	uint32_t lscale = 0;
	uint32_t rscale = 0;
	int32_t attack = 0;
	int32_t defense = 0;
	uint32_t link_marker = 0;
	uint32_t ot = 0;
	std::string name;
	std::string text;
	std::string name_lower;
	std::string text_lower;
	std::string document; //the text that is turned into an embedding
	uint64_t document_hash = 0;
};

struct AiResult {
	bool ok = false;
	std::wstring message; //what the assistant understood, or what went wrong
	std::vector<uint32_t> codes; //best match first
	bool ranked_by_meaning = false; //false when only the filters were used and the caller may sort the cards
};

//Natural language card search with a local language model (llama.cpp's llama-server, started in the background):
//one model turns the question into filters + a description of the wanted effect, a second model turns card texts
//and the description into embeddings so the cards can be ranked by meaning.
class AiAssistant {
public:
	AiAssistant();
	~AiAssistant();
	//the ai folder with the runner and the models is there
	bool IsInstalled() const { return installed; }
	//main thread: copies what is needed about every card
	static std::vector<AiCard> BuildSnapshot();
	//starts the model servers (if needed) and builds/updates the card index in the background
	void Start(std::vector<AiCard> snapshot);
	//the card browser was closed: the servers are shut down after a while
	void ScheduleStop();
	void Ask(std::wstring question, bool include_unofficial);
	//main thread, once per frame: true when a new answer is available
	bool PollResult(AiResult& result);
	//one line for the user about what the assistant is doing right now
	std::wstring GetStatus();
	bool IsBusy() const { return busy; }

private:
	struct AskTask {
		std::string question;
		bool include_unofficial;
	};
	struct Process;
	void WorkerLoop();
	void SetStatus(std::wstring status);
	bool EnsureServers();
	bool WaitForServer(int port, const std::chrono::seconds& timeout);
	void StopServers();
	bool EnsureIndex();
	bool BuildIndex();
	bool LoadIndex();
	bool SaveIndex();
	bool EmbedTexts(const std::vector<std::string>& texts, std::vector<std::vector<float>>& out);
	AiResult HandleAsk(const AskTask& task);

	bool installed = false;
	std::thread worker;
	std::mutex mutex;
	std::condition_variable cv;
	bool quit = false;
	bool start_requested = false;
	std::vector<AiCard> pending_snapshot;
	bool has_pending_snapshot = false;
	std::deque<AskTask> asks;
	std::chrono::steady_clock::time_point stop_at;
	bool stop_scheduled = false;
	AiResult result;
	bool has_result = false;
	std::wstring status;
	std::atomic<bool> busy{ false };

	//worker thread only
	std::vector<AiCard> cards;
	std::vector<float> embeddings; //row after row, each row normalized
	std::vector<uint32_t> embedding_ids;
	std::vector<uint64_t> embedding_hashes;
	std::unordered_map<uint32_t, size_t> embedding_row;
	size_t dimension = 0;
	bool index_ready = false;
	std::unique_ptr<Process> chat_process;
	std::unique_ptr<Process> embed_process;
	std::string system_prompt;
	std::string schema_text;
};

}

#endif
