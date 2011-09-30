// Burner Input module
#include "burner.h"

unsigned int nInputSelect = 0;
bool bInputOkay = false;

static bool bCinpOkay;

#if defined (_XBOX)
	extern struct InputInOut InputInOutXInput2;
#elif defined (_WIN32)
	extern struct InputInOut InputInOutDInput;
#elif defined (BUILD_SDL)
	extern struct InputInOut InputInOutSDL;
#endif

static struct InputInOut *pInputInOut[]=
{
#if defined (_XBOX)
	&InputInOutXInput2,
#elif defined (_WIN32)
	&InputInOutDInput,
#elif defined (BUILD_SDL)
	&InputInOutSDL,
#endif
};

#define INPUT_LEN (sizeof(pInputInOut) / sizeof(pInputInOut[0]))

static InterfaceInfo InpInfo = { NULL, };

inline int CinpState(const int nCode)
{
	// Return off for keyboard inputs if current input is turned off
	if (nCode < 0x4000 && !bCinpOkay)
		return 0;

	// Read from Direct Input
#ifdef _XBOX
	extern int DinpState(int nCode);
	return DinpState(nCode);
#else
	return pInputInOut[nInputSelect]->ReadSwitch(nCode);
#endif
}

// Read an axis of a joystick
inline int CinpJoyAxis(const int i, const int nAxis)
{
	return pInputInOut[nInputSelect]->ReadJoyAxis(i, nAxis);	// Read from Direct Input
}

// Read an axis of a mouse
inline int CinpMouseAxis(const int i, const int nAxis)
{
	return pInputInOut[nInputSelect]->ReadMouseAxis(i, nAxis);	// Read from Direct Input
}

int InputInit()
{
	int nRet;

	bInputOkay = false;

	if (nInputSelect >= INPUT_LEN)
		return 1;

	if ((nRet = pInputInOut[nInputSelect]->Init()) == 0)
		bInputOkay = true;

	return nRet;
}

int InputExit()
{
	IntInfoFree(&InpInfo);

	if (nInputSelect >= INPUT_LEN)
		return 1;

	bInputOkay = false;

	return pInputInOut[nInputSelect]->Exit();
}

// This will request exclusive access for mouse and/or request input processing if the application is in the foreground only
//  - bExcusive = 1   - Request exclusive access to inputs (this may apply to only the mouse or all input, depending on the API)
//                      This function will show or hide the mouse cursor as appropriate
//  - bForeground = 1 - Request input processing only if application is in foreground (this may not be supported by all APIs)
int InputSetCooperativeLevel(const bool bExclusive, const bool bForeground)
{
	if (!bInputOkay || nInputSelect >= INPUT_LEN)
		return 1;

	if (pInputInOut[nInputSelect]->SetCooperativeLevel == NULL)
		return 0;

	return pInputInOut[nInputSelect]->SetCooperativeLevel(bExclusive, bForeground);
}

// This will process all PC-side inputs and optionally update the emulated game side.
int InputMake(bool bCopy)
{
	struct GameInp* pgi;
	unsigned int i;

	if (nInputSelect >= INPUT_LEN)
		return 1;

	pInputInOut[nInputSelect]->NewFrame();			// Poll joysticks etc

	bCinpOkay = AppProcessKeyboardInput();

	// Do one frames worth of keyboard input sliders
	// Begin of InputTick()

	for (i = 0, pgi = GameInp; i < nGameInpCount; i++, pgi++) {
		int nAdd = 0;
		if ((pgi->nInput &  GIT_GROUP_SLIDER) == 0)				// not a slider
			continue;

		if (pgi->nInput == GIT_KEYSLIDER)
		{
			// Get states of the two keys
			if (CinpState(pgi->Input.Slider.SliderAxis.nSlider[0]))
				nAdd -= 0x100;
			if (CinpState(pgi->Input.Slider.SliderAxis.nSlider[1]))
				nAdd += 0x100;
		}

		if (pgi->nInput == GIT_JOYSLIDER)
		{	// Get state of the axis
			nAdd = CinpJoyAxis(pgi->Input.Slider.JoyAxis.nJoy, pgi->Input.Slider.JoyAxis.nAxis);
			nAdd /= 0x100;
		}

		// nAdd is now -0x100 to +0x100

		// Change to slider speed
		nAdd *= pgi->Input.Slider.nSliderSpeed;
		nAdd /= 0x100;

		if (pgi->Input.Slider.nSliderCenter) {						// Attact to center
			int v = pgi->Input.Slider.nSliderValue - 0x8000;
			v *= (pgi->Input.Slider.nSliderCenter - 1);
			v /= pgi->Input.Slider.nSliderCenter;
			v += 0x8000;
			pgi->Input.Slider.nSliderValue = v;
		}

		pgi->Input.Slider.nSliderValue += nAdd;
		// Limit slider
		if (pgi->Input.Slider.nSliderValue < 0x0100)
			pgi->Input.Slider.nSliderValue = 0x0100;
		if (pgi->Input.Slider.nSliderValue > 0xFF00)
			pgi->Input.Slider.nSliderValue = 0xFF00;
	}
	// End of InputTick()

	for (i = 0, pgi = GameInp; i < nGameInpCount; i++, pgi++)
	{
		if (pgi->Input.pVal == NULL)
			continue;

		switch (pgi->nInput)
		{
			case 0:									// Undefined
				pgi->Input.nVal = 0;
				break;
			case GIT_CONSTANT:						// Constant value
				pgi->Input.nVal = pgi->Input.Constant.nConst;
				if (bCopy)
					*(pgi->Input.pVal) = pgi->Input.nVal;
				break;
			case GIT_SWITCH: {						// Digital input
						 int s = CinpState(pgi->Input.Switch.nCode);

						 if (pgi->nType & BIT_GROUP_ANALOG) {
							 // Set analog controls to full
							 if (s) {
								 pgi->Input.nVal = 0xFFFF;
							 } else {
								 pgi->Input.nVal = 0x0001;
							 }
							 if (bCopy) {
#ifdef LSB_FIRST
								 *(pgi->Input.pShortVal) = pgi->Input.nVal;
#else
								 *((int *)pgi->Input.pShortVal) = pgi->Input.nVal;
#endif
							 }
						 } else {
							 // Binary controls
							 if (s) {
								 pgi->Input.nVal = 1;
							 } else {
								 pgi->Input.nVal = 0;
							 }
							 if (bCopy) {
								 *(pgi->Input.pVal) = pgi->Input.nVal;
							 }
						 }

						 break;
					 }
			case GIT_KEYSLIDER:						// Keyboard slider
			case GIT_JOYSLIDER:                 // Joystick slider
					 {
						 int nSlider = pgi->Input.Slider.nSliderValue;
						 if (pgi->nType == BIT_ANALOG_REL) {
							 nSlider -= 0x8000;
							 nSlider >>= 4;
						 }

						 pgi->Input.nVal = (unsigned short)nSlider;
						 if (bCopy) {
#ifdef LSB_FIRST
							 *(pgi->Input.pShortVal) = pgi->Input.nVal;
#else
							 *((int *)pgi->Input.pShortVal) = pgi->Input.nVal;
#endif
						 }
						 break;
					 }
			case GIT_MOUSEAXIS:						// Mouse axis
					 pgi->Input.nVal = (unsigned short)(CinpMouseAxis(pgi->Input.MouseAxis.nMouse, pgi->Input.MouseAxis.nAxis) * nAnalogSpeed);
					 if (bCopy) {
#ifdef LSB_FIRST
						 *(pgi->Input.pShortVal) = pgi->Input.nVal;
#else				
						 *((int *)pgi->Input.pShortVal) = pgi->Input.nVal;
#endif
					 }
					 break;
			case GIT_JOYAXIS_FULL:	{				// Joystick axis
							int nJoy = CinpJoyAxis(pgi->Input.JoyAxis.nJoy, pgi->Input.JoyAxis.nAxis);

							if (pgi->nType == BIT_ANALOG_REL) {
								nJoy *= nAnalogSpeed;
								nJoy >>= 13;

								// Clip axis to 8 bits
								if (nJoy < -32768) {
									nJoy = -32768;
								}
								if (nJoy >  32767) {
									nJoy =  32767;
								}
							} else {
								nJoy >>= 1;
								nJoy += 0x8000;

								// Clip axis to 16 bits
								if (nJoy < 0x0001) {
									nJoy = 0x0001;
								}
								if (nJoy > 0xFFFF) {
									nJoy = 0xFFFF;
								}
							}

							pgi->Input.nVal = (unsigned short)nJoy;
							if (bCopy) {
#ifdef LSB_FIRST
								*(pgi->Input.pShortVal) = pgi->Input.nVal;
#else
								*((int *)pgi->Input.pShortVal) = pgi->Input.nVal;
#endif
							}

							break;
						}
			case GIT_JOYAXIS_NEG:	{				// Joystick axis Lo
							int nJoy = CinpJoyAxis(pgi->Input.JoyAxis.nJoy, pgi->Input.JoyAxis.nAxis);
							if (nJoy < 32767) {
								nJoy = -nJoy;

								if (nJoy < 0x0000) {
									nJoy = 0x0000;
								}
								if (nJoy > 0xFFFF) {
									nJoy = 0xFFFF;
								}

								pgi->Input.nVal = (unsigned short)nJoy;
							} else {
								pgi->Input.nVal = 0;
							}

							if (bCopy) {
#ifdef LSB_FIRST
								*(pgi->Input.pShortVal) = pgi->Input.nVal;
#else
								*((int *)pgi->Input.pShortVal) = pgi->Input.nVal;
#endif
							}
							break;
						}
			case GIT_JOYAXIS_POS:	{				// Joystick axis Hi
							int nJoy = CinpJoyAxis(pgi->Input.JoyAxis.nJoy, pgi->Input.JoyAxis.nAxis);
							if (nJoy > 32767) {

								if (nJoy < 0x0000) {
									nJoy = 0x0000;
								}
								if (nJoy > 0xFFFF) {
									nJoy = 0xFFFF;
								}

								pgi->Input.nVal = (unsigned short)nJoy;
							} else {
								pgi->Input.nVal = 0;
							}

							if (bCopy) {
#ifdef LSB_FIRST
								*(pgi->Input.pShortVal) = pgi->Input.nVal;
#else
								*((int *)pgi->Input.pShortVal) = pgi->Input.nVal;
#endif
							}
							break;
						}
		}
	}

	for (i = 0; i < nMacroCount; i++, pgi++)
	{
		if (pgi->Macro.nMode)
		{	 // Macro is defined

			if (bCopy && CinpState(pgi->Macro.Switch.nCode))
			{
				for (int j = 0; j < 4; j++)
				{
					if (pgi->Macro.pVal[j])
						*(pgi->Macro.pVal[j]) = pgi->Macro.nVal[j];
				}
			}
		}
	}

	return 0;
}

// Use this function as follows:
//  - First call with nFlags =  2
//  -  Then call with nFlags =  4 until all controls are released
//  -  Then call with nFlags =  8 until a control is activated
//  -  Then call with nFlags = 16 until all controls are released again
//     It will continue to return a control (reflecting the direction an axis is moved in)
//     Use this to handle analog controls correctly
//
// Call with nFlags & 1 to indicate that controls need to be polled (when normal input processing is disabled)
int InputFind(const int nFlags)
{
	static int nInputCode, nDelay, nJoyPrevPos;

	int nFind;

	if (nInputSelect >= INPUT_LEN)
		return 1;

#ifndef _XBOX
	if (nFlags & 1)
		pInputInOut[nInputSelect]->NewFrame();
#endif

	nFind = pInputInOut[nInputSelect]->Find(nFlags & 2);

	switch (nFlags)
	{
		case  4:
			return nFind;
		case  8:
			{
				if (nFind >= 0) {
					nInputCode = nFind;
					if ((nInputCode & 0x4000) && (nInputCode & 0xFF) < 0x10)
						nJoyPrevPos = CinpJoyAxis((nInputCode >> 8) & 0x3F, (nInputCode >> 1) & 0x07);
					nDelay = 0;
				}

				return nFind;
			}
		case 16:
		{

				 // Treat joystick axes specially
				 // Wait until the axis reports no movement for some time
				 if ((nInputCode & 0x4000) && (nInputCode & 0xFF) < 0x10) {
					 int nJoyPos = CinpJoyAxis((nInputCode >> 8) & 0x3F, (nInputCode >> 1) & 0x07);
					 int nJoyDelta = nJoyPrevPos - nJoyPos;

					 nJoyPrevPos = nJoyPos;

					 if (nFind != -1) {
						 nInputCode = nFind;
					 }

					 // While the movement is within the threshold, treat it as no movement
					 if (nJoyDelta > -0x0100 || nJoyDelta < 0x0100) {
						 nDelay++;
						 if (nDelay > 64)
							 return -1;
					 }
					 else
						 nDelay = 0;

					 return nInputCode;
				 }

				 // Treat mouse axes specially
				 // Wait until the axis reports no movement/movement in the same direction for some time
				 if ((nInputCode & 0x8000) && (nInputCode & 0xFF) < 0x06) {
					 int nMouseDelta = CinpMouseAxis((nInputCode >> 8) & 0x3F, (nInputCode >> 1) & 0x07);
					 if (nFind == -1 || ((nInputCode & 1) ? nMouseDelta > 0 : nMouseDelta < 0)) {
						 nDelay++;
						 if (nDelay > 128)
							 return -1;
					 } else {
						 nDelay = 0;
						 nInputCode = nFind;
					 }

					 return nInputCode;
				 }

				 return nFind;
			 }
	}

	return -1;
}

// Get the name of a control and/or the device it's on (not all API's may support this)
// Either parameter can be passed as NULL
int InputGetControlName(int nCode, TCHAR* pszDeviceName, TCHAR* pszControlName)
{
	if (!bInputOkay || nInputSelect >= INPUT_LEN)
		return 1;
	if (pInputInOut[nInputSelect]->GetControlName == NULL)
		return 1;

	return pInputInOut[nInputSelect]->GetControlName(nCode, pszDeviceName, pszControlName);
}

static const TCHAR* InpDriverName(unsigned int driver)
{
#if defined (_WIN32)
	return _T("DirectInput");
#elif defined (BUILD_SDL)
	return _T("SDLInput");
#endif

	return FBALoadStringEx(1);
}

InterfaceInfo* InputGetInfo()
{
	if (IntInfoInit(&InpInfo)) {
		IntInfoFree(&InpInfo);
		return NULL;
	}

	if (bInputOkay) {
		InpInfo.pszModuleName = InpDriverName(0);

		if (pInputInOut[nInputSelect]->GetSetting) {
			pInputInOut[nInputSelect]->GetSetting(&InpInfo);
		}

		for (int nType = 0; nType < 3; nType++) {
			int nDeviceTypes[] = { 0x0000, 0x8000, 0x4000 };
			TCHAR nDeviceTypeNames[][16] = { _T("keyboard"), _T("mouse   "), _T("joystick") };
			TCHAR nDeviceName[MAX_PATH] = _T("");
			int nActiveDevice = 0;

			while (nActiveDevice < 16 && pInputInOut[nInputSelect]->GetControlName(nDeviceTypes[nType] | (nActiveDevice << 8), nDeviceName, NULL) == 0 && nDeviceName[0]) {
				TCHAR szString[MAX_PATH] = _T("");

				_sntprintf(szString, sizearray(szString), _T("%s %d %s"), nDeviceTypeNames[nType], nActiveDevice, nDeviceName);

				if (IntInfoAddStringInterface(&InpInfo, szString)) {
					break;
				}

				nActiveDevice++;
			}
		}
	} else {
		IntInfoAddStringInterface(&InpInfo, _T("Input plugin not initialised"));
	}

	return &InpInfo;
}
