#include "FileSystem.h"
#include "FileStream.h"
#include "FileSystemType.h"

#ifdef HAS_RNS

#include <Log.h>

#if FS_TYPE == FS_TYPE_INTERNALFS

inline int _countLfsBlock(void *p, lfs_block_t block) {
	lfs_size_t *size = (lfs_size_t*) p;
	*size += 1;
	return 0;
}

lfs_ssize_t usedBlocks() {
    lfs_size_t size = 0;
    lfs_traverse(FS._getFS(), _countLfsBlock, &size);
    return size;
}

size_t usedBytes() {
	const lfs_config* config = FS._getFS()->cfg;
	const size_t usedBlockCount = usedBlocks();
	return config->block_size * usedBlockCount;
}

size_t totalBytes() {
	const lfs_config* config = FS._getFS()->cfg;
	return config->block_size * config->block_count;
}

#elif FS_TYPE == FS_TYPE_FLASHFS

Adafruit_FlashTransport_SPI g_flashTransport(SS, SPI);

//Flash definition structure for GD25Q16C Flash (RAK15001)
Cached_SPIFlash g_flash(&g_flashTransport);
SPIFlash_Device_t g_RAK15001 {
	.total_size = (1UL << 21),
	.start_up_time_us = 5000,
	.manufacturer_id = 0xc8,
	.memory_type = 0x40,
	.capacity = 0x15,
	.max_clock_speed_mhz = 15,
	.quad_enable_bit_mask = 0x00,
	.has_sector_protection = false,
	.supports_fast_read = true,
	.supports_qspi = false,
	.supports_qspi_writes = false,
	.write_status_register_split = false,
	.single_status_byte = true,
};

#endif


// ── SD overflow tier (prototype) ─────────────────────────────────────────────
// On boards with an SD card (e.g. T-Beam Supreme) route the bulk, growth-prone
// Reticulum state — the path table (/destination_table), packet cache (/cache),
// and hashlist (/packet_hashlist) — onto SD, while identity/config stay on the
// primary flash FS. This lets a car transport node store/relay far more than
// internal flash allows, and degrades gracefully to flash-only if no card is up.
//
// Enabled per-env with -DFILESYSTEM_SD_OVERFLOW=1. Dormant otherwise: every path
// resolves to the primary FS, so existing (flash-only) builds are unchanged.
//
// NOTE: on the Supreme the SD card's 3v3 rail (AXP2101 BLDO1) must be powered
// BEFORE init_sd_overflow() runs — bring up the PMU first in setup().
#ifndef FILESYSTEM_SD_OVERFLOW
#define FILESYSTEM_SD_OVERFLOW 0
#endif

#if (FS_TYPE == FS_TYPE_LITTLEFS || FS_TYPE == FS_TYPE_SPIFFS)
// ESP32 fs::FS world: LittleFS/SPIFFS and SD are all fs::FS, so choosing a
// backend per path is just a reference swap.

  // `FS` is an object-like macro (= LittleFS/SPIFFS). Suspend it while we pull in
  // SD.h and name the shared base class: SD.h declares `class SDFS : public FS`,
  // and the bare token `fs::FS` would otherwise expand to `fs::LittleFS`.
  #pragma push_macro("FS")
  #undef FS
  #if FILESYSTEM_SD_OVERFLOW
    #include <SD.h>
  #endif
  using RNodeFsBase = fs::FS;
  #pragma pop_macro("FS")

  #if FILESYSTEM_SD_OVERFLOW
    #include <cstring>
    #include <SPI.h>

    static SPIClass sd_overflow_spi(HSPI);
    static bool     sd_overflow_ready = false;

    // Bulk / growth-prone Reticulum state that belongs on SD. Everything else
    // (identity, eeprom, config, time_offset) stays on the primary flash FS.
    static bool path_is_overflow(const char* p) {
      if (p == nullptr) { return false; }
      return strncmp(p, "/cache", 6) == 0
          || strncmp(p, "/destination_table", 18) == 0
          || strncmp(p, "/packet_hashlist", 16) == 0;
    }

    // Resolve the backend for a path: SD for overflow paths when the card is up,
    // otherwise the primary flash FS (also the fallback when SD is absent).
    static RNodeFsBase& backendFor(const char* p) {
      if (sd_overflow_ready && path_is_overflow(p)) { return SD; }
      return FS;
    }

    // Bring up the SD card on the board's shared SPI bus. Caller MUST have
    // already enabled the card's power rail (Supreme: AXP2101 BLDO1) first.
    static bool init_sd_overflow() {
      #ifdef IMU_CS
        // The IMU shares this SPI bus on the Supreme — park its CS high.
        pinMode(IMU_CS, OUTPUT);
        digitalWrite(IMU_CS, HIGH);
      #endif
      sd_overflow_spi.begin(SD_CLK, SD_MISO, SD_MOSI, SD_CS);
      sd_overflow_ready = SD.begin(SD_CS, sd_overflow_spi);
      return sd_overflow_ready;
    }
  #else
    // Overflow disabled: all paths resolve to the primary flash FS.
    static inline RNodeFsBase& backendFor(const char*) { return FS; }
  #endif

  // Route a filesystem op for the given path to its chosen backend.
  #define FS_FOR(p) backendFor(p)
#else
  // Non-ESP32 filesystems (nRF52 InternalFS/FlashFS): no SD routing.
  #define FS_FOR(p) FS
#endif


// ── SD overflow status accessors (for the /status endpoint & health beacon) ──
// External linkage (declared in FileSystem.h). Return safe defaults when the
// overflow tier is disabled or no card is present, so callers need no #ifdefs.
#include <Arduino.h>

#if (FS_TYPE == FS_TYPE_LITTLEFS || FS_TYPE == FS_TYPE_SPIFFS) && FILESYSTEM_SD_OVERFLOW

bool     fs_sd_overflow_ready()  { return sd_overflow_ready; }
uint64_t fs_sd_card_size_bytes() { return sd_overflow_ready ? SD.cardSize()  : 0; }
uint64_t fs_sd_total_bytes()     { return sd_overflow_ready ? SD.totalBytes() : 0; }
uint64_t fs_sd_used_bytes()      { return sd_overflow_ready ? SD.usedBytes()  : 0; }

// JSON array of the overflow files physically present on the card, so a remote
// /status query can confirm the path table + cache actually live on SD.
String fs_sd_overflow_listing() {
  if (!sd_overflow_ready) { return "[]"; }
  String out = "[";
  bool first = true;
  const char* dirs[] = { "/", "/cache" };
  for (uint8_t i = 0; i < 2; i++) {
    File d = SD.open(dirs[i]);
    if (!d) { continue; }
    if (!d.isDirectory()) { d.close(); continue; }
    File f = d.openNextFile();
    while (f) {
      if (!f.isDirectory()) {
        if (!first) { out += ","; }
        first = false;
        String base(dirs[i]);
        out += "{\"path\":\"";
        out += base;
        if (base != "/") { out += "/"; }
        out += f.name();
        out += "\",\"bytes\":";
        out += (uint32_t)f.size();
        out += "}";
      }
      f.close();
      f = d.openNextFile();
    }
    d.close();
  }
  out += "]";
  return out;
}

#else

bool     fs_sd_overflow_ready()  { return false; }
uint64_t fs_sd_card_size_bytes() { return 0; }
uint64_t fs_sd_total_bytes()     { return 0; }
uint64_t fs_sd_used_bytes()      { return 0; }
String   fs_sd_overflow_listing(){ return "[]"; }

#endif


bool FileSystem::init() {
	TRACE("Initializing filesystem...");
	try {
#if FS_TYPE == FS_TYPE_SPIFFS
		// Initialize SPIFFS
		INFO("SPIFFS mounting filesystem");
		if (!SPIFFS.begin(true, "")) {
			ERROR("SPIFFS filesystem mount failed");
			return false;
		}
		INFO("SPIFFS filesystem is ready");
#elif FS_TYPE == FS_TYPE_LITTLEFS
		// Initialize LittleFS
		INFO("LittleFS mounting filesystem");
		if (!LittleFS.begin(true, "")) {
			ERROR("LittleFS filesystem mount failed");
			return false;
		}
		DEBUG("LittleFS filesystem is ready");
#elif FS_TYPE == FS_TYPE_INTERNALFS
		// Initialize InternalFileSystem
		INFO("InternalFS mounting filesystem");
		if (!InternalFS.begin()) {
			ERROR("InternalFS filesystem mount failed");
			return false;
		}
		INFO("InternalFS filesystem is ready");
#elif FS_TYPE == FS_TYPE_FLASHFS
		// Initialize FlashFileSystem
		INFO("FlashFS mounting filesystem");
		if (!g_flash.begin(&g_RAK15001)) {
			ERROR("FlashFS failed to initialize");
			return false;
		}
		if (!FlashFS.begin(&g_flash)) {
			ERROR("FlashFS filesystem mount failed");
			return false;
		}
#endif
#if (FS_TYPE == FS_TYPE_LITTLEFS || FS_TYPE == FS_TYPE_SPIFFS) && FILESYSTEM_SD_OVERFLOW
		// Bring up the SD overflow tier. Requires the card's power rail to be on
		// already (Supreme: AXP2101 BLDO1, enabled during PMU init in setup()).
		if (init_sd_overflow()) {
			INFO("SD overflow tier mounted; routing /cache + path table to SD");
		} else {
			WARNING("SD overflow tier unavailable; falling back to flash-only");
		}
#endif
		// Ensure filesystem is writable and reformat if not
		RNS::Bytes test("test");
		if (write_file("/test", test) < 4) {
			HEAD("Failed to write test file, filesystem is being reformatted...", RNS::LOG_CRITICAL);
			//FS.format();
			reformat();
		}
		else {
			remove_file("/test");
		}
	}
	catch (std::exception& e) {
		//ERROR("FileSystem init Exception: " + std::string(e.what()));
		return false;
	}
	TRACE("Finished initializing");
	return true;
}

bool FileSystem::format() {
	INFO("Formatting filesystem...");
	try {
		if (!FS.format()) {
			ERROR("Format failed!");
			return false;
		}
		return true;
	}
	catch (std::exception& e) {
		ERROR("FileSystem reformat Exception: " + std::string(e.what()));
	}
	return false;
}

bool FileSystem::reformat() {
	INFO("Reformatting filesystem...");
	try {
		RNS::Bytes eeprom;
		read_file("/eeprom", eeprom);
		RNS::Bytes transport_identity;
		read_file("/transport_identity", transport_identity);
		//RNS::Bytes time_offset;
		//read_file("/time_offset", time_offset);
		if (!FS.format()) {
			ERROR("Format failed!");
			return false;
		}
		if (eeprom) {
			write_file("/eeprom", eeprom);
		}
		if (transport_identity) {
			write_file("/transport_identity", transport_identity);
		}
		//if (time_offset) {
		//	write_file("/time_offset", time_offset);
		//}
		return true;
	}
	catch (std::exception& e) {
		ERROR("FileSystem reformat Exception: " + std::string(e.what()));
	}
	return false;
}

#ifndef NDEBUG

void FileSystem::listDir(const char* dir, const char* prefix /*= ""*/) {
	Serial.print(prefix);
	std::string full_dir(dir);
	if (full_dir.compare("/") != 0) {
		full_dir += "/";
	}
	Serial.println(full_dir.c_str());
	std::string pre(prefix);
	pre.append("  ");
	try {
		File root = FS.open(dir);
		if (!root) {
			Serial.print(pre.c_str());
			Serial.println("(failed to open directory)");
			return;
		}
		File file = root.openNextFile();
		while (file) {
			char* name = (char*)file.name();
			std::string recurse_dir(full_dir);
			if (file.isDirectory()) {
				recurse_dir += name;
				listDir(recurse_dir.c_str(), pre.c_str());
			}
			else {
				Serial.print(pre.c_str());
				//Serial.print("FILE: ");
				Serial.print(name);
				Serial.print(" (");
				Serial.print(file.size());
				Serial.println(" bytes)");
			}
			file.close();
			file = root.openNextFile();
		}
		root.close();
	}
	catch (std::exception& e) {
		Serial.print("listDir Exception: ");
		Serial.println(e.what());
	}
}

void FileSystem::dumpDir(const char* dir) {
	Serial.print("DIR: ");
	std::string full_dir(dir);
	if (full_dir.compare("/") != 0) {
		full_dir += "/";
	}
	Serial.println(full_dir.c_str());
	try {
		File root = FS.open(dir);
		if (!root) {
			Serial.println("(failed to open directory)");
			return;
		}
		File file = root.openNextFile();
		while (file) {
			char* name = (char*)file.name();
			if (file.isDirectory()) {
				std::string recurse_dir(full_dir);
				recurse_dir += name;
				dumpDir(recurse_dir.c_str());
			}
			else {
				Serial.print("\nFILE: ");
				Serial.print(name);
				Serial.print(" (");
				Serial.print(file.size());
				Serial.println(" bytes)");
				char data[4096];
				size_t size = file.size();
				size_t read = file.readBytes(data, (size < sizeof(data)) ? size : sizeof(data));
				Serial.write(data, read);
				Serial.println("");
			}
			file.close();
			file = root.openNextFile();
		}
		root.close();
	}
	catch (std::exception& e) {
		Serial.print("dumpDir Exception: ");
		Serial.println(e.what());
	}
}

#endif


/*virtua*/ bool FileSystem::file_exists(const char* file_path) {
	TRACEF("file_exists: checking for existence of file %s", file_path);
/*
#if FS_TYPE == FS_TYPE_INTERNALFS || FS_TYPE == FS_TYPE_FLASHFS
	File file(FS);
	if (file.open(file_path, FILE_O_READ)) {
#else
	File file = FS.open(file_path, FILE_READ);
	if (file) {
#endif
		bool is_directory = file.isDirectory();
		file.close();
		return !is_directory;
	}
	return false;
*/
	return FS_FOR(file_path).exists(file_path);
}

/*virtua*/ size_t FileSystem::read_file(const char* file_path, RNS::Bytes& data) {
	TRACEF("read_file: reading from file %s", file_path);
	size_t read = 0;
#if FS_TYPE == FS_TYPE_INTERNALFS || FS_TYPE == FS_TYPE_FLASHFS
	File file(FS);
	if (file.open(file_path, FILE_O_READ)) {
#else
	File file = FS_FOR(file_path).open(file_path, FILE_READ);
	if (file) {
#endif
		size_t size = file.size();
		read = file.readBytes((char*)data.writable(size), size);
		TRACEF("read_file: read %u bytes from file %s", read, file_path);
		if (read != size) {
			ERRORF("read_file: failed to read file %s", file_path);
            data.resize(read);
		}
		//TRACE("read_file: closing input file");
		file.close();
	}
	else {
		if (FS_FOR(file_path).exists(file_path)) {
			ERRORF("read_file: failed to open input file %s", file_path);
		} else {
			TRACEF("read_file: file %s does not exist (expected on first use)", file_path);
		}
	}
    return read;
}

/*virtua*/ size_t FileSystem::write_file(const char* file_path, const RNS::Bytes& data) {
	TRACEF("write_file: writing to file %s", file_path);
	// CBA TODO Replace remove with working truncation
	if (FS_FOR(file_path).exists(file_path)) {
		FS_FOR(file_path).remove(file_path);
	}
	size_t wrote = 0;
#if FS_TYPE == FS_TYPE_INTERNALFS || FS_TYPE == FS_TYPE_FLASHFS
	File file(FS);
	if (file.open(file_path, FILE_O_WRITE)) {
#else
	File file = FS_FOR(file_path).open(file_path, FILE_WRITE);
	if (file) {
#endif
		// Seek to beginning to overwrite
		//file.seek(0);
		//file.truncate(0);
		wrote = file.write(data.data(), data.size());
        TRACEF("write_file: wrote %u bytes to file %s", wrote, file_path);
        if (wrote < data.size()) {
			WARNINGF("write_file: not all data was written to file %s", file_path);
		}
		//TRACE("write_file: closing output file");
		file.close();
	}
	else {
		ERRORF("write_file: failed to open output file %s", file_path);
	}
    return wrote;
}

/*virtual*/ RNS::FileStream FileSystem::open_file(const char* file_path, RNS::FileStream::MODE file_mode) {
	TRACEF("open_file: opening file %s", file_path);
#if FS_TYPE == FS_TYPE_INTERNALFS || FS_TYPE == FS_TYPE_FLASHFS
	int mode;
	if (file_mode == RNS::FileStream::MODE_READ) {
		mode = FILE_O_READ;
	}
	else if (file_mode == RNS::FileStream::MODE_WRITE) {
		mode = FILE_O_WRITE;
		// CBA TODO Replace remove with working truncation
		if (FS.exists(file_path)) {
			FS.remove(file_path);
		}
	}
	else if (file_mode == RNS::FileStream::MODE_APPEND) {
		// CBA This is the default write mode for nrf52 littlefs
		mode = FILE_O_WRITE;
	}
	else {
		ERRORF("open_file: unsupported mode %d", file_mode);
		return {RNS::Type::NONE};
	}
	File* file = new File(FS);
	if (!file->open(file_path, mode)) {
		ERRORF("open_file: failed to open output file %s", file_path);
		return {RNS::Type::NONE};
	}
	// Seek to beginning to overwrite (this is failing on nrf52)
	//if (file_mode == RNS::FileStream::MODE_WRITE) {
	//	file->seek(0);
	//	file->truncate(0);
	//}
	TRACEF("open_file: successfully opened file %s", file_path);
	return RNS::FileStream(new FileStream(file));
#else
	const char* mode;
	if (file_mode == RNS::FileStream::MODE_READ) {
		mode = FILE_READ;
	}
	else if (file_mode == RNS::FileStream::MODE_WRITE) {
		mode = FILE_WRITE;
	}
	else if (file_mode == RNS::FileStream::MODE_APPEND) {
		mode = FILE_APPEND;
	}
	else {
		ERRORF("open_file: unsupported mode %d", file_mode);
		return {RNS::Type::NONE};
	}
	TRACEF("open_file: opening file %s in mode %s", file_path, mode);
	// CBA Using copy constructor to obtain File*
	File* file = new File(FS_FOR(file_path).open(file_path, mode));
	if (file == nullptr || !(*file)) {
		ERRORF("open_file: failed to open output file %s", file_path);
		return {RNS::Type::NONE};
	}
	TRACEF("open_file: successfully opened file %s", file_path);
	return RNS::FileStream(new FileStream(file));
#endif
}

/*virtua*/ bool FileSystem::remove_file(const char* file_path) {
	TRACEF("remove_file: removing file %s", file_path);
	return FS_FOR(file_path).remove(file_path);
}

/*virtua*/ bool FileSystem::rename_file(const char* from_file_path, const char* to_file_path) {
	TRACEF("rename_file: renaming file %s to %s", from_file_path, to_file_path);
	// NOTE: assumes both paths live on the same tier — cross-backend renames are
	// not supported (would need copy+delete). Reticulum renames within a tier.
	return FS_FOR(from_file_path).rename(from_file_path, to_file_path);
}

/*virtua*/ bool FileSystem::directory_exists(const char* directory_path) {
	TRACEF("directory_exists: checking for existence of directory %s", directory_path);
#if FS_TYPE == FS_TYPE_INTERNALFS || FS_TYPE == FS_TYPE_FLASHFS
	File file(FS);
	if (file.open(directory_path, FILE_O_READ)) {
#else
	File file = FS_FOR(directory_path).open(directory_path, FILE_READ);
	if (file) {
#endif
		bool is_directory = file.isDirectory();
		file.close();
		return is_directory;
	}
	return false;
}

/*virtua*/ bool FileSystem::create_directory(const char* directory_path) {
	TRACEF("create_directory: creating directory %s", directory_path);
	if (!FS_FOR(directory_path).mkdir(directory_path)) {
		ERROR("create_directory: failed to create directory " + std::string(directory_path));
		return false;
	}
	return true;
}

/*virtua*/ bool FileSystem::remove_directory(const char* directory_path) {
	TRACEF("remove_directory: removing directory %s", directory_path);
#if FS_TYPE == FS_TYPE_INTERNALFS || FS_TYPE == FS_TYPE_FLASHFS
	if (!FS.rmdir_r(directory_path)) {
#else
	if (!FS_FOR(directory_path).rmdir(directory_path)) {
#endif
		ERROR("remove_directory: failed to remove directory " + std::string(directory_path));
		return false;
	}
	return true;
}

/*virtua*/ std::list<std::string> FileSystem::list_directory(const char* directory_path) {
	TRACEF("list_directory: listing directory %s", directory_path);
	std::list<std::string> files;
	File root = FS_FOR(directory_path).open(directory_path);
	if (!root) {
		ERROR("list_directory: failed to open directory " + std::string(directory_path));
		return files;
	}
	File file = root.openNextFile();
	while (file) {
		if (!file.isDirectory()) {
			char* name = (char*)file.name();
			files.push_back(name);
		}
		// CBA Following close required to avoid leaking memory
		file.close();
		file = root.openNextFile();
	}
	root.close();
	TRACE("list_directory: returning directory listing");
	return files;
}

/*virtual*/ size_t FileSystem::storage_size() {
#if FS_TYPE == FS_TYPE_INTERNALFS
	return totalBytes();
#else
	return FS.totalBytes();
#endif
}

/*virtual*/ size_t FileSystem::storage_available() {
#if FS_TYPE == FS_TYPE_INTERNALFS
	return (totalBytes() - usedBytes());
#else
	return (FS.totalBytes() - FS.usedBytes());
#endif
}

#endif
