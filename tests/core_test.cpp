// Host-side tests for src/core: make test
#include "Core.h"

#include <cstdio>
#include <cstdlib>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { \
	fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #x); \
	failures++; } } while (0)

int main()
{
	// Shaped like the real GitHub API answer: the uploader object and
	// other fields sit between the assets' sizes and URLs.
	const char* json = R"([{"name":"Nightly build00095","tag_name":"nightly","assets":[
	 {"name":"RenkuOS-x86_gcc2h-20261120-build00095.iso","uploader":{"login":"bot","id":1},
	  "size":690814976,"digest":"sha256:ab","browser_download_url":"https://github.com/RenkuOS/Source/releases/download/nightly/RenkuOS-x86_gcc2h-20261120-build00095.iso"},
	 {"name":"RenkuOS-x86_64-20261120-build00095.iso","size":690814977,
	  "browser_download_url":"https://github.com/RenkuOS/Source/releases/download/nightly/RenkuOS-x86_64-20261120-build00095.iso"},
	 {"name":"x.sha256","size":105,"browser_download_url":"https://github.com/RenkuOS/Source/releases/download/nightly/RenkuOS-x86_64-20261120-build00095.iso.sha256"},
	 {"name":"renkuos-x86_64-repo.tar","size":5,"browser_download_url":"https://github.com/RenkuOS/Source/releases/download/nightly/renkuos-x86_64-repo.tar"}]},
	 {"name":"Old","assets":[{"size": 12,"browser_download_url":"https://x/RenkuOS-x86_64-20261001-build00080.iso"},
	  {"size":3,"browser_download_url":"https://x/RenkuOS-arm64-20261120-build00095.img.zst"}]},
	 {"name":"Firmware mirror","assets":[]}])";
	auto b = ParseReleases(json);
	CHECK(b.size() == 3);
	if (b.size() == 3) {
		CHECK(b[0].arch == "x86_64");	// newest, 64-bit first
		CHECK(b[0].size == 690814977);
		CHECK(!b[0].shaUrl.empty());
		CHECK(b[1].arch == "x86_gcc2h");
		CHECK(b[1].size == 690814976);
		CHECK(b[1].shaUrl.empty());
		CHECK(b[2].date == "20261001" && b[2].size == 12);
		CHECK(Describe(b[0]) == "Build 95, 20 Nov 2026, 64-bit (690 MB)");
	}
	Build local;
	local.path = "/home/u/Downloads/my.iso";
	local.size = 2500000000LL;
	CHECK(Describe(local) == "my.iso (2.5 GB)");
	CHECK(ParseReleases("{\"message\":\"API rate limit exceeded\"}").empty());

	const char* lsblk =
		"NAME=\"/dev/nvme0n1\" SIZE=\"512110190592\" TRAN=\"nvme\" TYPE=\"disk\" MODEL=\"Samsung\" MOUNTPOINT=\"\" PKNAME=\"\"\n"
		"NAME=\"/dev/nvme0n1p2\" SIZE=\"5\" TRAN=\"\" TYPE=\"part\" MODEL=\"\" MOUNTPOINT=\"/\" PKNAME=\"/dev/nvme0n1\"\n"
		"NAME=\"/dev/sda\" SIZE=\"15376318464\" TRAN=\"usb\" TYPE=\"disk\" MODEL=\"Cruzer\\x20Blade  \" MOUNTPOINT=\"\" PKNAME=\"\"\n"
		"NAME=\"/dev/sda1\" SIZE=\"15376318000\" TRAN=\"\" TYPE=\"part\" MODEL=\"\" MOUNTPOINT=\"/run/media/u/STICK\" PKNAME=\"/dev/sda\"\n"
		// A USB disk the system runs from (root inside LUKS) is never offered.
		"NAME=\"/dev/sdb\" SIZE=\"1000\" TRAN=\"usb\" TYPE=\"disk\" MODEL=\"\" MOUNTPOINT=\"\" PKNAME=\"\"\n"
		"NAME=\"/dev/sdb1\" SIZE=\"900\" TRAN=\"\" TYPE=\"part\" MODEL=\"\" MOUNTPOINT=\"\" PKNAME=\"/dev/sdb\"\n"
		"NAME=\"/dev/mapper/root\" SIZE=\"900\" TRAN=\"\" TYPE=\"crypt\" MODEL=\"\" MOUNTPOINT=\"/\" PKNAME=\"/dev/sdb1\"\n"
		"NAME=\"/dev/sdc\" SIZE=\"0\" TRAN=\"usb\" TYPE=\"disk\" MODEL=\"Card reader\" MOUNTPOINT=\"\" PKNAME=\"\"\n";
	auto d = ParseLsblk(lsblk);
	CHECK(d.size() == 1);
	if (d.size() == 1) {
		CHECK(d[0].device == "/dev/sda");
		CHECK(d[0].model == "Cruzer Blade");
		CHECK(Describe(d[0]) == "Cruzer Blade (15.4 GB)");
	}

	CHECK(ParseDdBytes("123731968 bytes (124 MB, 118 MiB) copied, 2 s, 61.8 MB/s") == 123731968);
	CHECK(ParseDdBytes("690814976 bytes (691 MB, 659 MiB) copied, 30.1 s, 23 MB/s") == 690814976);
	CHECK(ParseDdBytes("164+1 records in") == -1);
	CHECK(ParseDdBytes("dd: failed to open '/dev/sdz': Permission denied") == -1);

	const char* hash = "B3F1c0ffee00000000000000000000000000000000000000000000000000abcd";
	CHECK(ParseSha256(std::string(hash) + "  RenkuOS.iso\n")
		== "b3f1c0ffee00000000000000000000000000000000000000000000000000abcd");
	CHECK(ParseSha256("Not Found") == "");

	std::string out;
	CHECK(Run({"sh", "-c", "echo hi; echo err >&2; exit 3"}, &out) == 3);
	CHECK(out == "hi\nerr\n");
	CHECK(Run({"/nonexistent/program"}, &out) == 127);

	if (failures == 0)
		printf("core tests passed\n");
	return failures ? 1 : 0;
}
