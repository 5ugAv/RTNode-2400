#pragma once

#ifdef HAS_RNS

#include <FileSystem.h>
#include <FileStream.h>
#include <Bytes.h>
#include <Type.h>

#include <Stream.h>

class FileSystem : public RNS::FileSystemImpl {

public:
	FileSystem() {}

	bool init();
	bool format();
	bool reformat();

	// CBA Debug
	static void listDir(const char* dir, const char* prefix = "");
	static void dumpDir(const char* dir);

public:
	virtual bool file_exists(const char* file_path);
	virtual size_t read_file(const char* file_path, RNS::Bytes& data);
	virtual size_t write_file(const char* file_path, const RNS::Bytes& data);
	virtual RNS::FileStream open_file(const char* file_path, RNS::FileStream::MODE file_mode);
	virtual bool remove_file(const char* file_path);
	virtual bool rename_file(const char* from_file_path, const char* to_file_path);
	virtual bool directory_exists(const char* directory_path);
	virtual bool create_directory(const char* directory_path);
	virtual bool remove_directory(const char* directory_path);
	virtual std::list<std::string> list_directory(const char* directory_path);
	virtual size_t storage_size();
	virtual size_t storage_available();

};

// ── SD overflow tier status (implemented in FileSystem.cpp) ──────────────────
// Safe to call from anywhere; return false/0/"[]" when the tier is disabled or
// no card is present. Used by the /status endpoint to confirm the path table +
// cache physically live on the microSD card.
#include <Arduino.h>
bool     fs_sd_overflow_ready();
uint64_t fs_sd_card_size_bytes();
uint64_t fs_sd_total_bytes();
uint64_t fs_sd_used_bytes();
String   fs_sd_overflow_listing();

#endif
