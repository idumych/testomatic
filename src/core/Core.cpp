#include "Core.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>

#include <fcntl.h>
#include <cerrno>
#include <sys/wait.h>
#include <unistd.h>


// Reads the JSON string value that follows a key ending at `pos`.
static bool JsonString(const std::string& s, size_t pos, std::string* out)
{
	while (pos < s.size() && (isspace((unsigned char)s[pos]) || s[pos] == ':'))
		pos++;
	if (pos >= s.size() || s[pos] != '"')
		return false;
	out->clear();
	for (pos++; pos < s.size() && s[pos] != '"'; pos++) {
		if (s[pos] == '\\' && pos + 1 < s.size())
			pos++;
		out->push_back(s[pos]);
	}
	return pos < s.size();
}


std::vector<Build> ParseReleases(const std::string& json)
{
	// Each asset object ends with browser_download_url, and its size comes
	// before that, after the previous asset's URL. The URL ends in the name.
	static const std::string kUrl = "\"browser_download_url\"";
	std::vector<std::pair<std::string, int64_t>> assets;
	size_t prev = 0, pos;
	while ((pos = json.find(kUrl, prev)) != std::string::npos) {
		int64_t size = 0;
		size_t s = json.rfind("\"size\"", pos);
		if (s != std::string::npos && s >= prev) {
			s = json.find(':', s);
			if (s != std::string::npos && s < pos)
				size = strtoll(json.c_str() + s + 1, nullptr, 10);
		}
		std::string url;
		if (JsonString(json, pos + kUrl.size(), &url))
			assets.emplace_back(url, size);
		prev = pos + kUrl.size();
	}

	std::set<std::string> urls;
	for (auto& a : assets)
		urls.insert(a.first);

	std::vector<Build> builds;
	for (auto& a : assets) {
		Build b;
		b.url = a.first;
		b.size = a.second;
		b.name = b.url.substr(b.url.rfind('/') + 1);
		// RenkuOS-<arch>-<yyyymmdd>-build<n>.iso
		const std::string& n = b.name;
		if (n.compare(0, 8, "RenkuOS-") != 0 || n.size() < 12
			|| n.compare(n.size() - 4, 4, ".iso") != 0)
			continue;
		std::string mid = n.substr(8, n.size() - 12);
		size_t d1 = mid.find('-'), d2 = mid.rfind('-');
		if (d1 == std::string::npos || d1 == d2)
			continue;
		b.arch = mid.substr(0, d1);
		b.date = mid.substr(d1 + 1, d2 - d1 - 1);
		std::string build = mid.substr(d2 + 1);
		if (b.date.size() != 8 || build.compare(0, 5, "build") != 0
			|| !std::all_of(b.date.begin(), b.date.end(), ::isdigit))
			continue;
		b.number = build.substr(5);
		if (urls.count(b.url + ".sha256"))
			b.shaUrl = b.url + ".sha256";
		builds.push_back(b);
	}
	std::stable_sort(builds.begin(), builds.end(),
		[](const Build& x, const Build& y) {
			if (x.date != y.date)
				return x.date > y.date;
			if (x.number != y.number)
				return x.number > y.number;
			return x.arch == "x86_64" && y.arch != "x86_64";
		});
	return builds;
}


std::string FormatSize(int64_t bytes)
{
	char buf[32];
	if (bytes >= 1000000000LL)
		snprintf(buf, sizeof(buf), "%.1f GB", bytes / 1e9);
	else
		snprintf(buf, sizeof(buf), "%lld MB", (long long)(bytes / 1000000));
	return buf;
}


std::string Describe(const Build& b)
{
	if (!b.path.empty())
		return b.path.substr(b.path.rfind('/') + 1) + " (" + FormatSize(b.size) + ")";
	static const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
		"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
	int month = atoi(b.date.substr(4, 2).c_str());
	std::string date = std::to_string(atoi(b.date.substr(6, 2).c_str())) + " "
		+ (month >= 1 && month <= 12 ? kMonths[month - 1] : "?") + " "
		+ b.date.substr(0, 4);
	std::string arch = b.arch == "x86_64" ? "64-bit"
		: b.arch == "x86_gcc2h" ? "32-bit" : b.arch;
	return "Build " + std::to_string(atoi(b.number.c_str())) + ", " + date
		+ ", " + arch + " (" + FormatSize(b.size) + ")";
}


std::string Describe(const Drive& d)
{
	return d.model + " (" + FormatSize(d.size) + ")";
}


// KEY="value" pairs of one lsblk -P line, with its \xNN escapes undone.
static std::map<std::string, std::string> Pairs(const std::string& line)
{
	std::map<std::string, std::string> m;
	size_t i = 0;
	while (i < line.size()) {
		size_t eq = line.find("=\"", i);
		if (eq == std::string::npos)
			break;
		std::string key = line.substr(i, eq - i), value;
		size_t j = eq + 2;
		for (; j < line.size() && line[j] != '"'; j++) {
			if (line[j] == '\\' && j + 3 < line.size() && line[j + 1] == 'x') {
				value.push_back((char)strtol(line.substr(j + 2, 2).c_str(),
					nullptr, 16));
				j += 3;
			} else
				value.push_back(line[j]);
		}
		m[key] = value;
		i = j + 1;
		while (i < line.size() && line[i] == ' ')
			i++;
	}
	return m;
}


std::vector<Drive> ParseLsblk(const std::string& output)
{
	std::vector<std::map<std::string, std::string>> rows;
	std::map<std::string, std::string> parent;
	size_t start = 0;
	while (start < output.size()) {
		size_t end = output.find('\n', start);
		if (end == std::string::npos)
			end = output.size();
		auto row = Pairs(output.substr(start, end - start));
		if (!row["NAME"].empty()) {
			parent[row["NAME"]] = row["PKNAME"];
			rows.push_back(row);
		}
		start = end + 1;
	}

	auto disk = [&](std::string name) {
		for (int i = 0; i < 16 && !parent[name].empty(); i++)
			name = parent[name];
		return name;
	};
	static const std::set<std::string> kSystem = {"/", "/boot", "/boot/efi",
		"/efi", "/home", "/usr", "/var", "[SWAP]"};
	std::set<std::string> busy;
	for (auto& r : rows) {
		if (kSystem.count(r["MOUNTPOINT"]))
			busy.insert(disk(r["NAME"]));
	}

	std::vector<Drive> drives;
	for (auto& r : rows) {
		int64_t size = strtoll(r["SIZE"].c_str(), nullptr, 10);
		if (r["TYPE"] != "disk" || r["TRAN"] != "usb" || size <= 0
			|| busy.count(r["NAME"]))
			continue;
		std::string model = r["MODEL"];
		while (!model.empty() && isspace((unsigned char)model.back()))
			model.pop_back();
		drives.push_back({r["NAME"], model.empty() ? "USB drive" : model, size});
	}
	return drives;
}


int64_t ParseDdBytes(const std::string& line)
{
	size_t i = 0;
	while (i < line.size() && isspace((unsigned char)line[i]))
		i++;
	if (i >= line.size() || !isdigit((unsigned char)line[i])
		|| line.find(" bytes", i) == std::string::npos)
		return -1;
	return strtoll(line.c_str() + i, nullptr, 10);
}


std::string ParseSha256(const std::string& text)
{
	size_t i = 0;
	while (i < text.size() && isspace((unsigned char)text[i]))
		i++;
	std::string hex;
	for (; i < text.size() && isxdigit((unsigned char)text[i]); i++)
		hex.push_back(tolower((unsigned char)text[i]));
	return hex.size() == 64 ? hex : "";
}


pid_t Spawn(const std::vector<std::string>& argv, int* fd)
{
	std::vector<char*> args;
	for (auto& a : argv)
		args.push_back(const_cast<char*>(a.c_str()));
	args.push_back(nullptr);

	int p[2];
	if (pipe(p) != 0)
		return -1;
	pid_t pid = fork();
	if (pid < 0) {
		close(p[0]);
		close(p[1]);
		return -1;
	}
	if (pid == 0) {
		setpgid(0, 0);
		dup2(p[1], 1);
		dup2(p[1], 2);
		close(p[0]);
		close(p[1]);
		int null = open("/dev/null", O_RDONLY);
		if (null >= 0)
			dup2(null, 0);
		execvp(args[0], args.data());
		_exit(127);
	}
	close(p[1]);
	*fd = p[0];
	return pid;
}


int Wait(pid_t pid)
{
	int status;
	while (waitpid(pid, &status, 0) < 0) {
		if (errno != EINTR)
			return -1;
	}
	return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}


int Run(const std::vector<std::string>& argv, std::string* output)
{
	int fd;
	pid_t pid = Spawn(argv, &fd);
	if (pid < 0)
		return -1;
	char buf[4096];
	ssize_t n;
	output->clear();
	while ((n = read(fd, buf, sizeof(buf))) > 0 || (n < 0 && errno == EINTR)) {
		if (n > 0)
			output->append(buf, n);
	}
	close(fd);
	return Wait(pid);
}
