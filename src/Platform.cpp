#include "Platform.h"

#ifdef APP_COSMOE

// Linux: lsblk finds the drives; root rights come from pkexec, which also
// unmounts the drive's partitions before dd writes it.

std::vector<Drive> ListDrives()
{
	std::string out;
	if (Run({"lsblk", "-P", "-p", "-b", "-o",
			"NAME,SIZE,TRAN,TYPE,MODEL,MOUNTPOINT,PKNAME"}, &out) != 0)
		return {};
	return ParseLsblk(out);
}


bool PrepareDrive(const Drive&, std::string*)
{
	return true;
}


std::vector<std::string> WriteCommand(const std::string& image,
	const Drive& drive)
{
	if (drive.device.compare(0, 5, "/dev/") != 0)	// TESTOMATIC_TEST_DRIVE
		return {"dd", "if=" + image, "of=" + drive.device, "bs=4M",
			"conv=fsync", "status=progress"};
	// Paths go in as arguments, never into the script text.
	return {"pkexec", "/bin/sh", "-c",
		"for p in \"$1\"?*; do umount \"$p\" 2>/dev/null; done; "
		"exec dd if=\"$2\" of=\"$1\" bs=4M conv=fsync status=progress",
		"sh", drive.device, image};
}


std::vector<std::string> RestartCommand()
{
	return {"systemctl", "reboot"};
}

#else	// Haiku, Renku

#include <dirent.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <string.h>
#include <unistd.h>

#include <Directory.h>
#include <Drivers.h>
#include <Entry.h>
#include <Path.h>
#include <Volume.h>
#include <VolumeRoster.h>
#include <fs_info.h>
#include <fs_volume.h>


// "/dev/disk/usb/0/0/raw" -> "/dev/disk/usb/0/0/", which its partitions share.
static std::string Prefix(const std::string& raw)
{
	return raw.substr(0, raw.size() - 3);
}


static bool StartsWith(const char* s, const std::string& prefix)
{
	return strncmp(s, prefix.c_str(), prefix.size()) == 0;
}


std::vector<Drive> ListDrives()
{
	fs_info boot = {};
	BVolume bootVolume;
	BVolumeRoster().GetBootVolume(&bootVolume);
	fs_stat_dev(bootVolume.Device(), &boot);

	std::vector<Drive> drives;
	DIR* bus = opendir("/dev/disk/usb");
	if (bus == NULL)
		return drives;
	while (dirent* d = readdir(bus)) {
		if (d->d_name[0] == '.')
			continue;
		std::string raw = std::string("/dev/disk/usb/") + d->d_name + "/0/raw";
		if (StartsWith(boot.device_name, Prefix(raw)))
			continue;	// the system runs from this stick
		int fd = open(raw.c_str(), O_RDONLY);
		if (fd < 0)
			continue;
		device_geometry g = {};
		char name[B_FILE_NAME_LENGTH] = "";
		bool ok = ioctl(fd, B_GET_GEOMETRY, &g, sizeof(g)) == 0;
		ioctl(fd, B_GET_DEVICE_NAME, name, sizeof(name));
		close(fd);
		int64_t size = (int64_t)g.bytes_per_sector * g.sectors_per_track
			* g.cylinder_count * g.head_count;
		if (!ok || g.read_only || size <= 0)
			continue;
		drives.push_back({raw, name[0] ? name : "USB drive", size});
	}
	closedir(bus);
	return drives;
}


bool PrepareDrive(const Drive& drive, std::string* error)
{
	std::string prefix = Prefix(drive.device);
	BVolumeRoster roster;
	BVolume volume;
	while (roster.GetNextVolume(&volume) == B_OK) {
		fs_info info;
		if (fs_stat_dev(volume.Device(), &info) != 0
			|| !StartsWith(info.device_name, prefix))
			continue;
		BDirectory root;
		BEntry entry;
		BPath path;
		if (volume.GetRootDirectory(&root) != B_OK
			|| root.GetEntry(&entry) != B_OK || entry.GetPath(&path) != B_OK
			|| fs_unmount_volume(path.Path(), 0) != B_OK) {
			*error = std::string("Could not unmount \"") + info.volume_name
				+ "\". Close any windows showing files on the drive and "
				"try again.";
			return false;
		}
	}
	return true;
}


std::vector<std::string> WriteCommand(const std::string& image,
	const Drive& drive)
{
	return {"dd", "if=" + image, "of=" + drive.device, "bs=4M", "conv=fsync",
		"status=progress"};
}


std::vector<std::string> RestartCommand()
{
	return {"shutdown", "-r"};
}

#endif
