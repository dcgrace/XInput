/*
  Copyright (C) 2014 David Grace

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include <Windows.h>
#include <cstdio>
#include <xinput.h>

#include "../../SDK/API/RainmeterAPI.h"

// Overview: XInput device info.

// Sample skin:
/*
	[mXInput_JoyL_X]
	Measure=Plugin
	Plugin=XInput.dll
	Device=0
*/


#define MAX_XINPUT_DEVICES		4				///< max number of XInput devices to query (XUSER_MAX_COUNT?)
#define QUERY_TIMEOUT			(1.0/100)		///< minimum time interval between polls on a particular device
#define DO_BATTERY				0				///< the battery API is only available to the Win8 SDK and higher (XInput 1.4)


struct Device
{
	enum {
		CHANNEL_JOYL_X,
		CHANNEL_JOYL_Y,
		CHANNEL_JOYR_X,
		CHANNEL_JOYR_Y,
		CHANNEL_DPAD_U,
		CHANNEL_DPAD_D,
		CHANNEL_DPAD_L,
		CHANNEL_DPAD_R,
		CHANNEL_FACE_U,
		CHANNEL_FACE_D,
		CHANNEL_FACE_L,
		CHANNEL_FACE_R,
		CHANNEL_LTRIG1,
		CHANNEL_RTRIG1,
		CHANNEL_LTRIG2,
		CHANNEL_RTRIG2,
		CHANNEL_JOYL_CLICK,
		CHANNEL_JOYR_CLICK,
		CHANNEL_START,
		CHANNEL_BACK,
		CHANNEL_CONNECTED,
		CHANNEL_BATTERY_PAD,
		CHANNEL_BATTERY_HEADSET,
		// ... //
		NUM_CHANNELS
	};

	XINPUT_STATE		m_state;				///< current controller state
	LARGE_INTEGER		m_pcPoll;				///< performance counter on last device poll
	double				m_data[NUM_CHANNELS];	///< parsed data

	Device ()
	{
		m_pcPoll.QuadPart = 0;
		memset(&m_state, 0, sizeof(m_state));
		memset(m_data, 0, sizeof(m_data));
	}

	void Update (int devID);
};


struct Measure
{
	int		m_devID;			///< device ID # (parsed from options)
	int		m_channel;			///< channel # (parsed from options)

	Measure ()
		: m_devID	(0)
		, m_channel	(0)
	{
	}
};


static double	s_pcMult;							///< performance counter inv frequency
static Device	s_device[MAX_XINPUT_DEVICES];		///< device data


/**
 * Create and initialize a measure instance.
 *
 * @param[out]	data			Pointer address in which to return measure instance.
 * @param[in]	rm				Rainmeter context.
 */
PLUGIN_EXPORT void Initialize (void** data, void* rm)
{
	Measure* m	= new Measure;
	*data		= m;

	// initialize the performance counter freq if necessary
	if(s_pcMult == 0.0) {
		LARGE_INTEGER pcFreq;
		QueryPerformanceFrequency(&pcFreq);
		s_pcMult = 1.0 / (double)pcFreq.QuadPart;
	}
}


/**
 * Destroy the measure instance.
 *
 * @param[in]	data			Measure instance pointer.
 */
PLUGIN_EXPORT void Finalize (void* data)
{
	Measure* m = (Measure*)data;
	delete m;
}


/**
 * (Re-)parse parameters from .ini file.
 *
 * @param[in]	data			Measure instance pointer.
 * @param[in]	rm				Rainmeter context.
 * @param[out]	maxValue		?
 */
PLUGIN_EXPORT void Reload (void* data, void* rm, double* maxValue)
{
	Measure* m = (Measure*)data;

	static const LPCWSTR s_chanName[Device::NUM_CHANNELS] = {
		L"JoyL_X",			// CHANNEL_JOYL_X
		L"JoyL_Y",			// CHANNEL_JOYL_Y
		L"JoyR_X",			// CHANNEL_JOYR_X
		L"JoyR_Y",			// CHANNEL_JOYR_Y
		L"Dpad_U",			// CHANNEL_DPAD_U
		L"Dpad_D",			// CHANNEL_DPAD_D
		L"Dpad_L",			// CHANNEL_DPAD_L
		L"Dpad_R",			// CHANNEL_DPAD_R
		L"Face_U",			// CHANNEL_FACE_U
		L"Face_D",			// CHANNEL_FACE_D
		L"Face_L",			// CHANNEL_FACE_L
		L"Face_R",			// CHANNEL_FACE_R
		L"LTrig1",			// CHANNEL_LTRIG1
		L"RTrig1",			// CHANNEL_RTRIG1
		L"LTrig2",			// CHANNEL_LTRIG2
		L"RTrig2",			// CHANNEL_RTRIG2
		L"JoyL_Click",		// CHANNEL_JOYL_CLICK
		L"JoyR_Click",		// CHANNEL_JOYR_CLICK
		L"Start",			// CHANNEL_START
		L"Back",			// CHANNEL_BACK
		L"Connected",		// CHANNEL_CONNECTED
		L"Battery_Pad",		// CHANNEL_BATTERY_PAD
		L"Battery_Headset",	// CHANNEL_BATTERY_HEADSET
	};

	// parse device index
	if(
			((m->m_devID=RmReadInt(rm, L"Device", m->m_devID)) < 0)
			|| (m->m_devID >= MAX_XINPUT_DEVICES)
	) {
		RmLogF(rm, LOG_ERROR, L"Invalid Device %ld: must an integer between 0 and %ld - defaulting to 0.\n", m->m_devID, MAX_XINPUT_DEVICES-1);
		m->m_devID = 0;
	}

	// parse channel
	LPCWSTR channel = RmReadString(rm, L"Channel", L"");
	if(*channel) {
		int iChannel;
		for(iChannel=0; iChannel<Device::NUM_CHANNELS; ++iChannel) {
			if(_wcsicmp(channel, s_chanName[iChannel]) == 0) {
				m->m_channel = iChannel;
				break;
			}
		}
		if(!(iChannel<Device::NUM_CHANNELS)) {
			WCHAR	msg[512];
			WCHAR*	d	= msg;
			d			+= _snwprintf_s(
								d, (sizeof(msg)+(UINT32)msg-(UINT32)d)/sizeof(WCHAR), _TRUNCATE,
								L"Invalid Channel '%s', must be one of:",
								channel
						);
			for(unsigned int i=0; i<Device::NUM_CHANNELS; ++i) {
				d		+= _snwprintf_s(
								d, (sizeof(msg)+(UINT32)msg-(UINT32)d)/sizeof(WCHAR), _TRUNCATE,
								L"%s%s%s",
								(i)? L", " : L" ",
								(i==(Device::NUM_CHANNELS-1))? L"or " : L"",
								s_chanName[i]
						);
			}
			d			+= _snwprintf_s(
								d, (sizeof(msg)+(UINT32)msg-(UINT32)d)/sizeof(WCHAR), _TRUNCATE,
								L".\n"
						);
			RmLogF(rm, LOG_ERROR, msg);
		}
	}
}


/**
 * Update the measure.
 *
 * @param[in]	data			Measure instance pointer.
 * @return		Latest value - typically an audio level between 0.0 and 1.0.
 */
PLUGIN_EXPORT double Update (void* data)
{
	Measure*	m		= (Measure*)data;
	Device&		device	= s_device[m->m_devID];

	// re-poll from system if necessary
	device.Update(m->m_devID);

	return device.m_data[m->m_channel];
}


/**
 * Update an XInput device if necessary.
 */
void Device::Update (int devID)
{
	// map from XInput button bits to my channels
	// (see http://msdn.microsoft.com/en-us/library/windows/desktop/microsoft.directx_sdk.reference.xinput_gamepad%28v=vs.85%29.aspx)
	static const int s_buttonChan[16] = {
		CHANNEL_DPAD_U,			// XINPUT_GAMEPAD_DPAD_UP
		CHANNEL_DPAD_D,			// XINPUT_GAMEPAD_DPAD_DOWN
		CHANNEL_DPAD_L,			// XINPUT_GAMEPAD_DPAD_LEFT
		CHANNEL_DPAD_R,			// XINPUT_GAMEPAD_DPAD_RIGHT
		CHANNEL_START,			// XINPUT_GAMEPAD_START
		CHANNEL_BACK,			// XINPUT_GAMEPAD_BACK
		CHANNEL_JOYL_CLICK,		// XINPUT_GAMEPAD_LEFT_THUMB
		CHANNEL_JOYR_CLICK,		// XINPUT_GAMEPAD_RIGHT_THUMB
		CHANNEL_LTRIG1,			// XINPUT_GAMEPAD_LEFT_SHOULDER
		CHANNEL_RTRIG1,			// XINPUT_GAMEPAD_RIGHT_SHOULDER
		-1,						// (reserved)
		-1,						// (reserved)
		CHANNEL_FACE_D,			// XINPUT_GAMEPAD_A
		CHANNEL_FACE_R,			// XINPUT_GAMEPAD_B
		CHANNEL_FACE_L,			// XINPUT_GAMEPAD_X
		CHANNEL_FACE_U,			// XINPUT_GAMEPAD_Y
	};
	static const double s_batLevel[4] = {
		0.0,					// BATTERY_LEVEL_EMPTY
		0.33,					// BATTERY_LEVEL_LOW
		0.75,					// BATTERY_LEVEL_MEDIUM
		1.0,					// BATTERY_LEVEL_FULL
	};

	// check elapsed time from last poll
	LARGE_INTEGER	pcCur;	QueryPerformanceCounter(&pcCur);
	if(((pcCur.QuadPart-m_pcPoll.QuadPart)*s_pcMult) < QUERY_TIMEOUT) {
		// this device still up-to-date
		return;
	}

	// poll the device
	if(XInputGetState(devID, &m_state) == ERROR_SUCCESS) {
		// connected - parse the data
		const XINPUT_GAMEPAD& gp = m_state.Gamepad;
		// parse digital buttons
		for(int iButton=0; iButton<16; ++iButton) {
			int iChan;
			if((iChan=s_buttonChan[iButton]) >= 0) {
				m_data[iChan]		= (double)((gp.wButtons & (1<<iButton)) != 0);
			}
		}
		// parse triggers
		m_data[CHANNEL_LTRIG2]		= (double)gp.bLeftTrigger * (1.0f/255);
		m_data[CHANNEL_RTRIG2]		= (double)gp.bRightTrigger * (1.0f/255);
		// parse joysticks
		m_data[CHANNEL_JOYL_X]		= (double)(gp.sThumbLX+32768) * (2.0f/65535) - 1.0f;
		m_data[CHANNEL_JOYL_Y]		= (double)(gp.sThumbLY+32768) * (2.0f/65535) - 1.0f;
		m_data[CHANNEL_JOYR_X]		= (double)(gp.sThumbRX+32768) * (2.0f/65535) - 1.0f;
		m_data[CHANNEL_JOYR_Y]		= (double)(gp.sThumbRY+32768) * (2.0f/65535) - 1.0f;
		// mark as connected
		m_data[CHANNEL_CONNECTED]	= 1.0f;
#if (DO_BATTERY)
		// retrieve battery levels
		for(int iBat=0; iBat<2; ++iBat) {
			XINPUT_BATTERY_INFORMATION batInfo;
			double& batLevel = m_data[CHANNEL_BATTERY_PAD + iBat];
			if(
					(XInputGetBatteryInformation(devID, BATTERY_DEVTYPE_GAMEPAD+iBat, &batInfo) == ERROR_SUCCESS)
					&& (batInfo.BatteryType != BATTERY_TYPE_UNKNOWN)
			) {
				batLevel = s_batLevel[batInfo.BatteryLevel];
			} else {
				batLevel = 0.0;
			}
		}
#endif
	} else {
		// disconnected
		memset(m_data, 0, sizeof(m_data));
	}
	// mark timestamp
	m_pcPoll = pcCur;
}
