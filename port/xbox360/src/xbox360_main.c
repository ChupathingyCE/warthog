/*
XBOX360_MAIN.C

Phase 0 of the Xbox 360 port (port/xbox360/README.md): boots, writes
game:\debug.txt, reports the memory and platform facts the port is planned
on, clears the screen, and returns to the launcher. None of the game is in
this XEX.

Written for OXDK360's clang with the Xenon calling convention. Two of its
known gaps shape the code: a 64-bit value returned by an XDK function comes
back wrong (none is called here), and variadic doubles print garbage (no %f
anywhere; sizes are printed as integers).
*/

#include <xtl.h>
#include <winsockx.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xbox360_build_stamp.h"

/* ---------- constants */

#define LOG_PATH "game:\\debug.txt"
#define KILOBYTE 1024UL
#define MEGABYTE (1024UL * 1024UL)
/* the original Xbox's memory, which the game's addresses and pools are laid out for */
#define GAME_WINDOW_SIZE (128UL * MEGABYTE)
#define WAIT_MILLISECONDS 10000UL
#define NETWORK_WAIT_MILLISECONDS 8000UL
#define NETWORK_POLL_MILLISECONDS 250UL
/* the screen's color: green once everything has been reported */
#define CLEAR_COLOR_RUNNING 0xFF404040UL
#define CLEAR_COLOR_DONE 0xFF107C10UL

/* xam export (xapilib links it); not declared by the public headers */
DWORD XamGetSystemVersion(void);

/* ---------- globals */

static HANDLE log_file = INVALID_HANDLE_VALUE;
static D3DDevice *device = NULL;

/* ---------- log */

/* each line to game:\debug.txt, flushed, and to the debug channel: a crash
keeps everything before it */
static void log_line(const char *format, ...)
{
	char text[512];
	va_list arguments;
	DWORD written;
	size_t length;

	va_start(arguments, format);
	_vsnprintf(text, sizeof(text) - 3, format, arguments);
	va_end(arguments);
	text[sizeof(text) - 3] = '\0';
	length = strlen(text);
	text[length++] = '\r';
	text[length++] = '\n';
	text[length] = '\0';

	OutputDebugStringA(text);
	if (log_file != INVALID_HANDLE_VALUE)
	{
		WriteFile(log_file, text, (DWORD)length, &written, NULL);
		FlushFileBuffers(log_file);
	}
}

static void log_open(void)
{
	log_file = CreateFileA(LOG_PATH, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL, NULL);
}

/* ---------- memory */

static unsigned long megabytes(SIZE_T bytes)
{
	return (unsigned long)(bytes / MEGABYTE);
}

static unsigned long kilobytes(SIZE_T bytes)
{
	return (unsigned long)(bytes / KILOBYTE);
}

static void *heap_allocate(SIZE_T size)
{
	return malloc(size);
}

static void heap_release(void *block)
{
	free(block);
}

static void *physical_allocate(SIZE_T size)
{
	return XPhysicalAlloc(size, MAXULONG_PTR, 0, PAGE_READWRITE);
}

static void physical_release(void *block)
{
	XPhysicalFree(block);
}

/* the largest block that succeeds, to 1 MB: halve down from 512 MB until one
does, then search back up between it and its double */
static SIZE_T largest_allocation(void *(*allocate)(SIZE_T), void (*release)(void *))
{
	SIZE_T size = 512UL * MEGABYTE;
	SIZE_T step;
	void *block;

	for (;;)
	{
		block = allocate(size);
		if (block)
		{
			release(block);
			break;
		}
		if (size <= MEGABYTE)
		{
			return 0;
		}
		size /= 2;
	}
	for (step = size / 2; step >= MEGABYTE; step /= 2)
	{
		block = allocate(size + step);
		if (block)
		{
			release(block);
			size += step;
		}
	}
	return size;
}

static void report_game_window(const char *name, DWORD protection)
{
	void *block = XPhysicalAlloc(GAME_WINDOW_SIZE, MAXULONG_PTR, 0, protection);

	if (block)
	{
		log_line("memory: 128 MB contiguous (%s): yes, at 0x%08lX", name, (unsigned long)(ULONG_PTR)block);
		XPhysicalFree(block);
	}
	else
	{
		log_line("memory: 128 MB contiguous (%s): no", name);
	}
}

static void report_memory(void)
{
	MEMORYSTATUS status;
	SIZE_T largest;

	memset(&status, 0, sizeof(status));
	status.dwLength = sizeof(status);
	GlobalMemoryStatus(&status);
	log_line("memory: physical total %lu MB (%lu KB), free %lu MB (%lu KB)",
		megabytes(status.dwTotalPhys), kilobytes(status.dwTotalPhys),
		megabytes(status.dwAvailPhys), kilobytes(status.dwAvailPhys));
	log_line("memory: virtual total %lu MB, free %lu MB",
		megabytes(status.dwTotalVirtual), megabytes(status.dwAvailVirtual));

	largest = largest_allocation(heap_allocate, heap_release);
	log_line("memory: largest malloc %lu MB", megabytes(largest));
	largest = largest_allocation(physical_allocate, physical_release);
	log_line("memory: largest XPhysicalAlloc %lu MB", megabytes(largest));

	report_game_window("4 KB pages", PAGE_READWRITE);
	report_game_window("64 KB pages", PAGE_READWRITE | MEM_LARGE_PAGES);
	report_game_window("16 MB pages", PAGE_READWRITE | MEM_16MB_PAGES);

	GlobalMemoryStatus(&status);
	log_line("memory: free after the probes %lu MB", megabytes(status.dwAvailPhys));
}

/* ---------- platform */

static const char *yes_no(BOOL value)
{
	return value ? "yes" : "no";
}

static void report_path(const char *path)
{
	DWORD attributes = GetFileAttributesA(path);

	log_line("storage: %s %s", path, attributes == 0xFFFFFFFFUL ? "not found" : "found");
}

static void report_platform(void)
{
	XVIDEO_MODE mode;
	DWORD version = XamGetSystemVersion();

	log_line("system: version %lu.%lu.%lu.%lu (0x%08lX)",
		(unsigned long)(version >> 28), (unsigned long)((version >> 24) & 0xF),
		(unsigned long)((version >> 8) & 0xFFFF), (unsigned long)(version & 0xFF),
		(unsigned long)version);

	memset(&mode, 0, sizeof(mode));
	XGetVideoMode(&mode);
	log_line("video: %lux%lu, widescreen %s, interlaced %s, HD %s, standard %lu, AV pack %lu",
		(unsigned long)mode.dwDisplayWidth, (unsigned long)mode.dwDisplayHeight,
		yes_no(mode.fIsWideScreen), yes_no(mode.fIsInterlaced), yes_no(mode.fIsHiDef),
		(unsigned long)mode.VideoStandard, (unsigned long)XGetAVPack());

	log_line("storage: log written to %s: %s", LOG_PATH, yes_no(log_file != INVALID_HANDLE_VALUE));
	report_path("game:\\default.xex");
	report_path("game:\\");
	report_path("Hdd1:\\");
	report_path("Hdd:\\");
	report_path("Usb0:\\");
}

/* ---------- network */

/* the kind of address the console has, never the address (the project's
rule: no player addresses in logs) */
static const char *address_class(unsigned long address)
{
	unsigned long first = address >> 24;
	unsigned long second = (address >> 16) & 0xFF;

	if (first == 10)
	{
		return "private (10/8)";
	}
	if (first == 172 && second >= 16 && second <= 31)
	{
		return "private (172.16/12)";
	}
	if (first == 192 && second == 168)
	{
		return "private (192.168/16)";
	}
	if (first == 169 && second == 254)
	{
		return "link-local (169.254/16)";
	}
	return "public";
}

static void report_network(void)
{
	XNetStartupParams parameters;
	WSADATA winsock;
	XNADDR address;
	DWORD link;
	DWORD status = XNET_GET_XNADDR_PENDING;
	DWORD waited;
	int result;

	memset(&parameters, 0, sizeof(parameters));
	parameters.cfgSizeOfStruct = sizeof(parameters);
	parameters.cfgFlags = XNET_STARTUP_BYPASS_SECURITY;
	result = XNetStartup(&parameters);
	log_line("network: XNetStartup %d (parameters %lu bytes)", result, (unsigned long)sizeof(parameters));
	if (result != 0)
	{
		return;
	}
	result = WSAStartup(MAKEWORD(2, 2), &winsock);
	log_line("network: WSAStartup %d", result);

	link = XNetGetEthernetLinkStatus();
	log_line("network: link %s%s%s",
		(link & XNET_ETHERNET_LINK_ACTIVE) ? "up" : "down",
		(link & XNET_ETHERNET_LINK_WIRELESS) ? ", wireless" : "",
		(link & XNET_ETHERNET_LINK_100MBPS) ? ", 100 Mbps" : "");

	/* the title's address is PENDING until the interface has one */
	for (waited = 0; waited < NETWORK_WAIT_MILLISECONDS; waited += NETWORK_POLL_MILLISECONDS)
	{
		memset(&address, 0, sizeof(address));
		status = XNetGetTitleXnAddr(&address);
		if (status != XNET_GET_XNADDR_PENDING)
		{
			break;
		}
		Sleep(NETWORK_POLL_MILLISECONDS);
	}
	if (status == XNET_GET_XNADDR_PENDING || status == XNET_GET_XNADDR_NONE || address.ina.s_addr == 0)
	{
		log_line("network: has an address: no (status 0x%08lX, %lu ms)", (unsigned long)status, (unsigned long)waited);
	}
	else
	{
		log_line("network: has an address: yes, %s (status 0x%08lX, %lu ms)",
			address_class((unsigned long)ntohl(address.ina.s_addr)), (unsigned long)status, (unsigned long)waited);
	}
}

/* ---------- controllers */

static DWORD controllers_connected(void)
{
	DWORD mask = 0;
	DWORD port;
	XINPUT_STATE state;

	for (port = 0; port < XUSER_MAX_COUNT; port++)
	{
		memset(&state, 0, sizeof(state));
		if (XInputGetState(port, &state) == ERROR_SUCCESS)
		{
			mask |= 1UL << port;
		}
	}
	return mask;
}

static BOOL button_pressed(void)
{
	DWORD port;
	XINPUT_STATE state;

	for (port = 0; port < XUSER_MAX_COUNT; port++)
	{
		memset(&state, 0, sizeof(state));
		if (XInputGetState(port, &state) == ERROR_SUCCESS && state.Gamepad.wButtons != 0)
		{
			return TRUE;
		}
	}
	return FALSE;
}

static void report_controllers(void)
{
	DWORD mask = controllers_connected();

	log_line("controllers: 1 %s, 2 %s, 3 %s, 4 %s",
		yes_no((mask & 1) != 0), yes_no((mask & 2) != 0), yes_no((mask & 4) != 0), yes_no((mask & 8) != 0));
}

/* ---------- screen */

/* the ATG framework's defaults, which every display locks onto (as
OXDK360's d3dclear sample) */
static void screen_initialize(void)
{
	D3DPRESENT_PARAMETERS parameters;
	HRESULT result;

	memset(&parameters, 0, sizeof(parameters));
	parameters.BackBufferWidth = 640;
	parameters.BackBufferHeight = 480;
	parameters.BackBufferFormat = D3DFMT_A8R8G8B8;
	parameters.BackBufferCount = 1;
	parameters.MultiSampleType = D3DMULTISAMPLE_NONE;
	parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
	parameters.EnableAutoDepthStencil = TRUE;
	parameters.AutoDepthStencilFormat = D3DFMT_D24S8;
	parameters.PresentationInterval = D3DPRESENT_INTERVAL_ONE;

	result = Direct3D_CreateDevice(0, D3DDEVTYPE_HAL, NULL, D3DCREATE_HARDWARE_VERTEXPROCESSING, &parameters, &device);
	log_line("screen: Direct3D_CreateDevice 0x%08lX", (unsigned long)result);
	if (FAILED(result))
	{
		device = NULL;
	}
}

static void screen_clear(D3DCOLOR color)
{
	if (device)
	{
		D3DDevice_Clear(device, 0, NULL, D3DCLEAR_TARGET, color, 1.0f, 0, FALSE);
		D3DDevice_Present(device);
	}
}

/* ---------- main */

int main(void)
{
	DWORD start;
	BOOL pressed = FALSE;

	log_open();
	log_line("Warthog %s (ChupathingyCE), commit %s, built %s, Xbox 360",
		XBOX360_BUILD_VERSION, XBOX360_BUILD_COMMIT, XBOX360_BUILD_TIME);
	log_line("phase 0: memory and platform report");

	screen_initialize();
	screen_clear(CLEAR_COLOR_RUNNING);

	report_platform();
	report_memory();
	report_controllers();
	report_network();

	log_line("done: press any button or wait 10 seconds");
	start = GetTickCount();
	while (GetTickCount() - start < WAIT_MILLISECONDS)
	{
		screen_clear(CLEAR_COLOR_DONE);
		if (button_pressed())
		{
			pressed = TRUE;
			break;
		}
		if (!device)
		{
			Sleep(16);
		}
	}
	log_line("exit: %s; returning to the launcher", pressed ? "button" : "timeout");

	if (log_file != INVALID_HANDLE_VALUE)
	{
		CloseHandle(log_file);
		log_file = INVALID_HANDLE_VALUE;
	}
	/* left as it is: tearing Direct3D down mid-present can hang the console */
	XLaunchNewImage("", 0);
	return 0;
}
