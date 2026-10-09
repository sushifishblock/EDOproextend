#ifndef IMAGEMANAGER_H
#define IMAGEMANAGER_H

#include "config.h"
#include <path.h>
#include <rect.h>
#include <chrono>
#include <thread>
#include <vector>
#include <deque>
#include <fstream>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <atomic>
#include <queue>
#include "epro_mutex.h"
#include "epro_condition_variable.h"
#include "epro_thread.h"

namespace irr {
class IrrlichtDevice;
namespace io {
class IReadFile;
}
namespace video {
class IImage;
class ITexture;
class IVideoDriver;
class SColor;
}
}

namespace ygo {

//timing counters for the card preload, a report is written to preload_profile.txt when EDOPRO_PROFILE is set
struct PreloadProfile {
	std::atomic<uint64_t> loader_ns{ 0 }, stamp_ns{ 0 }, cache_read_ns{ 0 }, decode_ns{ 0 }, scale_ns{ 0 }, cache_write_ns{ 0 };
	std::atomic<uint64_t> cache_hits{ 0 }, cache_misses{ 0 }, loaded{ 0 };
	std::atomic<uint64_t> refresh_ns{ 0 }, add_texture_ns{ 0 }, textures{ 0 };
	std::atomic<uint64_t> update_ns{ 0 }, has_local_ns{ 0 }, frames{ 0 };
	std::atomic<uint64_t> index_build_ns{ 0 }, pack_load_ns{ 0 }, cache_entries{ 0 };
	double start_age_ms = 0;
	std::atomic<uint64_t> idle_ns{ 0 }, jpeg_decode_ns{ 0 }, image_create_ns{ 0 }, lock_wait_ns{ 0 };
	uint64_t queue_samples = 0, queue_to_load_sum = 0, queue_finished_sum = 0;
	std::chrono::steady_clock::time_point start;
	std::chrono::steady_clock::time_point last_texture;
	bool started = false;
	bool reported = false;
	std::vector<std::pair<uint64_t, double>> checkpoints;
};
struct ProfileScope {
	std::atomic<uint64_t>& accumulator;
	std::chrono::steady_clock::time_point begin;
	explicit ProfileScope(std::atomic<uint64_t>& acc) : accumulator(acc), begin(std::chrono::steady_clock::now()) {}
	~ProfileScope() {
		accumulator += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - begin).count();
	}
};

#ifndef IMGTYPE
#define IMGTYPE
enum imgType {
	ART,
	FIELD,
	COVER,
	THUMB
};
#endif

class ImageManager {
private:
	using chrono_time = uint32_t;
	enum class preloadStatus {
		NONE,
		LOADING,
		LOADED,
		WAIT_DOWNLOAD,
	};
	struct texture_map_entry {
		preloadStatus preload_status;
		irr::video::ITexture* texture;
	};
	using texture_map = std::unordered_map<uint32_t, texture_map_entry>;
	struct load_parameter {
		uint32_t code;
		imgType type;
		size_t index;
		const std::atomic<irr::s32>& reference_width;
		const std::atomic<irr::s32>& reference_height;
		chrono_time timestamp;
		const std::atomic<chrono_time>& reference_timestamp;
		load_parameter(uint32_t code_, imgType type_, size_t index_, const std::atomic<irr::s32>& reference_width_,
					   const std::atomic<irr::s32>& reference_height_, chrono_time timestamp_, const std::atomic<chrono_time>& reference_timestamp_) :
			code(code_), type(type_), index(index_), reference_width(reference_width_),
			reference_height(reference_height_), timestamp(timestamp_),
			reference_timestamp(reference_timestamp_) {
		}
	};
	enum class loadStatus {
		LOAD_OK,
		LOAD_FAIL,
		WAIT_DOWNLOAD,
	};
	struct load_return {
		loadStatus status;
		uint32_t code;
		irr::video::IImage* texture;
		epro::path_string path;
	};
public:
	ImageManager();
	~ImageManager();
	bool Initial();
	void ChangeTextures(epro::path_stringview path);
	void ResetTextures();
	void SetDevice(irr::IrrlichtDevice* dev);
	void ClearTexture(bool resize = false);
	//frees the small per-card textures (deck editor thumbnails, info panel art) but keeps the main card art cache
	//and does not cancel anything that is still loading in the background
	void ClearTexturesKeepCardArt();
	void RefreshCachedTextures();
	//card browser: keeps card textures in memory up to budget_bytes and frees the least recently used ones
	//first. Cards in `keep` (on screen or about to be) are never freed. Queued loads for cards that are no longer
	//wanted are dropped. info_code is the card shown in the info panel.
	void TrimCardTextures(size_t budget_bytes, const std::unordered_set<uint32_t>& keep, const std::unordered_set<uint32_t>& keep_large, uint32_t info_code);
	//width in pixels of the pictures kept in the main card cache
	int CardTextureWidth() const { return sizes[0].first; }
	//how many milliseconds per frame may be spent creating card textures from loaded pictures
	void SetUploadBudget(int milliseconds) { upload_budget_ms = milliseconds; }
	//size of the large card picture (the preview at the top left); 0, 0 goes back to the size of the preview area
	void SetLargeCardTextureSize(int width, int height);
	//card browser background preload helpers
	size_t PendingCardLoads();
	void SampleQueues();
	mutable PreloadProfile profile;
	//milliseconds since the process was started
	static double ProcessAgeMs();
	//builds (again, when the picture folders changed) the list of the pictures that exist on disk with their size and
	//modification time, so that asking for a picture does not need any file system call
	void EnsureLocalPictureIndex();
	//writes what is still buffered of the picture cache to disk
	void FlushCardCache();
	void WritePreloadProfile();
	//changes every time the card textures are cleared, so background loaders know to start over
	uint32_t CacheGeneration() const { return cache_generation; }
	//size of the card pictures kept in the main texture cache; 0, 0 restores the normal size. Changing it drops
	//every cached card texture so they reload at the new size (the card browser uses a smaller size so that
	//thousands of cards fit in memory at once)
	void SetCardTextureSize(int width, int height);
	bool HasLocalCardPicture(uint32_t code) const;
	void ClearCachedTextures();
	static bool imageScaleNNAA(irr::video::IImage* src, irr::video::IImage* dest, chrono_time timestamp_id, const std::atomic<chrono_time>& source_timestamp_id);
	irr::video::IImage* GetScaledImage(irr::video::IImage* srcimg, int width, int height, chrono_time timestamp_id, const std::atomic<chrono_time>& source_timestamp_id);
	irr::video::IImage* GetScaledImageFromFile(const irr::io::path& file, int width, int height);
	irr::video::ITexture* GetTextureFromFile(const irr::io::path& file, int width, int height);
	irr::video::ITexture* GetTextureCard(uint32_t code, imgType type, bool wait = false, bool fit = false, int* chk = nullptr);
	irr::video::ITexture* GetTextureField(uint32_t code);
	irr::video::ITexture* GetCheckboxScaledTexture(float scale);
	irr::video::ITexture* guiScalingResizeCached(irr::video::ITexture* src, const irr::core::rect<irr::s32>& srcrect,
												 const irr::core::rect<irr::s32> &destrect);
	void draw2DImageFilterScaled(irr::video::ITexture* txr,
								 const irr::core::rect<irr::s32>& destrect, const irr::core::rect<irr::s32>& srcrect,
								 const irr::core::rect<irr::s32>* cliprect = nullptr, const irr::video::SColor* const colors = nullptr,
								 bool usealpha = false);
private:
	texture_map tMap[2];
	texture_map tThumb;
	std::unordered_map<uint32_t, uint64_t> card_last_use;
	uint64_t card_use_tick = 0;
	std::atomic<int> upload_budget_ms{ 6 };
	int large_override_width = 0;
	int large_override_height = 0;
	std::atomic<uint32_t> cache_generation{ 0 };
	std::unordered_map<uint32_t, irr::video::ITexture*> tFields;
	texture_map tCovers;
	irr::IrrlichtDevice* device;
	irr::video::IVideoDriver* driver;
public:
	irr::video::ITexture* tCover[2];
	irr::video::ITexture* tUnknown;
#define A(what) \
		public: \
		irr::video::ITexture* what;\
		private: \
		irr::video::ITexture* def_##what;
	A(tAct)
	A(tAttack)
	A(tNegated)
	A(tChain)
	A(tNumber)
	A(tLPFrame)
	A(tLPBar)
	A(tMask)
	A(tEquip)
	A(tTarget)
	A(tChainTarget)
	A(tLim)
	A(tOT)
	A(tHand[3])
	A(tBackGround)
	A(tBackGround_menu)
	A(tBackGround_deck)
	A(tBackGround_duel_topdown)
	A(tField[2][4])
	A(tFieldTransparent[2][4])
	A(tSettings)
	A(tCheckBox[3])
#undef A
private:
	void ClearFutureObjects();
	//decodes a card picture, a JPEG is decoded at a reduced size when the result would still be at least min_w x min_h
	//which is several times faster than decoding the full picture
	irr::video::IImage* LoadCardImageFast(irr::io::IReadFile* file, int min_w, int min_h);
	irr::video::IImage* LoadCardImageFastFromPath(const irr::io::path& path, int min_w, int min_h);
	//persistent cache of the loaded card pictures (./cardcache/<code>.ecc), a small JPEG per card plus the size and
	//modification time of the original picture so a changed picture is picked up again
	bool FindCardSourceStamp(uint32_t code, uint64_t& size, int64_t& mtime) const;
	struct LocalPicture {
		uint64_t size;
		int64_t mtime;
	};
	mutable std::shared_mutex local_index_mutex;
	std::unordered_map<uint32_t, LocalPicture> local_pictures;
	std::atomic<bool> local_index_ready{ false };
	size_t local_index_dirs = 0;
	void BuildLocalPictureIndex();
	//the picture cache is one file (cardcache/cards.pack) holding a small JPEG per card, read once at start
	struct CardCacheEntry {
		uint32_t width;
		uint32_t height;
		uint64_t src_size;
		int64_t src_mtime;
		const uint8_t* data;
		uint32_t length;
	};
	mutable std::shared_mutex card_cache_mutex;
	std::unordered_map<uint32_t, CardCacheEntry> card_cache;
	std::vector<uint8_t> card_cache_blob;
	std::deque<std::vector<uint8_t>> card_cache_extra;
	std::once_flag card_cache_loaded;
	std::thread card_cache_preload_thread;
	uint64_t card_cache_valid_length = 0;
	bool card_cache_file_ok = false;
	std::mutex card_cache_write_mutex;
	std::ofstream card_cache_out;
	int card_cache_unflushed = 0;
	void LoadCardCachePack();
	bool OpenCardCacheForAppend();
	irr::video::IImage* ReadCardCache(uint32_t code, int width, int height, uint64_t src_size, int64_t src_mtime, bool any_size = false);
	void WriteCardCache(uint32_t code, irr::video::IImage* image, uint64_t src_size, int64_t src_mtime);
	void RefreshCovers();
	void LoadPic();
	irr::video::ITexture* loadTextureFixedSize(epro::path_stringview texture_name, int width, int height);
	irr::video::ITexture* loadTextureAnySize(epro::path_stringview texture_name);
	void replaceTextureLoadingFixedSize(irr::video::ITexture*& texture, irr::video::ITexture* fallback, epro::path_stringview texture_name, int width, int height);
	void replaceTextureLoadingAnySize(irr::video::ITexture*& texture, irr::video::ITexture* fallback, epro::path_stringview texture_name);
	load_return LoadCardTexture(uint32_t code, imgType type, const std::atomic<irr::s32>& width, const std::atomic<irr::s32>& height, chrono_time timestamp_id, const std::atomic<chrono_time>& source_timestamp_id);
	epro::path_string textures_path;
	std::pair<std::atomic<irr::s32>, std::atomic<irr::s32>> sizes[3];
	std::atomic<chrono_time> timestamp_id;
	std::map<epro::path_string, irr::video::ITexture*> g_txrCache;
	std::map<irr::io::path, irr::video::IImage*> g_imgCache; //ITexture->getName returns a io::path
	epro::mutex obj_clear_lock;
	epro::thread obj_clear_thread;
	epro::condition_variable cv_clear;
	std::deque<load_return> to_clear;
	std::atomic<bool> stop_threads;
	epro::condition_variable cv_load;
	std::deque<load_parameter> to_load;
	std::deque<load_return> loaded_pics[4];
	epro::mutex pic_load;
	//bool stop_threads;
	std::vector<epro::thread> load_threads;
};

#define CARD_IMG_WIDTH		177
#define CARD_IMG_HEIGHT		254
#define CARD_IMG_WIDTH_F	177.0f
#define CARD_IMG_HEIGHT_F	254.0f
#define CARD_THUMB_WIDTH	44
#define CARD_THUMB_HEIGHT	64

}

#endif // IMAGEMANAGER_H
