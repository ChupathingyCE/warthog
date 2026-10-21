/*
XBOX_AUDIO_CAPTURE.C

The console's recording of its own output, for the audio probe
(port/linux/game/audio_probe.c, audio_probe.h): what the AC'97 controller
plays, the final mix of the audio hardware (the voice processor's mix bins
through the DSP's effects: the I3DL2 reverb, the crosstalk), sample for
sample as the console sends it to its DAC, 16-bit stereo at 48 kHz.

DirectSound has the DSP write its stereo output into a ring of system memory
that the AC'97 controller's PCM out channel plays, through a list of up to 32
buffer descriptors (each a physical address and a length in 16-bit samples).
The retail DirectSound fills only the first: one buffer of 4096 samples that
the controller plays again and again (the devkit, 2026-10-21), so the ring is
as many descriptors as are filled, from the first, and a look at the
controller that finds it back in the descriptor it was in has gone round.
The controller is the MCPX's ACI, PCI 0:6.0, whose bus master registers the
kernel puts at I/O 0xD200 (the SDK's xpcicfg.h); PCM out's are at 0x10 of
them: the list's physical address (BDBAR), the descriptor playing (CIV) and
the samples of it left (PICB). Titles run in ring 0: a thread reads those
registers every millisecond and copies what has been played since it last
looked, which the DSP will not write again until it has gone round the ring,
into a ring of its own; the game's thread writes that to the WAVE file
(audio_capture_update). Nothing is written to the controller, and nothing
runs until the probe starts it: with debug.audio_probe empty (the default)
none of this is used.

If the controller is not playing, or its descriptors do not look like a
ring of 16-bit stereo, the recording does not start and debug.txt says why.
*/

#include <xtl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void platform_log(const char *format, ...);
int snprintf(char *buffer, size_t size, const char *format, ...);

/* the kernel's (xboxkrnl), which the SDK's headers leave out */
__declspec(dllimport) PVOID __stdcall MmMapIoSpace(ULONG physical_address, ULONG size, ULONG protect);
__declspec(dllimport) VOID __stdcall MmUnmapIoSpace(PVOID base, ULONG size);

#define ACI_BUS_MASTER_PORT 0xD200
#define PCM_OUT_BDBAR 0x10
#define PCM_OUT_CIV 0x14
#define PCM_OUT_LVI 0x15
#define PCM_OUT_SR 0x16
#define PCM_OUT_PICB 0x18
#define PCM_OUT_CR 0x1B
#define PCM_OUT_CR_RUN 0x01
#define DESCRIPTORS 32

#define CAPTURE_RATE 48000
#define CAPTURE_CHANNELS 2
/* the copy's ring: 2.7 seconds, for the game's thread to fall behind by */
#define CAPTURE_RING_FRAMES 131072
/* the most of the output ring mapped (it is a few KB) */
#define MAXIMUM_SPAN 0x100000

struct descriptor
{
	unsigned long address;
	unsigned short samples;
	unsigned short flags;
};

static struct
{
	volatile int on;
	volatile int stop;
	HANDLE thread;
	FILE *file;
	unsigned long file_frames;

	/* the controller's descriptors, and its buffers' span, mapped */
	volatile struct descriptor *descriptors;
	unsigned long span_start, span_size;
	unsigned char *span;
	/* the frames of a whole turn of the ring, and its descriptors */
	unsigned long ring_frames;
	unsigned long count;

	/* where the copy has reached: a descriptor and the samples into it */
	unsigned long index, offset;

	/* the copy's ring; the capture thread puts, the game's thread takes */
	short *ring;
	volatile unsigned long put, taken;
	/* frames played since the start (put, and any lost) */
	volatile unsigned long frames;
	volatile unsigned long lost, late, outside;
} capture;

static unsigned char in8(unsigned short port)
{
	unsigned char value;

	__asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
	return value;
}

static unsigned short in16(unsigned short port)
{
	unsigned short value;

	__asm__ volatile ("inw %1, %0" : "=a"(value) : "Nd"(port));
	return value;
}

static unsigned long in32(unsigned short port)
{
	unsigned long value;

	__asm__ volatile ("inl %1, %0" : "=a"(value) : "Nd"(port));
	return value;
}

/* the descriptor playing and the samples of it played, read until the two
agree (the controller may move on between the reads) */
static void playing(unsigned long *index, unsigned long *offset)
{
	unsigned char before, after;
	unsigned short left;

	do
	{
		before = in8(ACI_BUS_MASTER_PORT + PCM_OUT_CIV);
		left = in16(ACI_BUS_MASTER_PORT + PCM_OUT_PICB);
		after = in8(ACI_BUS_MASTER_PORT + PCM_OUT_CIV);
	}
	while (before != after);
	*index = before % capture.count;
	{
		unsigned long samples = capture.descriptors[*index].samples;

		*offset = left <= samples ? samples - left : 0;
	}
}

/* samples of descriptor index from first to last into the copy's ring */
static void copy(unsigned long index, unsigned long first, unsigned long last)
{
	volatile struct descriptor *entry = &capture.descriptors[index];
	unsigned long address = entry->address + first * 2;
	unsigned long frames = (last - first) / CAPTURE_CHANNELS;
	const short *source;

	if (last <= first)
		return;
	if (address < capture.span_start || address + frames * 4 > capture.span_start + capture.span_size)
	{
		capture.outside++;
		return;
	}
	source = (const short *)(capture.span + (address - capture.span_start));
	if (capture.put - capture.taken + frames > CAPTURE_RING_FRAMES)
	{
		capture.lost += frames;
	}
	else
	{
		unsigned long frame;

		for (frame = 0; frame < frames; frame++)
		{
			unsigned long slot = (capture.put + frame) % CAPTURE_RING_FRAMES;

			capture.ring[slot * 2] = source[frame * 2];
			capture.ring[slot * 2 + 1] = source[frame * 2 + 1];
		}
		/* (the frames written before they are counted put) */
		__asm__ volatile ("" ::: "memory");
		capture.put += frames;
	}
	capture.frames += frames;
}

static DWORD WINAPI capture_thread(LPVOID parameter)
{
	LARGE_INTEGER frequency, last, now;

	(void)parameter;
	QueryPerformanceFrequency(&frequency);
	QueryPerformanceCounter(&last);
	while (!capture.stop)
	{
		unsigned long index, offset;

		Sleep(1);
		QueryPerformanceCounter(&now);
		/* (a wait longer than most of a turn of the ring: what was played
		may have been written over) */
		if ((double)(now.QuadPart - last.QuadPart) / (double)frequency.QuadPart * CAPTURE_RATE >
			capture.ring_frames * 3 / 4)
		{
			capture.late++;
		}
		last = now;
		playing(&index, &offset);
		/* (back in the descriptor it was in, behind where it was: round the
		ring, as a ring of one descriptor always is) */
		if (capture.index == index && offset < capture.offset)
		{
			copy(capture.index, capture.offset, capture.descriptors[capture.index].samples);
			capture.index = (capture.index + 1) % capture.count;
			capture.offset = 0;
		}
		while (capture.index != index)
		{
			copy(capture.index, capture.offset, capture.descriptors[capture.index].samples);
			capture.index = (capture.index + 1) % capture.count;
			capture.offset = 0;
		}
		if (offset > capture.offset)
		{
			copy(index, capture.offset, offset);
			capture.offset = offset;
		}
	}
	return 0;
}

static void write_u32(FILE *file, unsigned long value)
{
	unsigned char bytes[4];

	bytes[0] = (unsigned char)value;
	bytes[1] = (unsigned char)(value >> 8);
	bytes[2] = (unsigned char)(value >> 16);
	bytes[3] = (unsigned char)(value >> 24);
	fwrite(bytes, 1, 4, file);
}

static void write_u16(FILE *file, unsigned int value)
{
	unsigned char bytes[2];

	bytes[0] = (unsigned char)value;
	bytes[1] = (unsigned char)(value >> 8);
	fwrite(bytes, 1, 2, file);
}

/* a WAVE header of 16-bit stereo frames */
static void write_header(FILE *file, unsigned long frames)
{
	unsigned long data_bytes = frames * CAPTURE_CHANNELS * 2;

	fwrite("RIFF", 1, 4, file);
	write_u32(file, 4 + 8 + 16 + 8 + data_bytes);
	fwrite("WAVEfmt ", 1, 8, file);
	write_u32(file, 16);
	write_u16(file, 1);
	write_u16(file, CAPTURE_CHANNELS);
	write_u32(file, CAPTURE_RATE);
	write_u32(file, CAPTURE_RATE * CAPTURE_CHANNELS * 2);
	write_u16(file, CAPTURE_CHANNELS * 2);
	write_u16(file, 16);
	fwrite("data", 1, 4, file);
	write_u32(file, data_bytes);
}

/* the controller's descriptors read and checked, and the span of memory
they play mapped */
static int map_output(void)
{
	unsigned long list = in32(ACI_BUS_MASTER_PORT + PCM_OUT_BDBAR);
	unsigned char control = in8(ACI_BUS_MASTER_PORT + PCM_OUT_CR);
	unsigned long low = 0xFFFFFFFFUL, high = 0, samples = 0, index;

	platform_log("audio capture: PCM out list %08lx, control %02x, status %04x, playing %u, last %u", list,
		control, in16(ACI_BUS_MASTER_PORT + PCM_OUT_SR), in8(ACI_BUS_MASTER_PORT + PCM_OUT_CIV),
		in8(ACI_BUS_MASTER_PORT + PCM_OUT_LVI));
	if (!(control & PCM_OUT_CR_RUN) || !list || (list & 7))
	{
		platform_log("audio capture: the AC'97 PCM out channel is not playing; nothing is recorded");
		return 0;
	}
	capture.descriptors = (volatile struct descriptor *)MmMapIoSpace(list, DESCRIPTORS * sizeof(struct descriptor),
		PAGE_READWRITE | PAGE_NOCACHE);
	if (!capture.descriptors)
	{
		platform_log("audio capture: cannot map the descriptor list");
		return 0;
	}
	for (index = 0; index < DESCRIPTORS; index++)
	{
		volatile struct descriptor *entry = &capture.descriptors[index];

		if (index < 4 || index == DESCRIPTORS - 1)
			platform_log("audio capture: descriptor %lu: %08lx, %u samples, flags %04x", index, entry->address,
				entry->samples, entry->flags);
		if (!entry->address || !entry->samples || (entry->samples & 1))
		{
			if (index)
				break;
			platform_log("audio capture: descriptor 0 is not a buffer of stereo samples; nothing is recorded");
			return 0;
		}
		if (entry->address < low)
			low = entry->address;
		if (entry->address + entry->samples * 2UL > high)
			high = entry->address + entry->samples * 2UL;
		samples += entry->samples;
	}
	capture.count = index;
	platform_log("audio capture: %lu of %d descriptors filled", capture.count, DESCRIPTORS);
	/* (a ring is one buffer cut in turn: each descriptor's buffer where the
	last's ends, if its lengths are in 16-bit samples) */
	for (index = 1; index < capture.count; index++)
	{
		volatile struct descriptor *previous = &capture.descriptors[index - 1];

		if (capture.descriptors[index].address != previous->address + previous->samples * 2UL)
			break;
	}
	platform_log("audio capture: %lu of %lu descriptors follow each other in memory", index, capture.count);
	if (high - low > MAXIMUM_SPAN)
	{
		platform_log("audio capture: the buffers span %lu bytes, more than expected; nothing is recorded", high - low);
		return 0;
	}
	capture.span_start = low;
	capture.span_size = high - low;
	capture.span = (unsigned char *)MmMapIoSpace(low, capture.span_size, PAGE_READWRITE | PAGE_NOCACHE);
	if (!capture.span)
	{
		platform_log("audio capture: cannot map the buffers");
		return 0;
	}
	capture.ring_frames = samples / CAPTURE_CHANNELS;
	platform_log("audio capture: buffers %08lx to %08lx, a turn of %lu frames (%lu ms)", low, high,
		capture.ring_frames, capture.ring_frames * 1000 / CAPTURE_RATE);
	return 1;
}

static void unmap_output(void)
{
	if (capture.span)
		MmUnmapIoSpace(capture.span, capture.span_size);
	if (capture.descriptors)
		MmUnmapIoSpace((PVOID)capture.descriptors, DESCRIPTORS * sizeof(struct descriptor));
	capture.span = NULL;
	capture.descriptors = NULL;
}

int audio_capture_start(char const *path_prefix)
{
	char path[300];

	if (capture.on)
		return 0;
	memset(&capture, 0, sizeof(capture));
	if (!map_output())
	{
		unmap_output();
		return 0;
	}
	capture.ring = (short *)malloc(CAPTURE_RING_FRAMES * CAPTURE_CHANNELS * sizeof(short));
	snprintf(path, sizeof(path), "%s-final.wav", path_prefix);
	capture.file = capture.ring ? fopen(path, "wb") : NULL;
	if (!capture.file)
	{
		platform_log("audio capture: cannot %s", capture.ring ? "write the recording" : "allocate its ring");
		free(capture.ring);
		capture.ring = NULL;
		unmap_output();
		return 0;
	}
	write_header(capture.file, 0);
	playing(&capture.index, &capture.offset);
	capture.thread = CreateThread(NULL, 16384, capture_thread, NULL, 0, NULL);
	if (!capture.thread)
	{
		platform_log("audio capture: cannot start its thread");
		fclose(capture.file);
		free(capture.ring);
		capture.ring = NULL;
		unmap_output();
		return 0;
	}
	SetThreadPriority(capture.thread, THREAD_PRIORITY_TIME_CRITICAL);
	capture.on = 1;
	platform_log("audio capture: recording %s", path);
	return 1;
}

unsigned long audio_capture_frames(void)
{
	return capture.frames;
}

unsigned long audio_capture_rate(void)
{
	return CAPTURE_RATE;
}

char const *audio_capture_platform(void)
{
	return "xbox";
}

void audio_capture_update(void)
{
	unsigned long put = capture.put;

	if (!capture.file)
		return;
	while (capture.taken != put)
	{
		unsigned long slot = capture.taken % CAPTURE_RING_FRAMES;
		unsigned long count = put - capture.taken;

		if (count > CAPTURE_RING_FRAMES - slot)
			count = CAPTURE_RING_FRAMES - slot;
		fwrite(capture.ring + slot * CAPTURE_CHANNELS, CAPTURE_CHANNELS * sizeof(short), count, capture.file);
		capture.file_frames += count;
		__asm__ volatile ("" ::: "memory");
		capture.taken += count;
	}
}

void audio_capture_stop(void)
{
	if (!capture.on)
		return;
	capture.stop = 1;
	WaitForSingleObject(capture.thread, 1000);
	CloseHandle(capture.thread);
	capture.on = 0;
	audio_capture_update();
	fseek(capture.file, 0, SEEK_SET);
	write_header(capture.file, capture.file_frames);
	fclose(capture.file);
	capture.file = NULL;
	platform_log("audio capture: recorded %lu frames; lost %lu (its ring full), %lu outside the buffers, "
		"%lu late looks at the controller", capture.file_frames, capture.lost, capture.outside, capture.late);
	free(capture.ring);
	capture.ring = NULL;
	unmap_output();
}
