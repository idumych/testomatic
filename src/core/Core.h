// Portable core: no Haiku headers, so it builds and is tested on any host.
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <sys/types.h>

// A bootable RenkuOS image attached to a GitHub release.
struct Build {
	std::string name;	// RenkuOS-x86_64-20261120-build00095.iso
	std::string path;	// an image file already on disk: nothing to download
	std::string url;
	std::string shaUrl;	// "" when the release has no .sha256 for it
	std::string arch;	// x86_64, x86_gcc2h
	std::string date;	// 20261120
	std::string number;	// 00095
	int64_t size = 0;
};

// Images from the GitHub releases API JSON, newest first, x86_64 first.
std::vector<Build> ParseReleases(const std::string& json);
std::string Describe(const Build& build);
std::string FormatSize(int64_t bytes);

struct Drive {
	std::string device;	// /dev/sdb, /dev/disk/usb/0/0/raw
	std::string model;
	int64_t size = 0;
};
std::string Describe(const Drive& drive);

// USB disks from `lsblk -P -p -b -o NAME,SIZE,TRAN,TYPE,MODEL,MOUNTPOINT,PKNAME`,
// leaving out any disk that holds the running system.
std::vector<Drive> ParseLsblk(const std::string& output);

// Bytes copied, from a GNU dd status=progress line; -1 when it has none.
int64_t ParseDdBytes(const std::string& line);

// The hash in a sha256sum line ("<hex>  <file>"), lowercase; "" if none.
std::string ParseSha256(const std::string& text);

// Starts argv with stdout and stderr on one pipe, in its own process group.
// Returns the pid (-1 on failure); *fd is the pipe's read end.
pid_t Spawn(const std::vector<std::string>& argv, int* fd);
// Exit code, or -1 if it died from a signal.
int Wait(pid_t pid);
// Runs argv to completion, collecting its output.
int Run(const std::vector<std::string>& argv, std::string* output);
