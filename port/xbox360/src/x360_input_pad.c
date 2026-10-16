/*
X360_INPUT_PAD.C

The console's controllers (XInput) as the original Xbox's gamepad
(xbox360_input.c gives them to the game): the digital buttons keep their
bits (the D-pad, Start, Back and the sticks' clicks are the same on both),
A, B, X and Y become the Xbox's analog buttons, the shoulder buttons the
white (left) and black (right) buttons, as the desktop builds map them
(port/linux/src/xinput_sdl.c).
*/

#include <xtl.h>
#include <stdio.h>
#include <string.h>

#include "x360_port.h"

/* the original Xbox's analog buttons */
enum
{
	XBOX_A,
	XBOX_B,
	XBOX_X,
	XBOX_Y,
	XBOX_BLACK,
	XBOX_WHITE,
	XBOX_LEFT_TRIGGER,
	XBOX_RIGHT_TRIGGER,
};

static DWORD reported_ports;

/* bring-up: "fake_pad" in game:\gl.txt keeps controller 1 connected (at
rest) while it is not, so an automated run is never paused for it */
static int fake_pad(void)
{
	static int loaded = -1;

	if (loaded < 0)
	{
		FILE *file = fopen("game:\\gl.txt", "rb");
		char word[64];

		loaded = 0;
		if (file)
		{
			while (fscanf(file, "%63s", word) == 1)
			{
				if (!strcmp(word, "fake_pad"))
					loaded = 1;
			}
			fclose(file);
		}
	}
	return loaded;
}
static WORD last_buttons[XUSER_MAX_COUNT];
static int button_logs;

int x360_pad_read(int port, struct x360_pad *pad)
{
	XINPUT_STATE state;
	WORD buttons;
	DWORD result;

	memset(pad, 0, sizeof(*pad));
	if (port < 0 || port >= XUSER_MAX_COUNT)
		return 0;
	memset(&state, 0, sizeof(state));
	result = XInputGetState((DWORD)port, &state);
	if (result != ERROR_SUCCESS)
	{
		if (reported_ports & (1UL << port))
		{
			reported_ports &= ~(1UL << port);
			x360_log("controller %d: disconnected", port + 1);
		}
		return port == 0 && fake_pad();
	}
	if (!(reported_ports & (1UL << port)))
	{
		reported_ports |= 1UL << port;
		x360_log("controller %d: connected", port + 1);
	}
	buttons = state.Gamepad.wButtons;
	/* (the first presses, in the log: input's bring-up) */
	if (buttons != last_buttons[port] && button_logs < 200)
	{
		button_logs++;
		x360_log("controller %d: buttons 0x%04x, triggers %d %d, left stick %d %d", port + 1, buttons,
			state.Gamepad.bLeftTrigger, state.Gamepad.bRightTrigger, state.Gamepad.sThumbLX, state.Gamepad.sThumbLY);
	}
	last_buttons[port] = buttons;
	/* D-pad, Start, Back, the sticks' clicks: the same bits */
	pad->buttons = buttons & 0x00ff;
	pad->analog[XBOX_A] = (buttons & XINPUT_GAMEPAD_A) ? 255 : 0;
	pad->analog[XBOX_B] = (buttons & XINPUT_GAMEPAD_B) ? 255 : 0;
	pad->analog[XBOX_X] = (buttons & XINPUT_GAMEPAD_X) ? 255 : 0;
	pad->analog[XBOX_Y] = (buttons & XINPUT_GAMEPAD_Y) ? 255 : 0;
	pad->analog[XBOX_WHITE] = (buttons & XINPUT_GAMEPAD_LEFT_SHOULDER) ? 255 : 0;
	pad->analog[XBOX_BLACK] = (buttons & XINPUT_GAMEPAD_RIGHT_SHOULDER) ? 255 : 0;
	pad->analog[XBOX_LEFT_TRIGGER] = state.Gamepad.bLeftTrigger;
	pad->analog[XBOX_RIGHT_TRIGGER] = state.Gamepad.bRightTrigger;
	pad->thumb_lx = state.Gamepad.sThumbLX;
	pad->thumb_ly = state.Gamepad.sThumbLY;
	pad->thumb_rx = state.Gamepad.sThumbRX;
	pad->thumb_ry = state.Gamepad.sThumbRY;
	return 1;
}

void x360_pad_rumble(int port, unsigned short left, unsigned short right)
{
	XINPUT_VIBRATION vibration;

	if (port < 0 || port >= XUSER_MAX_COUNT)
		return;
	vibration.wLeftMotorSpeed = left;
	vibration.wRightMotorSpeed = right;
	XInputSetState((DWORD)port, &vibration);
}
