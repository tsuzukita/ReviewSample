/*********************************************************
A機能開発サンプル
DeviceA.cpp
*********************************************************/

//*********************************************************

#include "Stdafx.h"
#include "PlayOptsBox.h"
#include "DeviceA.h"
#include "Mapper.h"
#include "MIDI Interface.h"
#include "Direct3DInterface.h"

//*********************************************************

#define NUMQUADS		25
#define VBSIZE			(NUMQUADS*4)
#define ENDSONGTIME		3
#define STARTDELAY		3
#define MINSCRNOTES		5

D3DMATRIX IdentityMat = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};

Game *Game::gGame = NULL;

//*********************************************************
// Called by Windows, must be global, so grab singleton and
// execute the class message pump

LRESULT gMsgProc(HWND , UINT msg, WPARAM wParam, LPARAM lParam)
{
	return Game::Gam()->MsgProc(msg, wParam, lParam);
}

//*********************************************************
// Protected constructor - singleton class

DeviceA::DeviceA()
{
	this->mpNoteBuffer = NULL;
	this->mpRecBuffer = NULL;
	this->mpTimer = 0;
}

//*********************************************************

DeviceA::~DeviceA()
{
}

//*********************************************************
// Get pointer to singleton, create if necessary

DeviceA *DeviceA::Gam()
{
	if (gGame == NULL)
		gGame = new Game();
	return gGame;
}

//*********************************************************
// Safely clean up all the allocated memory and resources

void DeviceA::Destroy()
{
	MIDIInterface *midi = MIDIInterface::MIDI();
	midi->StopIn();
	midi->CloseIn();
	midi->CloseOut();
	SAFE_FREE(this->mpNoteBuffer);
	SAFE_FREE(this->mpRecBuffer);
	SAFE_DELETE(gGame);
}

//*********************************************************
// Set up D3D and the MIDI interface

void DeviceA::Setup(MIDISong *song, CPlayOptsBox *pb)
{
	this->mSongOver = false;
	this->mReplaySong = false;
	this->mSong = song;
	this->mGameInterface = pb->mGameInterface;
	this->mGameStyle = pb->mGameStyle;
	this->mGameDisplay = pb->mGameDisplay;
	this->mGameDifficulty = pb->mGameDifficulty;
	this->mPlayUnmarked = pb->mGamePlayUnmark != 0;
	this->mPlayMIDIVel = pb->mGameMIDIVel != 0;
	this->mStartDelay = STARTDELAY;
	song->SetDifficulty(this->mGameDifficulty);
	song->PlayOpts(pb->mGameSolo != 0);

	Mapper *map = Mapper::Map();
	map->FindRange(this->mLowNote, this->mHighNote, this->mNotesInOrder, this->mNoteCount);
	for (int i=0; i<128; i++)
	{
		this->mReverseNote[i] = 0xFF;
		this->mNoteOn[i] = false;
	}
	UCHAR next = 0xFF;
	for (UCHAR i=0; i<this->mNoteCount; i++)
	{
		next = map->GetUsedInd(next);
		this->mReverseNote[map->GetNote(next)] = i;
	}
	if (this->mGameStyle == 0)
		this->mScreenNotes = this->mNoteCount;
	else if (this->mGameStyle == 1)
		this->mScreenNotes = this->mHighNote-this->mLowNote+1;
	else// if (this->mGameStyle == 2)
		;

	int sn = this->mScreenNotes;
	if (sn < MINSCRNOTES)
		sn = MINSCRNOTES;
	this->mNearZ = (float)sn/2/tan(30*rad);
	this->mFarZ = this->mNearZ*4;

	{
		UCHAR next = 0xFF;
		for (UCHAR i=0; i<this->mNoteCount; i++)
		{
			next = map->GetUsedInd(next);
			this->mSaveInd[i] = next;
			this->mSaveNote[i] = map->GetNote(next);
			if (this->mGameStyle == 0)
				this->mSaveX[i] = -(float)(this->mScreenNotes-1)/2+(float)i;
			else if (this->mGameStyle == 1)
				this->mSaveX[i] = -(float)(this->mScreenNotes-1)/2+(float)(map->GetNote(next)-this->mLowNote);
			else //if (this->mGameStyle == 2)
				;
			this->mSaveColor[i] = map->PianoColor(next);
		}
	}

	SAFE_FREE(this->mpNoteBuffer);
	SAFE_FREE(this->mpRecBuffer);
	song->ExtractNotes(&this->mpNoteBuffer, this->mTotalNotes, this->mReverseNote, this->mEndTime);
	this->mpRecBuffer = (MIDINoteGame *)malloc(sizeof(MIDINoteGame)*this->mTotalNotes*8);
	TRACE(_T("Record buffer:  %X  %X\n"), this->mpRecBuffer, this->mpRecBuffer+(this->mTotalNotes*8));
	this->mNumRecNotes = 0;
	D3DInterface *d3di = D3DInterface::D3DI();
	d3di->AllocVB(VBSIZE);
	if (this->mGameStyle == 0)
	{
		int r = this->mNoteCount;
		if (r < 30)
			r = 30;
		d3di->FontRatio(r);
	}
	else if (this->mGameStyle == 1)
	{
		int r = this->mHighNote-this->mLowNote;
		if (r < 30)
			r = 30;
		d3di->FontRatio(r);
	}
	else
	{
	}

	d3di->SetCallbacks(&::gMsgProc, &Render);
	MIDIInterface *midi = MIDIInterface::MIDI();
	if (!midi->IsDeviceOut())
	{
		midi->InitializeOut(pb->mGameMIDIOut-1);
		song->Play();
		song->DelayStart(2.0);
	}
//this->mSongOver = true;
//this->mScrollTime = 0;
	d3di->Create();
	this->mpTimer = SetTimer(d3di->HWnd(), 1, 1000/MUSIC_RATE, 0);

	switch (this->mGameInterface)
	{
	default:
	case 0:
		break;
	case 1:
		{
			MIDIInterface *midi = MIDIInterface::MIDI();
			midi->InitializeIn(this->mMIDIdev, d3di->HWnd());
			midi->StartIn();
		}
		break;
	case 2:
		{
			Gamepad *pad = Gamepad::Pad();
			pad->SetHWND(d3di->HWnd());
		}
		break;
	}

	d3di->Start();

	if (this->mpTimer != 0)
		KillTimer(d3di->HWnd(), this->mpTimer);
	this->mpTimer = 0;
}

//*********************************************************
// Play MIDI notes from the record buffer

void DeviceA::InstantReplay(double oldTime)
{
	MIDIInterface *midi = MIDIInterface::MIDI();
	double time = this->mSong->PlaySongTime();
	MIDINoteGame *pNote = this->mpRecBuffer;
	while (pNote < this->mpRecBuffer+this->mNumRecNotes)
	{
		if (pNote->mStartTime >= time)
			break;
		if (pNote->mStartTime >= oldTime-.0005)
			midi->SendShrtMsg(pNote->mRStatus, pNote->mRParam1, pNote->mRParam2);
		if (pNote->mStopTime >= oldTime-.0005 && pNote->mStopTime < time)
			midi->SendShrtMsg(pNote->mPStatus, pNote->mPParam1, pNote->mPParam2);
		pNote++;
	}
}

//*********************************************************
// Message pump that handles pre-game, game, and post-game
// Handles keystrokes, MIDI message, gamepad input and mouse input

LRESULT DeviceA::MsgProc(UINT msg, WPARAM wParam, LPARAM lParam)
{
	Mapper *map = Mapper::Map();
	MIDIInterface *midi = MIDIInterface::MIDI();

	switch (msg)
	{
	case WM_MOUSEMOVE:
		{
			int mx = GET_X_LPARAM(lParam);
			int my = GET_Y_LPARAM(lParam);
			if (this->mSongOver && !this->mReplaySong && (wParam & MK_LBUTTON))
			{
				double diff = (double)(my-this->mMouseY)/30;
				this->mScrollTime += diff;
				if (this->mScrollTime < 0)
					this->mScrollTime = 0;
				else if (this->mScrollTime > this->mEndTime+ENDSONGTIME)
					this->mScrollTime = this->mEndTime+ENDSONGTIME;
			}
			this->mMouseX = mx;
			this->mMouseY = my;
		}
		break;

	case WM_TIMER:
		{
			if (this->mStartDelay > 0)
				this->mStartDelay -= RATE_MULTIPLIER/MUSIC_RATE;
			else
			{
				if (!this->mSongOver && this->mEndTime+ENDSONGTIME < this->mSong->PlaySongTime())
				{
					this->mScrollTime = this->mSong->PlaySongTime();
					this->mSongOver = true;
					this->mReplaySong = false;
					midi->StopOut();
				}

				if (!this->mSongOver)
				{
					double oldTime = this->mSong->PlaySongTime();
					this->mSong->PlayUpdate(RATE_MULTIPLIER/MUSIC_RATE);
					if (this->mReplaySong)
						this->InstantReplay(oldTime);
				}

				if (!this->mPaintMess)
				{
					PostMessage(D3DInterface::D3DI()->HWnd(), WM_PAINT, 0, 0);
					this->mPaintMess = true;
				}

				if (this->mGameInterface == 2 && !this->mSongOver && !this->mReplaySong)
				{
					Gamepad *pad = Gamepad::Pad();
					if (pad->IsGamepad())
					{
						pad->UpdateGamepad();
						for (UCHAR i=0; i<map->LastIndex(); i++)
						{
							UCHAR button = map->GamepadCode(i);
							if (pad->Button(button) ^ pad->OldButton(button))
							{
								UCHAR note = 0xFF;
								UCHAR ind = map->FindGamepad(button);
								if (ind != 0xFF)
									note = map->GetNote(ind);
								if (this->mPlayUnmarked)
								{
									if (note == 0xFF)
										note = map->CodeGamepad(button);
								}
								else if (ind != 0xFF && !map->Use(ind))
									note = 0xFF;
								if (note != 0xFF)
								{
									if (pad->Button(button))
										this->RecordNote(0x90+this->mSong->PlayChannel(), note, this->FindMIDIVel(note));
									else
										this->RecordNote(0x80+this->mSong->PlayChannel(), note, 0x00);
								}
							}
						}
					}
				}
			}
		}
		return 0;

	case WM_KEYDOWN:
		if (wParam == VK_ESCAPE)
			PostMessage(D3DInterface::D3DI()->HWnd(), WM_CLOSE, 0, 0);
		if (this->mGameInterface == 0 && !this->mSongOver && !this->mReplaySong)
		{
			UCHAR note = 0xFF;
			UCHAR ind = map->FindKey((int)wParam);
			if (ind != 0xFF)
				note = map->GetNote(ind);
			if (this->mPlayUnmarked)
			{
				if (note == 0xFF)
					note = map->CodeKey((int)wParam);
			}
			else if (ind != 0xFF && !map->Use(ind))
				note = 0xFF;
			if (note != 0xFF && !(lParam & (1<<30)))
				this->RecordNote(0x90+this->mSong->PlayChannel(), note, this->FindMIDIVel(note));
		}
		if (wParam == 'R')
		{
			if (this->mSongOver && !this->mReplaySong)
			{
				this->mSong->Play();
				this->mSong->DelayStart(2.0);
				this->mReplaySong = true;
				this->mSongOver = false;
			}
			else if (!this->mSongOver && this->mReplaySong)
			{
				this->mReplaySong = false;
				this->mSongOver = true;
				this->mScrollTime = this->mSong->PlaySongTime();
				midi->StopOut();
			}
		}
		if (wParam == VK_SPACE && this->mSongOver && !this->mReplaySong)
		{
			for (int i=0; i<128; i++)
				this->mNoteOn[i] = false;
			this->mScrollTime = this->mSong->PlaySongTime();
			this->mNumRecNotes = 0;
			this->mSongOver = false;
			this->mSong->Play();
			this->mSong->DelayStart(2.0);
		}
		return 0;

	case WM_KEYUP:
		if (this->mGameInterface == 0 && !this->mSongOver && !this->mReplaySong)
		{
			UCHAR note = 0xFF;
			UCHAR ind = map->FindKey((int)wParam);
			if (ind != 0xFF)
				note = map->GetNote(ind);
			if (this->mPlayUnmarked)
			{
				if (note == 0xFF)
					note = map->CodeKey((int)wParam);
			}
			else if (ind >= 0 && !map->Use(ind))
				note = 0xFF;
			if (note != 0xFF)
				this->RecordNote(0x80+this->mSong->PlayChannel(), note, 0x00);
		}
		return 0;

	case MM_MIM_DATA:
		if (midi->IsDeviceIn() && !this->mSongOver && !this->mReplaySong)
		{
			do
			{
				MIDIMessage *message = midi->GetChanMess();
				if (message != NULL)
				{
					UCHAR evStat = message->mStatus & 0xF0;
					if (evStat == 0x90 && message->mParam2 == 0)	// check for note off
						evStat = 0x80;
					if (evStat == 0x90 || evStat == 0x80)
					{
						evStat = evStat + this->mSong->PlayChannel();
						if (this->mGameInterface == 1)				// send out all MIDI notes even if not used
						{
							UCHAR ind = 0xFF;
							if (this->mPlayUnmarked || ((ind = map->FindMIDI(message->mParam1)) != 0xFF && map->Use(ind)))
							{
								UCHAR note;
								if (ind != 0xFF)
									note = map->GetNote(ind);
								else
									note = map->CodeMIDI(message->mParam1);
								if (note != 0xFF)
								{
									UCHAR midiVel;
									if (this->mPlayMIDIVel)
										midiVel = message->mParam2;
									else
										midiVel = this->FindMIDIVel(note);
									this->RecordNote(evStat, note, midiVel);
								}
							}
						}
					}
				}
			} while (midi->NextChanMess());
		}
		return 0;

	default:
		break;
	}

	return 1;
}

//*********************************************************
// Display the note text on the bottom of the screen

void DeviceA::DisplayText(UCHAR ind, RECT &rc, float *c)
{
	D3DInterface *d3di = D3DInterface::D3DI();
	Mapper *map = Mapper::Map();
	TCHAR *noteS;
	if (this->mSong->PlayChannel() == 9)
		noteS = DrumMapStr[map->GetNote(ind)];
	else
		noteS = map->NoteS(ind);
	if (this->mGameDisplay)
		d3di->FontSmall()->DrawText(NULL, noteS, -1, &rc, DT_NOCLIP,
				   D3DXCOLOR(*c, *(c+1), *(c+2), 1.0f));
	else
	{
		switch (this->mGameInterface)
		{
		case 0:
			d3di->FontSmall()->DrawText(NULL, map->KeyS(ind), -1, &rc, DT_NOCLIP,
					   D3DXCOLOR(*c, *(c+1), *(c+2), 1.0f));
			break;
		case 1:
			d3di->FontSmall()->DrawText(NULL, noteS, -1, &rc, DT_NOCLIP,
					   D3DXCOLOR(*c, *(c+1), *(c+2), 1.0f));
			break;
		case 2:
			d3di->FontSmall()->DrawText(NULL, map->GamepadS(ind), -1, &rc, DT_NOCLIP,
					   D3DXCOLOR(*c, *(c+1), *(c+2), 1.0f));
			break;
		}
	}
}

//*********************************************************
// Calculate the x-position for a note

int CalcXPos(int w, int n, int d, int msn)
{
	if (msn >= MINSCRNOTES)
		return (w*n+w/3)/d;
	return (w*n+w*(d-msn)/2+w/3)/d;
}

//*********************************************************
// Find the closest note to the user's note to get an
// appropriate MIDI volume

UCHAR DeviceA::FindMIDIVel(UCHAR note)
{
	if (this->mReverseNote[note] == 0xFF)
		return 0x7F;
	double time = this->mSong->PlaySongTime();
	MIDINoteGame *pNote = this->mpNoteBuffer;
	MIDINoteGame *pLNote = NULL;
	while (pNote < this->mpNoteBuffer+this->mTotalNotes)
	{
		if (pNote->mStartTime > time && pNote->mRParam1 == note)
			break;
		if (pNote->mRParam1 == note)
			pLNote = pNote;
		pNote++;
	}
	if (pNote == this->mpNoteBuffer+this->mTotalNotes)
		return 0x7F;
	if (pLNote == NULL)
		return pNote->mRParam2;
	if (pLNote->mStopTime > time || time-pLNote->mStopTime < pNote->mStartTime-time)
		return pLNote->mRParam2;
	return pNote->mRParam2;
}

//*********************************************************
// Record a note in the buffer, make sure that it is not overrun
// Match all note offs with all the note ons

void DeviceA::RecordNote(UCHAR stat, UCHAR note, UCHAR vel)
{
	int ind = this->mReverseNote[note];
	double curtime = this->mSong->PlaySongTime();
	if (ind != 0xFF && this->mStartDelay <= 0)
	{
		MIDINoteGame *pRecNote = this->mpRecBuffer+this->mNumRecNotes;
		this->mNoteOn[note] = ((stat & 0x90) == 0x90);
		if (this->mNoteOn[note])
		{
			if (this->mNumRecNotes < this->mTotalNotes*8)
			{
				pRecNote->mStartTime = curtime;
				pRecNote->mStopTime = 0;
				pRecNote->mRStatus = stat;
				pRecNote->mRParam1 = note;
				pRecNote->mRParam2 = vel;
				this->mNumRecNotes++;
				TRACE(_T("Times: %f  %f  %X  Note on: %x %d %d\n"), pRecNote->mStartTime, pRecNote->mStopTime, pRecNote, stat, note, vel);

				// make sure we have note offs for any note ons of this note
				while (pRecNote > this->mpRecBuffer)
				{
					pRecNote--;
					if (pRecNote->mRParam1 == note)
					{
						if (pRecNote->mStopTime == 0)
						{
							pRecNote->mStopTime = curtime-.005;	// take a few millisecond off this time
							TRACE(_T("Times: %f  %f  %X  Note early off: %x %d %d\n"), pRecNote->mStartTime, pRecNote->mStopTime, pRecNote, stat, note, vel);
						}
						pRecNote = NULL;
					}
				}
			}
		}
		else
		{
			while (pRecNote > this->mpRecBuffer)
			{
				pRecNote--;
				if (pRecNote->mRParam1 == note)
				{
					if (pRecNote->mStopTime == 0)			// make sure no stop time has been set
					{
						pRecNote->mStopTime = curtime;		// otherwise stop and start times will overlap
						TRACE(_T("Times: %f  %f  %X  Note off: %x %d %d\n"), pRecNote->mStartTime, pRecNote->mStopTime, pRecNote, stat, note, vel);
					}
					pRecNote->mPStatus = stat;
					pRecNote->mPParam1 = note;
					pRecNote->mPParam2 = vel;
					pRecNote = NULL;
				}
			}
		}
	}
	MIDIInterface::MIDI()->SendShrtMsg(stat, note, vel);
}

//*********************************************************
// Render the playable notes from the buffer

void DeviceA::RenderBuffer(float xoff, MIDINoteGame *pNote, MIDINoteGame *pBuffEnd)
{
	D3DInterface *d3di = D3DInterface::D3DI();
	CUSTOMVERTEX *vertices = d3di->VB();
	int j = 0;
	double wt = WINDOWTIME*DifficultyTime[this->mGameDifficulty];
	double songTime;
	if (this->mSongOver)
		songTime = this->mScrollTime;
	else
		songTime = this->mSong->PlaySongTime();
	double timeStart = songTime - wt/16;
	double timeEnd = timeStart + wt;
	float timeWind = (float)(timeEnd-timeStart);
	while (pNote < pBuffEnd)
	{
		if (pNote->mStopTime < timeStart && pNote->mStopTime != 0)
			pNote++;
		else if (pNote->mStartTime > timeEnd)
			break;
		else
		{
			if (j == NUMQUADS)
			{
				d3di->TransferVB(j*4);
				for (int k=0; k<j; k++)
					d3di->Device()->DrawPrimitive(D3DPT_TRIANGLESTRIP, 4*k, 2);
				j = 0;
			}
			double t1 = pNote->mStartTime;
			double t2 = pNote->mStopTime;
			if (t2 == 0)
				t2 = songTime;
			if (t1 < timeStart)
				t1 = timeStart;
			if (t2 > timeEnd)
				t2 = timeEnd;
			float tt1 = (float)(t1-timeStart)/timeWind;
			float tt2 = (float)(t2-timeStart)/timeWind;
			int i = this->mReverseNote[pNote->mRParam1];
			vertices[j*4].x = vertices[j*4+1].x = this->mSaveX[i]-.25f+xoff;
			vertices[j*4+2].x = vertices[j*4+3].x = this->mSaveX[i]+xoff;
			float yVal = -this->mNearZ+.05f;
			if (this->mGameStyle == 1 && SemiTones[this->mSaveNote[i]%12])
				yVal += .25f;
			vertices[j*4].y = vertices[j*4+1].y = vertices[j*4+2].y = vertices[j*4+3].y = yVal;
			vertices[j*4+1].z = vertices[j*4+3].z = this->mNearZ+tt2*(this->mFarZ-this->mNearZ);
			vertices[j*4].z = vertices[j*4+2].z = this->mNearZ+tt1*(this->mFarZ-this->mNearZ);
			float *c;
			if (xoff != 0)
				c = OctaveColours[12];
			else
				c = this->mSaveColor[i];
			vertices[j*4].color = vertices[j*4+2].color = 0xff000000+(((int)(*c*168))<<16)+(((int)(*(c+1)*168))<<8)+((int)(*(c+2)*168));
			vertices[j*4+1].color = vertices[j*4+3].color = 0xff000000+(((int)(*c*255))<<16)+(((int)(*(c+1)*255))<<8)+((int)(*(c+2)*255));
			j++;
			pNote++;
		}
	}
	if (j > 0)
	{
		d3di->TransferVB(j*4);
		for (int k=0; k<j; k++)
			d3di->Device()->DrawPrimitive(D3DPT_TRIANGLESTRIP, 4*k, 2);
	}
}

//*********************************************************
// This is the global render function that grabs the singleton
// and calls the class function

void DeviceA::Render(int w, int h)
{
	gGame->Render2(w, h);
}

//*********************************************************
// This is the class render function

void DeviceA::Render2(int w, int h)
{
	D3DInterface *d3di = D3DInterface::D3DI();

	// render the note strings
	RECT rc;
	d3di->Device()->SetTransform(D3DTS_PROJECTION, &IdentityMat);
	SetRect(&rc, 0, 20, 0, 0);
	float *c = OctaveColours[12];

	// display the playback time
	double songTime;
	if (this->mSongOver)
		songTime = this->mScrollTime;
	else
		songTime = this->mSong->PlaySongTime();
	if (songTime < 0)
		songTime = 0;
	d3di->FontLarge()->DrawText(NULL, TPrint(_T("%d:%02d.%02d"), (int)(songTime/60), ((int)songTime)%60, ((int)(songTime*100))%100),
				-1, &rc, DT_NOCLIP,
			   D3DXCOLOR(*c, *(c+1), *(c+2), 1.0f));

	// display game messages
	if (this->mReplaySong)
	{
		SetRect(&rc, w/2-110, 20, 0, 0);
		float *c = OctaveColours[12];
		d3di->FontLarge()->DrawText(NULL, _T("INSTANT REPLAY"), -1, &rc, DT_NOCLIP,
				   D3DXCOLOR(*c, *(c+1), *(c+2), 1.0f));
	}
	else if (this->mSongOver)
	{
		SetRect(&rc, w/2-90, 20, 0, 0);
		float *c = OctaveColours[12];
		d3di->FontLarge()->DrawText(NULL, _T("SONG OVER"), -1, &rc, DT_NOCLIP,
				   D3DXCOLOR(*c, *(c+1), *(c+2), 1.0f));
	}
	else if (this->mStartDelay > 0)
	{
		SetRect(&rc, w/2-90, 20, 0, 0);
		float *c = OctaveColours[12];
		d3di->FontLarge()->DrawText(NULL, _T("GET READY"), -1, &rc, DT_NOCLIP,
				   D3DXCOLOR(*c, *(c+1), *(c+2), 1.0f));
	}
	else
	{
		SetRect(&rc, w/2-90, 20, 0, 0);
		float *c = OctaveColours[12];
		d3di->FontLarge()->DrawText(NULL, _T("PLAY NOTES!"), -1, &rc, DT_NOCLIP,
				   D3DXCOLOR(*c, *(c+1), *(c+2), 1.0f));
	}

	// display the note strings
	for (int i=0; i<this->mNoteCount; i++)
	{
		int sn = this->mScreenNotes;
		if (sn < MINSCRNOTES)
			sn = MINSCRNOTES;
		if (this->mGameStyle == 0)
			SetRect(&rc, CalcXPos(w, i, sn, this->mScreenNotes), h*16/19, 0, 0);			// 0's okay since no clipping
		else if (this->mGameStyle == 1)
			SetRect(&rc, CalcXPos(w, this->mSaveNote[i]-this->mLowNote, sn, this->mScreenNotes), h*16/19, 0, 0);
		else //if (this->mGameStyle == 2)
			;
		float *c;
		if (this->mNoteOn[this->mSaveNote[i]])
			c = OctaveColours[12];
		else
			c = this->mSaveColor[i];
		this->DisplayText(this->mSaveInd[i], rc, c);
	}

	// set up camera and transform matrices
	D3DXMATRIXA16 matWorld;
	D3DXMatrixRotationX(&matWorld, -30*rad);
	d3di->Device()->SetTransform(D3DTS_WORLD, &matWorld);

    D3DXVECTOR3 vEyePt( 0.0f, 0.0f, 0.0f );
    D3DXVECTOR3 vLookatPt( 0.0f, 0.0f, 5.0f );
    D3DXVECTOR3 vUpVec( 0.0f, 1.0f, 0.0f );
    D3DXMATRIXA16 matView;
    D3DXMatrixLookAtLH( &matView, &vEyePt, &vLookatPt, &vUpVec );
    d3di->Device()->SetTransform( D3DTS_VIEW, &matView );

    D3DXMATRIXA16 matProj;
    D3DXMatrixPerspectiveFovLH( &matProj, 46*rad, 1.0f, (float)w/(float)h, this->mFarZ*2 );
    d3di->Device()->SetTransform( D3DTS_PROJECTION, &matProj );
	CUSTOMVERTEX *vertices = d3di->VB();

	// draw the play bars
	int i;
	int j = 0;
	for (i=0; i<this->mNoteCount; i++)
	{
		if (j == NUMQUADS)
		{
			d3di->TransferVB(j*4);
			for (int k=0; k<j; k++)
				d3di->Device()->DrawPrimitive(D3DPT_TRIANGLESTRIP, 4*k, 2);
			j = 0;
		}
		vertices[j*4].x = vertices[j*4+1].x = this->mSaveX[i]-.35f;
		vertices[j*4+2].x = vertices[j*4+3].x = this->mSaveX[i]+.35f;
		float yVal = -this->mNearZ;
		if (this->mGameStyle == 1 && SemiTones[this->mSaveNote[i]%12])
			yVal += .25f;
		vertices[j*4].y = vertices[j*4+1].y = vertices[j*4+2].y = vertices[j*4+3].y = yVal;
		vertices[j*4+1].z = vertices[j*4+3].z = this->mFarZ;
		vertices[j*4].z = vertices[j*4+2].z = this->mNearZ;
		float *c = this->mSaveColor[i];
		vertices[j*4].color = vertices[j*4+2].color = 0xff000000+(((int)(*c*64))<<16)+(((int)(*(c+1)*64))<<8)+((int)(*(c+2)*64));
		vertices[j*4+1].color = vertices[j*4+3].color = 0xff000000+(((int)(*c*125))<<16)+(((int)(*(c+1)*125))<<8)+((int)(*(c+2)*125));
		j++;
	}

	// draw the play line
	if (j == NUMQUADS)
	{
		d3di->TransferVB(j*4);
		for (int k=0; k<j; k++)
			d3di->Device()->DrawPrimitive(D3DPT_TRIANGLESTRIP, 4*k, 2);
		j = 0;
	}
	vertices[j*4].x = vertices[j*4+1].x = this->mSaveX[0]-1;
	vertices[j*4+2].x = vertices[j*4+3].x = this->mSaveX[this->mNoteCount-1]+1;
	vertices[j*4].y = vertices[j*4+1].y = vertices[j*4+2].y = vertices[j*4+3].y = -this->mNearZ+.3f;
	vertices[j*4+1].z = vertices[j*4+3].z = this->mNearZ+(this->mFarZ-this->mNearZ)/15;
	vertices[j*4].z = vertices[j*4+2].z = this->mNearZ+(this->mFarZ-this->mNearZ)/17;
	vertices[j*4].color = vertices[j*4+2].color = 0xff808080;
	vertices[j*4+1].color = vertices[j*4+3].color = 0xffC0C0C0;
	j++;
	d3di->TransferVB(j*4);
	for (int k=0; k<j; k++)
		d3di->Device()->DrawPrimitive(D3DPT_TRIANGLESTRIP, 4*k, 2);

	this->RenderBuffer(0,this->mpNoteBuffer,this->mpNoteBuffer+this->mTotalNotes);
	this->RenderBuffer(0.25,this->mpRecBuffer,this->mpRecBuffer+this->mNumRecNotes);
	this->mPaintMess = false;
}

