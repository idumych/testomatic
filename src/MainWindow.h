#pragma once

#include <atomic>
#include <memory>
#include <vector>

#include <String.h>
#include <Window.h>

#include "Core.h"

class BButton;
class BFilePanel;
class BMenuField;
class BMessageRunner;
class SiteView;

class MainWindow : public BWindow {
public:
						MainWindow();
	virtual				~MainWindow();

	virtual	bool		QuitRequested();
	virtual	void		MessageReceived(BMessage* message);

private:
	enum State { kStateReady, kStateDownloading, kStateVerifying, kStateWriting,
		kStateDone, kStateFailed };

			void		_FetchBuilds();
			void		_PollDrives();
			void		_BuildMenu();
			void		_DriveMenu();
			void		_OpenImage(const entry_ref& ref);
			const Build* _Build() const;
			const Drive* _Drive() const;
			void		_Start();
			void		_Begin();
			void		_Restart();
			void		_Update();

			std::vector<Build> fBuilds;
			std::vector<Drive> fDrives;
			Build		fLocal;			// fLocal.path empty: none opened
			int			fBuildIndex;	// -1 none, -2 the local image
			BString		fDriveDevice;
			bool		fFetching;
			bool		fFetchFailed;
			bool		fPolling;

			State		fState;
			float		fProgress;
			bool		fFailedWriting;
			BString		fStatus;
			std::shared_ptr<std::atomic<bool>> fCancel;

			SiteView*	fSite;
			BMenuField*	fBuildField;
			BMenuField*	fDriveField;
			BButton*	fButton;
			BFilePanel*	fPanel;
			BMessageRunner* fPoller;
};
