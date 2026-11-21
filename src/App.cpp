#include <Alert.h>
#include <Application.h>
#include <Entry.h>

#include "MainWindow.h"

static const char* kSignature = "application/x-vnd.Testomatic";


class App : public BApplication {
public:
	App()
		:
		BApplication(kSignature),
		fWindow(new MainWindow())
	{
	}

	virtual void ReadyToRun()
	{
		fWindow->Show();
	}

	// An image dropped on the app's icon, or opened with it
	virtual void RefsReceived(BMessage* message)
	{
		fWindow->PostMessage(message);
	}

	// testomatic <image>: start with that image chosen
	virtual void ArgvReceived(int32 argc, char** argv)
	{
		BMessage refs(B_REFS_RECEIVED);
		for (int32 i = 1; i < argc; i++) {
			entry_ref ref;
			if (get_ref_for_path(argv[i], &ref) == B_OK)
				refs.AddRef("refs", &ref);
		}
		if (refs.HasRef("refs"))
			fWindow->PostMessage(&refs);
	}

	virtual void AboutRequested()
	{
		(new BAlert("About Testomatic", "Testomatic 0.1.0\n\nDownloads a "
			"RenkuOS build, or takes an image you already have, and writes it "
			"to a USB drive so you can try RenkuOS on your computer.\n\n"
			"Copyright 2026 the Testomatic authors. MIT licence.", "OK"))->Go();
	}

private:
	MainWindow* fWindow;
};


int main()
{
	App app;
	app.Run();
	return 0;
}
