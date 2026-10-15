/*
XBOX360_INPUT.C

The Xbox 360's controllers for the game, in place of the desktop builds'
xinput_sdl.c: the original Xbox's XInput (XGetDeviceChanges, XInputOpen,
XInputGetState) over the console's (x360_input_pad.c). A controller is
polled every time the game asks, so one plugged in (or turned on) late is
found. There is no keyboard or mouse; the debug keyboard is there but never
types, as in the dedicated server (server/platform/server_input.c).
*/

#include "platform.h"
#include "sdl_platform.h"

#include <stdio.h>
#include <string.h>

#include "x360_port.h"

#define PORT_COUNT 4

XPP_DEVICE_TYPE XDEVICE_TYPE_GAMEPAD_TABLE;
XPP_DEVICE_TYPE XDEVICE_TYPE_MEMORY_UNIT_TABLE;
XPP_DEVICE_TYPE XDEVICE_TYPE_DEBUG_KEYBOARD_TABLE;

struct controller
{
	BOOL open;
};

static struct controller controllers[PORT_COUNT];
static struct controller keyboard_device;
static DWORD reported_gamepads;
static BOOL reported_keyboard;

/* ---------- the keyboard and mouse's (none) */

int halo_linux_mouse_look(short gamepad_index, float *yaw, float *pitch)
{
	(void)gamepad_index;
	*yaw = 0.0f;
	*pitch = 0.0f;
	return FALSE;
}

int halo_linux_mouse_aiming(short gamepad_index)
{
	(void)gamepad_index;
	return FALSE;
}

int platform_input_scheme(void)
{
	return 0;
}

void platform_text_typing(int typing)
{
	(void)typing;
}

void platform_text_field(int typing)
{
	(void)typing;
}

int halo_input_from_name(const char *name)
{
	(void)name;
	return -1;
}

void halo_input_name(int input, char *name, size_t size)
{
	(void)input;
	if (size)
		name[0] = 0;
}

/* a binding's name as the menus show it: no keyboard layouts to ask */
void halo_input_shown_name(const char *binding, char *shown, size_t size)
{
	if (size)
		snprintf(shown, size, "%s", binding);
}

unsigned long halo_keyboard_actions(short controller_index)
{
	(void)controller_index;
	return 0;
}

void test_input_hold_action(int hold)
{
	(void)hold;
}

/* ---------- XAPI */

VOID WINAPI XInitDevices(DWORD preallocation_type_count, PXDEVICE_PREALLOC_TYPE preallocation_types)
{
	(void)preallocation_type_count;
	(void)preallocation_types;
	platform_sdl_initialize();
}

/* the ports with a controller now */
static DWORD connected_gamepads(void)
{
	struct x360_pad pad;
	DWORD mask = 0;
	int port;

	for (port = 0; port < PORT_COUNT; port++)
	{
		if (x360_pad_read(port, &pad))
			mask |= 1UL << port;
	}
	return mask;
}

BOOL WINAPI XGetDeviceChanges(PXPP_DEVICE_TYPE device_type, PDWORD insertions, PDWORD removals)
{
	*insertions = 0;
	*removals = 0;
	if (device_type == XDEVICE_TYPE_GAMEPAD)
	{
		DWORD now = connected_gamepads();

		*insertions = now & ~reported_gamepads;
		*removals = reported_gamepads & ~now;
		reported_gamepads = now;
	}
	else if (device_type == XDEVICE_TYPE_DEBUG_KEYBOARD && !reported_keyboard)
	{
		*insertions = 1;
		reported_keyboard = TRUE;
	}
	return *insertions || *removals;
}

HANDLE WINAPI XInputOpen(PXPP_DEVICE_TYPE device_type, DWORD port, DWORD slot,
	PXINPUT_POLLING_PARAMETERS polling_parameters)
{
	(void)slot;
	(void)polling_parameters;
	if (device_type == XDEVICE_TYPE_GAMEPAD && port < PORT_COUNT)
	{
		controllers[port].open = TRUE;
		return (HANDLE)&controllers[port];
	}
	if (device_type == XDEVICE_TYPE_DEBUG_KEYBOARD && port == 0)
	{
		keyboard_device.open = TRUE;
		return (HANDLE)&keyboard_device;
	}
	SetLastError(ERROR_DEVICE_NOT_CONNECTED);
	return NULL;
}

VOID WINAPI XInputClose(HANDLE device)
{
	struct controller *controller = (struct controller *)device;

	if (controller)
		controller->open = FALSE;
}

DWORD WINAPI XInputGetState(HANDLE device, PXINPUT_STATE state)
{
	static DWORD packet;
	struct x360_pad pad;
	int port;

	memset(state, 0, sizeof(*state));
	platform_pump_events();
	for (port = 0; port < PORT_COUNT; port++)
	{
		if (device == (HANDLE)&controllers[port] && controllers[port].open)
			break;
	}
	if (port == PORT_COUNT || !x360_pad_read(port, &pad))
		return ERROR_DEVICE_NOT_CONNECTED;
	state->dwPacketNumber = ++packet;
	state->Gamepad.wButtons = pad.buttons;
	memcpy(state->Gamepad.bAnalogButtons, pad.analog, sizeof(state->Gamepad.bAnalogButtons));
	state->Gamepad.sThumbLX = pad.thumb_lx;
	state->Gamepad.sThumbLY = pad.thumb_ly;
	state->Gamepad.sThumbRX = pad.thumb_rx;
	state->Gamepad.sThumbRY = pad.thumb_ry;
	return ERROR_SUCCESS;
}

DWORD WINAPI XInputSetState(HANDLE device, PXINPUT_FEEDBACK feedback)
{
	int port;

	if (!feedback)
		return ERROR_INVALID_PARAMETER;
	for (port = 0; port < PORT_COUNT; port++)
	{
		if (device == (HANDLE)&controllers[port])
			x360_pad_rumble(port, feedback->Rumble.wLeftMotorSpeed, feedback->Rumble.wRightMotorSpeed);
	}
	feedback->Header.dwStatus = ERROR_SUCCESS;
	return ERROR_SUCCESS;
}

DWORD WINAPI XInputDebugInitKeyboardQueue(PXINPUT_DEBUG_KEYQUEUE_PARAMETERS parameters)
{
	(void)parameters;
	return ERROR_SUCCESS;
}

DWORD WINAPI XInputDebugGetKeystroke(PXINPUT_DEBUG_KEYSTROKE keystroke)
{
	memset(keystroke, 0, sizeof(*keystroke));
	return ERROR_HANDLE_EOF;
}
