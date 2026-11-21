// The animated construction site: road signs for the steps, a tower crane
// hoisting floors onto a building as the image downloads and is written,
// and a message board with a hazard-striped progress bar.
#pragma once

#include <String.h>
#include <View.h>

class BBitmap;

enum Phase { kIdle, kDownload, kVerify, kWrite, kDone, kFailed };

class SiteView : public BView {
public:
						SiteView();
	virtual				~SiteView();

	// step: index of the current step sign (0 build .. 4 restart);
	// progress: 0..1 of the current phase.
			void		SetState(int step, Phase phase, float progress,
							const char* status);

	virtual	void		Draw(BRect updateRect);
	virtual	void		Pulse();
	virtual	void		FrameResized(float width, float height);

private:
			void		_Render(BView* v, float w, float h);
			void		_Signs(BView* v, float w, float y, float size);
			void		_Crane(BView* v, float towerX, float topY,
							float groundY, float jibEnd, float hookX,
							float hookY);
			void		_Building(BView* v, float x0, float x1, float groundY,
							float floorH, int frames, int floors);
			void		_Board(BView* v, BRect r);

			BBitmap*	fBuffer;
			float		fTime;
			float		fTrolley;	// eased trolley position, -1 = unset
			int			fStep;
			Phase		fPhase;
			float		fProgress;
			BString		fStatus;
};
