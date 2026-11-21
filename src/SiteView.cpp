#include "SiteView.h"

#include <math.h>

#include <Bitmap.h>
#include <Region.h>


static const rgb_color kYellow = {255, 196, 0, 255};
static const rgb_color kBlack = {28, 28, 30, 255};
static const rgb_color kOrange = {255, 122, 0, 255};
static const rgb_color kAmber = {255, 176, 0, 255};
static const rgb_color kSteel = {196, 64, 40, 255};
static const rgb_color kConcrete = {168, 166, 160, 255};
static const int kFloors = 8;
static const char* kStepNames[] = {"Choose build", "Choose drive", "Download",
	"Write", "Restart"};


static rgb_color Mix(rgb_color a, rgb_color b, float t)
{
	rgb_color c;
	c.red = (uint8)(a.red + (b.red - a.red) * t);
	c.green = (uint8)(a.green + (b.green - a.green) * t);
	c.blue = (uint8)(a.blue + (b.blue - a.blue) * t);
	c.alpha = 255;
	return c;
}


static rgb_color Alpha(rgb_color c, uint8 alpha)
{
	c.alpha = alpha;
	return c;
}


// Diagonal yellow and black stripes filling r, moving with `offset`.
static void Hazard(BView* v, BRect r, float offset, rgb_color light,
	rgb_color dark)
{
	BRegion clip(r);
	v->ConstrainClippingRegion(&clip);
	v->SetHighColor(light);
	v->FillRect(r);
	v->SetHighColor(dark);
	float step = r.Height() + 1;
	for (float x = r.left - 2 * step + fmodf(offset, 2 * step);
			x < r.right + step; x += 2 * step) {
		BPoint p[4] = {BPoint(x, r.bottom + 1), BPoint(x + step, r.top),
			BPoint(x + 2 * step, r.top), BPoint(x + step, r.bottom + 1)};
		v->FillPolygon(p, 4);
	}
	v->ConstrainClippingRegion(NULL);
}


// A lattice girder between two horizontal or vertical rails.
static void Lattice(BView* v, BPoint a, BPoint b, float depth, bool vertical)
{
	BPoint off = vertical ? BPoint(depth, 0) : BPoint(0, depth);
	v->StrokeLine(a, b);
	v->StrokeLine(a + off, b + off);
	float len = vertical ? b.y - a.y : b.x - a.x;
	int n = (int)(fabsf(len) / depth);
	for (int i = 0; i < n; i++) {
		float t0 = (float)i / n, t1 = (float)(i + 1) / n;
		BPoint p0 = a + BPoint((b.x - a.x) * t0, (b.y - a.y) * t0);
		BPoint p1 = a + BPoint((b.x - a.x) * t1, (b.y - a.y) * t1);
		v->StrokeLine(i % 2 ? p0 : p0 + off, i % 2 ? p1 + off : p1);
	}
}


SiteView::SiteView()
	:
	BView("site", B_WILL_DRAW | B_FRAME_EVENTS | B_PULSE_NEEDED),
	fBuffer(NULL),
	fTime(0),
	fTrolley(-1),
	fStep(0),
	fPhase(kIdle),
	fProgress(0)
{
	SetViewColor(B_TRANSPARENT_COLOR);
	float font = be_plain_font->Size();
	SetExplicitMinSize(BSize(font * 40, font * 22));
	SetExplicitPreferredSize(BSize(font * 48, font * 27));
}


SiteView::~SiteView()
{
	delete fBuffer;
}


void
SiteView::SetState(int step, Phase phase, float progress, const char* status)
{
	fStep = step;
	fPhase = phase;
	fProgress = progress < 0 ? 0 : progress > 1 ? 1 : progress;
	fStatus = status;
}


void
SiteView::Pulse()
{
	fTime += 0.04f;
	Invalidate();
}


void
SiteView::FrameResized(float, float)
{
	delete fBuffer;
	fBuffer = NULL;
	Invalidate();
}


void
SiteView::Draw(BRect)
{
	// Drawn off screen first, so the animation doesn't flicker.
	BRect b = Bounds();
	if (fBuffer == NULL) {
		fBuffer = new BBitmap(b.OffsetToCopy(B_ORIGIN), B_RGB32, true);
		fBuffer->AddChild(new BView(fBuffer->Bounds(), "buffer",
			B_FOLLOW_NONE, 0));
	}
	if (fBuffer->Lock()) {
		BView* v = fBuffer->ChildAt(0);
		_Render(v, b.Width() + 1, b.Height() + 1);
		v->Sync();
		fBuffer->Unlock();
	}
	DrawBitmap(fBuffer, b.LeftTop());
}


void
SiteView::_Render(BView* v, float w, float h)
{
	v->SetDrawingMode(B_OP_ALPHA);
	v->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
	v->SetPenSize(1);

	// Dusk sky with a few twinkling stars
	float groundY = floorf(h * 0.78f);
	const rgb_color top = {34, 48, 88, 255}, horizon = {250, 168, 92, 255};
	for (float y = 0; y < groundY; y += 4) {
		v->SetHighColor(Mix(top, horizon, y / groundY));
		v->FillRect(BRect(0, y, w, y + 3));
	}
	for (int i = 0; i < 14; i++) {
		float x = fmodf(i * 211.7f, w);
		float y = 24 + fmodf(i * 53.1f, groundY * 0.35f);
		uint8 a = (uint8)(110 + 100 * sinf(fTime * 1.7f + i));
		v->SetHighColor(Alpha(make_color(255, 244, 214), a));
		v->FillRect(BRect(x, y, x + 1, y + 1));
	}

	// City skyline in the distance
	v->SetHighColor(make_color(118, 98, 120));
	static const float kSkyline[] = {0.30f, 0.18f, 0.42f, 0.26f, 0.36f, 0.14f,
		0.48f, 0.22f, 0.32f, 0.20f, 0.40f, 0.28f};
	float bw = w / 12;
	for (int i = 0; i < 12; i++) {
		float bh = kSkyline[i] * groundY * 0.32f;
		v->FillRect(BRect(i * bw, groundY - bh, i * bw + bw * 0.86f, groundY));
	}

	// Ground
	v->SetHighColor(make_color(104, 78, 54));
	v->FillRect(BRect(0, groundY, w, h));
	v->SetHighColor(make_color(86, 63, 44));
	for (int i = 0; i < 30; i++) {
		float x = fmodf(i * 137.9f, w), y = groundY + 4 + fmodf(i * 29.3f, h - groundY - 8);
		v->StrokeLine(BPoint(x, y), BPoint(x + 6, y));
	}

	// Caution tape along the top
	Hazard(v, BRect(0, 0, w, 11), fTime * 24, kYellow, kBlack);

	font_height fh;
	be_plain_font->GetHeight(&fh);
	float signSize = floorf(fmaxf(26, fminf(h * 0.10f, 42)));
	_Signs(v, w, 20, signSize);

	// Crane and building
	float topY = 20 + signSize + fh.ascent + fh.descent + 34;
	float towerX = floorf(w * 0.22f);
	float jibEnd = floorf(w * 0.80f);
	float bx0 = floorf(w * 0.52f), bx1 = floorf(w * 0.80f);
	float floorH = floorf((groundY - topY - 46) / kFloors);

	int frames = 0, floors = 0;
	switch (fPhase) {
		case kDownload:
			frames = (int)(fProgress * kFloors);
			break;
		case kVerify:
			frames = kFloors;
			break;
		case kWrite:
			frames = kFloors;
			floors = (int)(fProgress * kFloors);
			break;
		case kDone:
			frames = floors = kFloors;
			break;
		case kFailed:
		case kIdle:
			break;
	}
	bool working = fPhase == kDownload || fPhase == kVerify
		|| fPhase == kWrite;

	float target;
	if (working)
		target = (bx0 + bx1) / 2;
	else {
		target = towerX + 40 + (sinf(fTime * 0.35f) + 1) / 2
			* (jibEnd - towerX - 70);
	}
	fTrolley = fTrolley < 0 ? target : fTrolley + (target - fTrolley) * 0.06f;

	// The load hangs just above the next floor, bobbing as it's lowered.
	int next = fPhase == kWrite ? floors : frames;
	if (next > kFloors - 1)
		next = kFloors - 1;
	float hookY;
	if (working)
		hookY = groundY - (next + 1) * floorH - 14 + sinf(fTime * 2.2f) * 5;
	else if (fPhase == kDone)
		hookY = topY + 30;
	else
		hookY = topY + (groundY - topY) * 0.45f + sinf(fTime * 0.8f) * 8;
	float swing = sinf(fTime * 1.3f) * (working ? 3 : 6);

	_Building(v, bx0, bx1, groundY, floorH, frames, floors);
	_Crane(v, towerX, topY, groundY, jibEnd, fTrolley + swing, hookY);

	// The load on the hook: a steel beam, a concrete bucket, or a crate.
	float lx = fTrolley + swing;
	if (fPhase == kWrite) {
		BPoint p[4] = {BPoint(lx - 10, hookY + 4), BPoint(lx + 10, hookY + 4),
			BPoint(lx + 6, hookY + 20), BPoint(lx - 6, hookY + 20)};
		v->SetHighColor(make_color(120, 118, 114));
		v->FillPolygon(p, 4);
		v->SetHighColor(kBlack);
		v->StrokePolygon(p, 4);
	} else if (fPhase == kDownload || fPhase == kVerify) {
		float half = (bx1 - bx0) / 2 - 6;
		v->SetHighColor(kSteel);
		v->FillRect(BRect(lx - half, hookY + 6, lx + half, hookY + 11));
		v->SetHighColor(kBlack);
		v->StrokeRect(BRect(lx - half, hookY + 6, lx + half, hookY + 11));
		v->StrokeLine(BPoint(lx, hookY + 4), BPoint(lx - half + 4, hookY + 6));
		v->StrokeLine(BPoint(lx, hookY + 4), BPoint(lx + half - 4, hookY + 6));
	} else if (fPhase != kDone) {
		BRect crate(lx - 11, hookY + 4, lx + 11, hookY + 24);
		v->SetHighColor(make_color(186, 132, 70));
		v->FillRect(crate);
		v->SetHighColor(make_color(120, 80, 40));
		v->StrokeRect(crate);
		v->StrokeLine(crate.LeftTop(), crate.RightBottom());
		v->StrokeLine(crate.RightTop(), crate.LeftBottom());
	}

	// Road closed: a barrier in front of the site
	if (fPhase == kFailed) {
		float y = groundY - 26;
		v->SetHighColor(kBlack);
		v->FillRect(BRect(w * 0.47f, y, w * 0.47f + 4, groundY));
		v->FillRect(BRect(w * 0.85f - 4, y, w * 0.85f, groundY));
		Hazard(v, BRect(w * 0.45f, y, w * 0.87f, y + 9), 0,
			make_color(250, 250, 246), make_color(214, 40, 40));
	}

	float boardH = fh.ascent + fh.descent + 30;
	_Board(v, BRect(14, h - boardH - 10, w - 15, h - 11));
}


void
SiteView::_Signs(BView* v, float w, float y, float size)
{
	font_height fh;
	be_plain_font->GetHeight(&fh);
	float slot = w / 5;
	for (int i = 0; i < 5; i++) {
		float cx = floorf(slot * (i + 0.5f)), cy = y + size / 2;
		float r = size / 2;
		bool done = i < fStep;
		bool current = i == fStep;
		if (current && fPhase != kFailed) {
			float glow = 0.5f + 0.5f * sinf(fTime * 4);
			v->SetHighColor(Alpha(kAmber, (uint8)(60 + 80 * glow)));
			v->FillEllipse(BPoint(cx, cy), r + 7, r + 7);
		}
		rgb_color face = fPhase == kFailed && current ? make_color(214, 40, 40)
			: (done || current) ? kOrange : make_color(150, 120, 90);
		BPoint d[4] = {BPoint(cx, cy - r), BPoint(cx + r, cy),
			BPoint(cx, cy + r), BPoint(cx - r, cy)};
		v->SetHighColor(face);
		v->FillPolygon(d, 4);
		v->SetHighColor(kBlack);
		v->SetPenSize(2);
		v->StrokePolygon(d, 4);
		v->SetPenSize(1);

		BFont bold(be_bold_font);
		bold.SetSize(size * 0.42f);
		v->SetFont(&bold);
		if (done) {
			v->SetPenSize(3);
			v->StrokeLine(BPoint(cx - r * 0.35f, cy), BPoint(cx - r * 0.08f, cy + r * 0.3f));
			v->StrokeLine(BPoint(cx - r * 0.08f, cy + r * 0.3f), BPoint(cx + r * 0.38f, cy - r * 0.32f));
			v->SetPenSize(1);
		} else {
			char n[2] = {(char)('1' + i), 0};
			font_height bh;
			bold.GetHeight(&bh);
			v->DrawString(n, BPoint(cx - bold.StringWidth(n) / 2,
				cy + (bh.ascent - bh.descent) / 2));
		}

		BFont plain(be_plain_font);
		plain.SetSize(be_plain_font->Size() * 0.9f);
		v->SetFont(&plain);
		BString label(kStepNames[i]);
		plain.TruncateString(&label, B_TRUNCATE_END, slot - 6);
		float lx = cx - plain.StringWidth(label) / 2;
		float ly = y + size + fh.ascent + 4;
		v->SetHighColor(Alpha(kBlack, 150));
		v->DrawString(label, BPoint(lx + 1, ly + 1));
		v->SetHighColor(done || current ? make_color(255, 244, 214)
			: make_color(196, 186, 170));
		v->DrawString(label, BPoint(lx, ly));
	}
}


void
SiteView::_Crane(BView* v, float towerX, float topY, float groundY,
	float jibEnd, float hookX, float hookY)
{
	const float depth = 14, counter = towerX - 70;
	rgb_color yellow = kYellow, shade = make_color(196, 140, 0);

	// Footing, tower, jib and counter-jib
	v->SetHighColor(kConcrete);
	v->FillRect(BRect(towerX - 14, groundY - 6, towerX + depth + 14, groundY + 2));
	v->SetPenSize(2);
	v->SetHighColor(shade);
	Lattice(v, BPoint(towerX, topY + depth), BPoint(towerX, groundY - 6), depth, true);
	v->SetHighColor(yellow);
	Lattice(v, BPoint(counter, topY), BPoint(jibEnd, topY), 8, false);
	v->SetPenSize(1);

	// Peak with tie lines
	BPoint apex(towerX + depth / 2, topY - 28);
	v->SetHighColor(shade);
	v->StrokeLine(BPoint(towerX, topY), apex);
	v->StrokeLine(BPoint(towerX + depth, topY), apex);
	v->SetHighColor(Alpha(kBlack, 160));
	v->StrokeLine(apex, BPoint(jibEnd - 20, topY));
	v->StrokeLine(apex, BPoint(counter + 6, topY));

	// Counterweight and cab
	v->SetHighColor(make_color(90, 90, 94));
	v->FillRect(BRect(counter + 2, topY + 8, counter + 26, topY + 30));
	v->SetHighColor(kBlack);
	v->StrokeRect(BRect(counter + 2, topY + 8, counter + 26, topY + 30));
	BRect cab(towerX + depth + 1, topY + 9, towerX + depth + 19, topY + 25);
	v->SetHighColor(yellow);
	v->FillRect(cab);
	v->SetHighColor(make_color(150, 200, 230));
	v->FillRect(cab.InsetByCopy(4, 3).OffsetByCopy(1, -1));
	v->SetHighColor(kBlack);
	v->StrokeRect(cab);

	// Warning beacon, blinking
	bool on = fmodf(fTime, 1.2f) < 0.6f;
	if (on) {
		v->SetHighColor(Alpha(make_color(255, 60, 30), 70));
		v->FillEllipse(apex - BPoint(0, 4), 11, 11);
	}
	v->SetHighColor(on ? make_color(255, 70, 40) : make_color(120, 30, 20));
	v->FillEllipse(apex - BPoint(0, 4), 3.5f, 3.5f);

	// Trolley, cable, hook
	float trolleyX = fminf(fmaxf(hookX, towerX + 30), jibEnd - 10);
	v->SetHighColor(kBlack);
	v->FillRect(BRect(trolleyX - 7, topY + 8, trolleyX + 7, topY + 13));
	v->SetHighColor(make_color(40, 40, 44));
	v->StrokeLine(BPoint(trolleyX - 2, topY + 13), BPoint(hookX - 1, hookY));
	v->StrokeLine(BPoint(trolleyX + 2, topY + 13), BPoint(hookX + 1, hookY));
	v->SetHighColor(make_color(210, 60, 30));
	v->FillRect(BRect(hookX - 4, hookY - 4, hookX + 4, hookY + 2));
	v->SetHighColor(kBlack);
	v->SetPenSize(2);
	v->StrokeArc(BPoint(hookX, hookY + 4), 3, 3, 180, 270);
	v->SetPenSize(1);
}


void
SiteView::_Building(BView* v, float x0, float x1, float groundY, float floorH,
	int frames, int floors)
{
	// Foundation slab
	v->SetHighColor(make_color(140, 138, 132));
	v->FillRect(BRect(x0 - 8, groundY - 5, x1 + 8, groundY + 1));

	for (int i = 0; i < frames; i++) {
		float yb = groundY - 5 - i * floorH, yt = yb - floorH;
		if (i < floors) {
			v->SetHighColor(kConcrete);
			v->FillRect(BRect(x0, yt, x1, yb));
			v->SetHighColor(make_color(120, 118, 112));
			v->StrokeRect(BRect(x0, yt, x1, yb));
			// Windows, lit at random-looking moments once done
			int n = (int)((x1 - x0) / 18);
			for (int k = 0; k < n; k++) {
				float wx = x0 + 6 + k * (x1 - x0 - 6) / n;
				bool lit = fPhase == kDone
					|| sinf(i * 7.1f + k * 3.3f + fTime * 0.2f) > 0.6f;
				v->SetHighColor(lit ? make_color(255, 214, 120)
					: make_color(60, 70, 90));
				v->FillRect(BRect(wx, yt + floorH * 0.3f, wx + 8,
					yt + floorH * 0.75f));
			}
		} else {
			// Scaffold: steel frame waiting for concrete
			v->SetHighColor(kSteel);
			v->SetPenSize(2);
			v->StrokeRect(BRect(x0, yt, x1, yb));
			v->SetPenSize(1);
			v->SetHighColor(Alpha(kSteel, 170));
			for (float x = x0; x < x1; x += floorH)
				v->StrokeLine(BPoint(x, yb), BPoint(fminf(x + floorH, x1), yt));
		}
	}

	// A flag on the finished roof
	if (fPhase == kDone) {
		float roof = groundY - 5 - kFloors * floorH, px = x1 - 14;
		v->SetHighColor(kBlack);
		v->StrokeLine(BPoint(px, roof), BPoint(px, roof - 28));
		BPoint flag[6];
		for (int i = 0; i < 3; i++) {
			float x = px + 1 + i * 9;
			float wave = sinf(fTime * 5 - i) * 2;
			flag[i] = BPoint(x, roof - 28 + wave);
			flag[5 - i] = BPoint(x, roof - 16 + wave);
		}
		v->SetHighColor(kOrange);
		v->FillPolygon(flag, 6);
	}
}


void
SiteView::_Board(BView* v, BRect r)
{
	v->SetHighColor(kBlack);
	v->FillRoundRect(r, 6, 6);
	v->SetHighColor(make_color(80, 80, 84));
	v->StrokeRoundRect(r, 6, 6);

	font_height fh;
	BFont bold(be_bold_font);
	bold.GetHeight(&fh);
	v->SetFont(&bold);

	BString percent;
	if (fPhase == kDownload || fPhase == kWrite)
		percent.SetToFormat("%d%%", (int)(fProgress * 100));
	float pw = bold.StringWidth("100%");
	BString status(fStatus);
	bold.TruncateString(&status, B_TRUNCATE_END, r.Width() - pw - 32);
	float ty = r.top + 8 + fh.ascent;
	v->SetHighColor(fPhase == kFailed ? make_color(255, 96, 80) : kAmber);
	v->DrawString(status, BPoint(r.left + 12, ty));
	v->DrawString(percent, BPoint(r.right - 12 - bold.StringWidth(percent), ty));

	BRect bar(r.left + 12, r.bottom - 16, r.right - 12, r.bottom - 8);
	v->SetHighColor(make_color(54, 54, 58));
	v->FillRect(bar);
	float fill;
	switch (fPhase) {
		case kDownload:
		case kWrite:
			fill = fProgress;
			break;
		case kVerify:
		case kDone:
			fill = 1;
			break;
		default:
			fill = 0;
	}
	if (fill > 0) {
		BRect f = bar;
		f.right = bar.left + (bar.Width()) * fill;
		bool moving = fPhase != kDone;
		Hazard(v, f, moving ? fTime * 30 : 0,
			fPhase == kDone ? make_color(80, 200, 90) : kYellow, kBlack);
	}
}
