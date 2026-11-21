#include "MainWindow.h"

#include <dirent.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <thread>

#include <Alert.h>
#include <Application.h>
#include <Button.h>
#include <Invoker.h>
#include <File.h>
#include <FilePanel.h>
#include <FindDirectory.h>
#include <LayoutBuilder.h>
#include <MenuBar.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <MessageRunner.h>
#include <Path.h>
#include <PopUpMenu.h>

#include "Platform.h"
#include "SiteView.h"

static const char* kReleasesUrl
	= "https://api.github.com/repos/RenkuOS/Source/releases";
static const char* kSettingsName = "Testomatic_settings";

enum {
	kMsgBuildChosen = 'bild',
	kMsgDriveChosen = 'driv',
	kMsgOpenImage = 'open',
	kMsgSearchAgain = 'srch',
	kMsgStart = 'strt',
	kMsgCancel = 'cncl',
	kMsgRestart = 'rbot',
	kMsgEraseAnswer = 'eras',
	kMsgRestartAnswer = 'rsta',
	kMsgPoll = 'poll',
	kMsgBuilds = 'blds',
	kMsgDrives = 'drvs',
	kMsgProgress = 'prog',
	kMsgFailed = 'fail',
	kMsgCancelled = 'cand',
	kMsgDone = 'done'
};


// #pragma mark - The job, on its own thread


static void Post(BMessenger& to, uint32 what, Phase phase, float progress,
	const std::string& text)
{
	BMessage m(what);
	m.AddInt32("phase", phase);
	m.AddFloat("progress", progress);
	m.AddString("status", text.c_str());
	to.SendMessage(&m);
}


static int64_t FileSize(const std::string& path)
{
	struct stat st;
	return stat(path.c_str(), &st) == 0 ? (int64_t)st.st_size : -1;
}


// Checks the file against the release's .sha256; "" when it matches.
static std::string Verify(const std::string& file, const Build& build)
{
	if (build.shaUrl.empty())
		return "";	// nothing to check against; the size matched
	std::string out;
	if (Run({"curl", "-fsSL", build.shaUrl}, &out) != 0
		|| ParseSha256(out).empty())
		return "Could not download the checksum of this build. Check the "
			"network connection and try again.";
	std::string expected = ParseSha256(out);
	if (Run({"sha256sum", file}, &out) != 0 || ParseSha256(out) != expected)
		return "The download is damaged: its checksum does not match. "
			"Start again to download it once more.";
	return "";
}


static void Job(BMessenger to, Build build, Drive drive, std::string cache,
	std::shared_ptr<std::atomic<bool>> cancel)
{
	std::string image = build.path;
	if (image.empty()) {
		image = cache + "/" + build.name;
		std::string part = image + ".part";
		bool have = FileSize(image) == build.size
			&& Verify(image, build).empty();
		if (!have) {
			// Only one image is kept: clear out older builds.
			if (DIR* dir = opendir(cache.c_str())) {
				while (dirent* e = readdir(dir)) {
					std::string name = e->d_name;
					if (name.compare(0, 8, "RenkuOS-") == 0
						&& name != build.name + ".part")
						unlink((cache + "/" + name).c_str());
				}
				closedir(dir);
			}

			if (FileSize(part) != build.size) {
				int fd;
				pid_t pid = Spawn({"curl", "-fL", "-sS", "--retry", "3",
					"-C", "-", "-o", part, build.url}, &fd);
				if (pid < 0) {
					Post(to, kMsgFailed, kFailed, 0, "Could not start curl.");
					return;
				}
				int status = 0;
				while (waitpid(pid, &status, WNOHANG) == 0) {
					if (*cancel) {
						kill(-pid, SIGTERM);
						waitpid(pid, &status, 0);
						close(fd);
						unlink(part.c_str());
						to.SendMessage(kMsgCancelled);
						return;
					}
					int64_t got = FileSize(part);
					if (got < 0)
						got = 0;
					Post(to, kMsgProgress, kDownload,
						(float)got / build.size, "Hoisting steel: "
							+ FormatSize(got) + " of " + FormatSize(build.size)
							+ " downloaded");
					usleep(250000);
				}
				char err[512];
				ssize_t n = read(fd, err, sizeof(err) - 1);
				err[n > 0 ? n : 0] = 0;
				close(fd);
				if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
					std::string why = err;
					while (!why.empty() && isspace((unsigned char)why.back()))
						why.pop_back();
					Post(to, kMsgFailed, kFailed, 0, "The download failed. "
						"Check the network connection and try again. ("
						+ why + ")");
					return;
				}
			}

			Post(to, kMsgProgress, kVerify, 1,
				"Inspecting the steel: checking the download…");
			std::string problem = Verify(part, build);
			if (!problem.empty()) {
				unlink(part.c_str());
				Post(to, kMsgFailed, kFailed, 0, problem);
				return;
			}
			rename(part.c_str(), image.c_str());
		}
		if (*cancel) {
			to.SendMessage(kMsgCancelled);
			return;
		}
	}

	std::string error;
	if (!PrepareDrive(drive, &error)) {
		Post(to, kMsgFailed, kFailed, 0, error);
		return;
	}
	int64_t total = FileSize(image);
	Post(to, kMsgProgress, kWrite, 0, "Pouring concrete: starting…");
	int fd;
	pid_t pid = Spawn(WriteCommand(image, drive), &fd);
	if (pid < 0) {
		Post(to, kMsgFailed, kFailed, 0, "Could not start writing.");
		return;
	}
	std::string line, last;
	char c;
	ssize_t n;
	while ((n = read(fd, &c, 1)) > 0 || (n < 0 && errno == EINTR)) {
		if (n <= 0)
			continue;
		if (c != '\r' && c != '\n') {
			line.push_back(c);
			continue;
		}
		int64_t bytes = ParseDdBytes(line);
		if (bytes >= 0 && total > 0) {
			Post(to, kMsgProgress, kWrite, (float)bytes / total,
				"Pouring concrete: " + FormatSize(bytes) + " of "
					+ FormatSize(total) + " written to " + drive.model);
		} else if (!line.empty() && line.find("records") == std::string::npos)
			last = line;
		line.clear();
	}
	close(fd);
	int code = Wait(pid);
	if (code == 0) {
		to.SendMessage(kMsgDone);
		return;
	}
#ifdef APP_COSMOE
	if (code == 126) {
		Post(to, kMsgFailed, kFailed, 0, "Nothing was written: the password "
			"prompt was cancelled.");
		return;
	}
	if (code == 127) {
		Post(to, kMsgFailed, kFailed, 0, "Could not get permission to write "
			"to the drive. Make sure pkexec (polkit) is installed.");
		return;
	}
#endif
	Post(to, kMsgFailed, kFailed, 0, "Writing to the drive failed. Try "
		"another USB port or drive. (" + last + ")");
}


// #pragma mark - MainWindow


static BPath SettingsPath()
{
	BPath path;
	find_directory(B_USER_SETTINGS_DIRECTORY, &path, true);
	path.Append(kSettingsName);
	return path;
}


MainWindow::MainWindow()
	:
	BWindow(BRect(100, 100, 700, 560), "Testomatic", B_TITLED_WINDOW,
		B_AUTO_UPDATE_SIZE_LIMITS | B_QUIT_ON_WINDOW_CLOSE),
	fBuildIndex(-1),
	fFetching(false),
	fFetchFailed(false),
	fPolling(false),
	fState(kStateReady),
	fProgress(0),
	fFailedWriting(false),
	fCancel(std::make_shared<std::atomic<bool>>(false)),
	fPanel(NULL)
{
	BMenuBar* bar = new BMenuBar("menu");
	BMenu* file = new BMenu("File");
	file->AddItem(new BMenuItem("Open image" B_UTF8_ELLIPSIS,
		new BMessage(kMsgOpenImage), 'O'));
	file->AddSeparatorItem();
	BMenuItem* about = new BMenuItem("About Testomatic" B_UTF8_ELLIPSIS,
		new BMessage(B_ABOUT_REQUESTED));
	about->SetTarget(be_app);
	file->AddItem(about);
	file->AddSeparatorItem();
	file->AddItem(new BMenuItem("Quit", new BMessage(B_QUIT_REQUESTED), 'Q'));
	bar->AddItem(file);

	fSite = new SiteView();
	fBuildField = new BMenuField("build", "Build:", new BPopUpMenu(""));
	fDriveField = new BMenuField("drive", "USB drive:", new BPopUpMenu(""));
	fButton = new BButton("go", "Start", new BMessage(kMsgStart));

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(bar)
		.Add(fSite)
		.AddGroup(B_VERTICAL)
			.SetInsets(B_USE_WINDOW_SPACING)
			.AddGrid()
				.AddMenuField(fBuildField, 0, 0)
				.AddMenuField(fDriveField, 0, 1)
			.End()
			.AddGroup(B_HORIZONTAL)
				.AddGlue()
				.Add(fButton)
			.End()
		.End();
	fButton->MakeDefault(true);

	BFile settingsFile(SettingsPath().Path(), B_READ_ONLY);
	BMessage settings;
	BRect frame;
	if (settings.Unflatten(&settingsFile) == B_OK
		&& settings.FindRect("frame", &frame) == B_OK) {
		MoveTo(frame.LeftTop());
		ResizeTo(frame.Width(), frame.Height());
		MoveOnScreen();
	} else
		CenterOnScreen();

	SetPulseRate(40000);
	_BuildMenu();
	_DriveMenu();
	_Update();
	_FetchBuilds();
	_PollDrives();
	BMessage poll(kMsgPoll);
	fPoller = new BMessageRunner(BMessenger(this), &poll, 2000000);
}


MainWindow::~MainWindow()
{
	delete fPoller;
	delete fPanel;
}


bool
MainWindow::QuitRequested()
{
	if (fState == kStateWriting) {
		BAlert* alert = new BAlert("Quit", "The USB drive is still being "
			"written. Quitting now leaves it unusable.", "Quit anyway",
			"Keep writing", NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
		alert->SetShortcut(1, B_ESCAPE);
		if (alert->Go() != 0)
			return false;
	}
	*fCancel = true;	// a download in progress stops and cleans up

	BMessage settings;
	settings.AddRect("frame", Frame());
	BFile file(SettingsPath().Path(), B_WRITE_ONLY | B_CREATE_FILE
		| B_ERASE_FILE);
	settings.Flatten(&file);
	return true;
}


void
MainWindow::_FetchBuilds()
{
	if (fFetching)
		return;
	fFetching = true;
	fFetchFailed = false;
	_BuildMenu();
	BMessenger to(this);
	std::thread([to]() mutable {
		std::string json;
		BMessage m(kMsgBuilds);
		if (::Run({"curl", "-fsSL", "--max-time", "30", kReleasesUrl}, &json) == 0)
			m.AddPointer("builds", new std::vector<Build>(ParseReleases(json)));
		if (to.SendMessage(&m) != B_OK) {
			void* p;
			if (m.FindPointer("builds", &p) == B_OK)
				delete (std::vector<Build>*)p;
		}
	}).detach();
}


void
MainWindow::_PollDrives()
{
	if (fPolling)
		return;
	fPolling = true;
	BMessenger to(this);
	std::thread([to]() mutable {
		BMessage m(kMsgDrives);
		auto* drives = new std::vector<Drive>(ListDrives());
		// For tests: a plain file that stands in for a USB drive
		if (const char* fake = getenv("TESTOMATIC_TEST_DRIVE"))
			drives->push_back({fake, "Test drive", 4000000000LL});
		m.AddPointer("drives", drives);
		if (to.SendMessage(&m) != B_OK)
			delete drives;
	}).detach();
}


const Build*
MainWindow::_Build() const
{
	if (fBuildIndex == -2)
		return &fLocal;
	if (fBuildIndex >= 0 && fBuildIndex < (int)fBuilds.size())
		return &fBuilds[fBuildIndex];
	return NULL;
}


const Drive*
MainWindow::_Drive() const
{
	for (auto& d : fDrives) {
		if (d.device == fDriveDevice.String())
			return &d;
	}
	return NULL;
}


void
MainWindow::_BuildMenu()
{
	BMenu* menu = fBuildField->Menu();
	while (BMenuItem* item = menu->RemoveItem((int32)0))
		delete item;

	for (size_t i = 0; i < fBuilds.size(); i++) {
		BMessage* m = new BMessage(kMsgBuildChosen);
		m->AddInt32("index", i);
		BString label(Describe(fBuilds[i]).c_str());
		if (i == 0)
			label << " (latest)";
		BMenuItem* item = new BMenuItem(label, m);
		item->SetMarked((int)i == fBuildIndex);
		menu->AddItem(item);
	}
	if (!fLocal.path.empty()) {
		BMessage* m = new BMessage(kMsgBuildChosen);
		m->AddInt32("index", -2);
		BMenuItem* item = new BMenuItem(Describe(fLocal).c_str(), m);
		item->SetMarked(fBuildIndex == -2);
		menu->AddItem(item);
	}
	if (fBuilds.empty()) {
		BMenuItem* item = new BMenuItem(fFetching ? "Looking for builds"
			B_UTF8_ELLIPSIS : "No builds found", NULL);
		item->SetEnabled(false);
		menu->AddItem(item, 0);
		if (fFetchFailed)
			menu->AddItem(new BMenuItem("Search again",
				new BMessage(kMsgSearchAgain)));
	}
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("Image file" B_UTF8_ELLIPSIS,
		new BMessage(kMsgOpenImage)));
	menu->SetTargetForItems(this);

	if (_Build() == NULL)
		fBuildField->MenuItem()->SetLabel(fBuilds.empty() && fFetching
			? "Looking for builds" B_UTF8_ELLIPSIS : "Choose a build");
}


void
MainWindow::_DriveMenu()
{
	BMenu* menu = fDriveField->Menu();
	while (BMenuItem* item = menu->RemoveItem((int32)0))
		delete item;
	for (auto& d : fDrives) {
		BMessage* m = new BMessage(kMsgDriveChosen);
		m->AddString("device", d.device.c_str());
		BMenuItem* item = new BMenuItem(Describe(d).c_str(), m);
		item->SetMarked(d.device == fDriveDevice.String());
		menu->AddItem(item);
	}
	if (fDrives.empty()) {
		BMenuItem* item = new BMenuItem("No USB drives found", NULL);
		item->SetEnabled(false);
		menu->AddItem(item);
	}
	menu->SetTargetForItems(this);
	if (_Drive() == NULL)
		fDriveField->MenuItem()->SetLabel(fDrives.empty()
			? "Plug in a USB drive" : "Choose a drive");
}


void
MainWindow::_OpenImage(const entry_ref& ref)
{
	BPath path(&ref);
	struct stat st;
	if (path.InitCheck() != B_OK || stat(path.Path(), &st) != 0
		|| !S_ISREG(st.st_mode))
		return;
	fLocal = Build();
	fLocal.path = path.Path();
	fLocal.name = path.Leaf();
	fLocal.size = st.st_size;
	fBuildIndex = -2;
	if (fState == kStateDone || fState == kStateFailed)
		fState = kStateReady;
	_BuildMenu();
	_Update();
}


void
MainWindow::_Start()
{
	const Build* build = _Build();
	const Drive* drive = _Drive();
	if (build == NULL || drive == NULL)
		return;

	BString text;
	text.SetToFormat("Everything on \"%s\" will be erased and replaced with "
		"RenkuOS. This cannot be undone.", Describe(*drive).c_str());
	BAlert* alert = new BAlert("Erase", text, "Erase and write", "Cancel",
		NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
	alert->SetShortcut(1, B_ESCAPE);
	BMessage* answer = new BMessage(kMsgEraseAnswer);
	answer->AddString("device", drive->device.c_str());
	alert->Go(new BInvoker(answer, this));
}


void
MainWindow::_Begin()
{
	const Build* build = _Build();
	const Drive* drive = _Drive();
	if (build == NULL || drive == NULL)
		return;

	BPath cache;
	find_directory(B_USER_CACHE_DIRECTORY, &cache, true);
	cache.Append("Testomatic");
	create_directory(cache.Path(), 0755);

	fCancel = std::make_shared<std::atomic<bool>>(false);
	fState = build->path.empty() ? kStateDownloading : kStateWriting;
	fProgress = 0;
	fStatus = build->path.empty() ? "Hoisting steel: connecting…"
		: "Pouring concrete: starting…";
	std::thread(Job, BMessenger(this), *build, *drive,
		std::string(cache.Path()), fCancel).detach();
	_Update();
}


void
MainWindow::_Restart()
{
	BAlert* alert = new BAlert("Restart", "Restart the computer now? Programs "
		"that are still open will be closed.", "Cancel", "Restart", NULL,
		B_WIDTH_AS_USUAL, B_IDEA_ALERT);
	alert->SetShortcut(0, B_ESCAPE);
	alert->Go(new BInvoker(new BMessage(kMsgRestartAnswer), this));
}


void
MainWindow::_Update()
{
	const Build* build = _Build();
	const Drive* drive = _Drive();
	bool busy = fState == kStateDownloading || fState == kStateVerifying
		|| fState == kStateWriting;
	bool fits = build && drive && drive->size >= build->size;

	int step = build == NULL ? 0 : drive == NULL ? 1 : 2;
	Phase phase = kIdle;
	switch (fState) {
		case kStateReady:
			if (fFetching && build == NULL)
				fStatus = "Surveying the site: looking for builds…";
			else if (build == NULL)
				fStatus = fFetchFailed ? "Could not reach GitHub. Open an "
					"image file, or check the network and search again."
					: "Site ready. Choose a build.";
			else if (drive == NULL)
				fStatus = fDrives.empty() ? "Plug in a USB drive to build on."
					: "Choose the USB drive to build on.";
			else if (!fits)
				fStatus = "That drive is too small for this build.";
			else
				fStatus = "Site ready. Press Start to break ground.";
			break;
		case kStateDownloading:
			phase = kDownload;
			break;
		case kStateVerifying:
			phase = kVerify;
			break;
		case kStateWriting:
			phase = kWrite;
			step = 3;
			break;
		case kStateDone:
			phase = kDone;
			step = 4;
			fStatus = "Topped out! Leave the drive in, restart, and choose it "
				"in the boot menu.";
			break;
		case kStateFailed:
			phase = kFailed;
			if (step == 2 && fFailedWriting)
				step = 3;
			break;
	}
	fSite->SetState(step, phase, fProgress, fStatus);

	fBuildField->SetEnabled(!busy);
	fDriveField->SetEnabled(!busy);
	if (fState == kStateDone) {
		fButton->SetLabel("Restart system");
		fButton->SetMessage(new BMessage(kMsgRestart));
		fButton->SetEnabled(true);
	} else if (busy) {
		fButton->SetLabel("Cancel");
		fButton->SetMessage(new BMessage(kMsgCancel));
		fButton->SetEnabled(fState == kStateDownloading);
	} else {
		fButton->SetLabel("Start");
		fButton->SetMessage(new BMessage(kMsgStart));
		fButton->SetEnabled(fits);
	}
}


void
MainWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgBuildChosen:
			fBuildIndex = message->GetInt32("index", -1);
			if (fState == kStateDone || fState == kStateFailed)
				fState = kStateReady;
			_Update();
			break;

		case kMsgDriveChosen:
			fDriveDevice = message->GetString("device", "");
			if (fState == kStateDone || fState == kStateFailed)
				fState = kStateReady;
			_Update();
			break;

		case kMsgOpenImage:
			if (fPanel == NULL) {
				fPanel = new BFilePanel(B_OPEN_PANEL, new BMessenger(this),
					NULL, B_FILE_NODE, false);
				fPanel->Window()->SetTitle("Open image");
			}
			fPanel->Show();
			break;

		case B_REFS_RECEIVED:
		case B_SIMPLE_DATA:
		{
			entry_ref ref;
			if (fState != kStateDownloading && fState != kStateVerifying
				&& fState != kStateWriting
				&& message->FindRef("refs", &ref) == B_OK)
				_OpenImage(ref);
			break;
		}

		case kMsgSearchAgain:
			_FetchBuilds();
			_Update();
			break;

		case kMsgBuilds:
		{
			fFetching = false;
			void* p;
			if (message->FindPointer("builds", &p) == B_OK) {
				std::vector<Build>* builds = (std::vector<Build>*)p;
				fBuilds = *builds;
				delete builds;
				if (fBuildIndex == -1 && !fBuilds.empty())
					fBuildIndex = 0;
			}
			fFetchFailed = fBuilds.empty();
			_BuildMenu();
			_Update();
			break;
		}

		case kMsgPoll:
			if (fState != kStateDownloading && fState != kStateVerifying
				&& fState != kStateWriting)
				_PollDrives();
			break;

		case kMsgDrives:
		{
			fPolling = false;
			void* p;
			if (message->FindPointer("drives", &p) != B_OK)
				break;
			std::vector<Drive>* drives = (std::vector<Drive>*)p;
			bool same = drives->size() == fDrives.size();
			for (size_t i = 0; same && i < drives->size(); i++) {
				same = (*drives)[i].device == fDrives[i].device
					&& (*drives)[i].size == fDrives[i].size;
			}
			if (!same) {
				fDrives = *drives;
				if (_Drive() == NULL)
					fDriveDevice = fDrives.size() == 1
						? fDrives[0].device.c_str() : "";
				_DriveMenu();
				_Update();
			}
			delete drives;
			break;
		}

		case kMsgStart:
			_Start();
			break;

		case kMsgCancel:
			*fCancel = true;
			fButton->SetEnabled(false);
			break;

		case kMsgRestart:
			_Restart();
			break;

		case kMsgEraseAnswer:
			// Only the drive the alert named, and only if nothing started since
			if (message->GetInt32("which", 1) == 0 && fState != kStateDownloading
				&& fState != kStateVerifying && fState != kStateWriting
				&& fDriveDevice == message->GetString("device", ""))
				_Begin();
			break;

		case kMsgRestartAnswer:
		{
			std::string out;
			if (message->GetInt32("which", 0) == 1
				&& ::Run(RestartCommand(), &out) != 0) {
				(new BAlert("Restart", "Could not restart the computer. "
					"Restart it from the system menu instead.", "OK"))
					->Go(NULL);
			}
			break;
		}

		case kMsgProgress:
		{
			Phase phase = (Phase)message->GetInt32("phase", kDownload);
			fState = phase == kVerify ? kStateVerifying
				: phase == kWrite ? kStateWriting : kStateDownloading;
			fProgress = message->GetFloat("progress", 0);
			fStatus = message->GetString("status", "");
			_Update();
			break;
		}

		case kMsgFailed:
			fFailedWriting = fState == kStateWriting;
			fState = kStateFailed;
			fStatus = message->GetString("status", "");
			_Update();
			break;

		case kMsgCancelled:
			fState = kStateReady;
			fProgress = 0;
			_Update();
			break;

		case kMsgDone:
			fState = kStateDone;
			fProgress = 1;
			_Update();
			break;

		default:
			BWindow::MessageReceived(message);
	}
}
