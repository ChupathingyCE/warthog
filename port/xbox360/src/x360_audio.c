/*
X360_AUDIO.C

The sound device the desktop mixer (port/linux/src/dsound_sdl.c) asks SDL
for, on the console's XAudio2: one 16-bit stereo source voice at the
mixer's 48 kHz. A thread of its own keeps a few buffers queued, asking the
mixer for each as SDL's device would (its callback), and the mixer's
floats arrive through SDL_PutAudioStreamData and become 16-bit samples
(the console's own byte order, which is what XAudio2 takes here).
*/

#include <xtl.h>
#include <xaudio2.h>
#include <string.h>

#include "x360_port.h"

#define RATE 48000
#define CHANNELS 2
/* 10 ms a buffer, three queued */
#define BUFFER_FRAMES 480
#define BUFFERS 4
#define QUEUED 3

/* SDL's callback: (user data, the stream, bytes wanted, bytes in all) */
typedef void (*audio_callback)(void *user_data, void *stream, int additional, int total);

static struct
{
	IXAudio2 *engine;
	IXAudio2MasteringVoice *master;
	IXAudio2SourceVoice *voice;
	audio_callback callback;
	void *user_data;
	short buffers[BUFFERS][BUFFER_FRAMES * CHANNELS];
	unsigned long filled;    /* samples in the buffer being filled */
	int current;
	BOOL running, dry;
	unsigned long underruns, underruns_logged, submitted;
	DWORD last_report;
} audio;

/* the stream SDL hands back (any non-NULL value): this file's state */
#define STREAM ((void *)&audio)

static DWORD WINAPI audio_thread(LPVOID parameter)
{
	(void)parameter;
	for (;;)
	{
		XAUDIO2_VOICE_STATE state;

		IXAudio2SourceVoice_GetState(audio.voice, &state, 0);
		/* (the voice ran dry: a gap the player hears) */
		if (state.BuffersQueued == 0 && audio.submitted && !audio.dry)
			audio.underruns++;
		audio.dry = state.BuffersQueued == 0;
		if (GetTickCount() - audio.last_report >= 10000)
		{
			audio.last_report = GetTickCount();
			if (audio.underruns != audio.underruns_logged)
			{
				x360_log("audio: %lu underruns so far (%lu buffers played)", audio.underruns, audio.submitted);
				audio.underruns_logged = audio.underruns;
			}
		}
		if (state.BuffersQueued < QUEUED)
		{
			/* (the mixer's floats for one buffer: it calls
			SDL_PutAudioStreamData, which submits it) */
			int wanted = BUFFER_FRAMES * CHANNELS * (int)sizeof(float);

			audio.callback(audio.user_data, STREAM, wanted, wanted);
			continue;
		}
		Sleep(2);
	}
	return 0;
}

void *SDL_OpenAudioDeviceStream(unsigned int device, const void *spec, void *callback, void *user_data)
{
	WAVEFORMATEX format;
	HRESULT result;

	(void)device;
	(void)spec;
	if (audio.voice)
		return STREAM;
	result = XAudio2Create(&audio.engine, 0, XAUDIO2_DEFAULT_PROCESSOR);
	if (FAILED(result))
	{
		x360_log("audio: XAudio2Create failed (0x%08lx)", (unsigned long)result);
		return NULL;
	}
	result = IXAudio2_CreateMasteringVoice(audio.engine, &audio.master, XAUDIO2_DEFAULT_CHANNELS,
		XAUDIO2_DEFAULT_SAMPLERATE, 0, 0, NULL);
	if (FAILED(result))
	{
		x360_log("audio: no mastering voice (0x%08lx)", (unsigned long)result);
		return NULL;
	}
	memset(&format, 0, sizeof(format));
	format.wFormatTag = WAVE_FORMAT_PCM;
	format.nChannels = CHANNELS;
	format.nSamplesPerSec = RATE;
	format.wBitsPerSample = 16;
	format.nBlockAlign = CHANNELS * 2;
	format.nAvgBytesPerSec = RATE * CHANNELS * 2;
	result = IXAudio2_CreateSourceVoice(audio.engine, &audio.voice, &format, 0, XAUDIO2_DEFAULT_FREQ_RATIO, NULL,
		NULL, NULL);
	if (FAILED(result))
	{
		x360_log("audio: no source voice (0x%08lx)", (unsigned long)result);
		return NULL;
	}
	audio.callback = (audio_callback)callback;
	audio.user_data = user_data;
	x360_log("audio: XAudio2, 16-bit stereo at %d Hz, %d ms buffers", RATE, BUFFER_FRAMES * 1000 / RATE);
	return STREAM;
}

_Bool SDL_ResumeAudioStreamDevice(void *stream)
{
	HANDLE thread;

	if (stream != STREAM || !audio.voice)
		return 0;
	if (audio.running)
		return 1;
	audio.running = TRUE;
	IXAudio2SourceVoice_Start(audio.voice, 0, XAUDIO2_COMMIT_NOW);
	thread = CreateThread(NULL, 0x40000, audio_thread, NULL, CREATE_SUSPENDED, NULL);
	if (!thread)
		return 0;
	/* (on a core of its own, away from the game's) */
	XSetThreadProcessor(thread, 4);
	SetThreadPriority(thread, THREAD_PRIORITY_ABOVE_NORMAL);
	ResumeThread(thread);
	CloseHandle(thread);
	return 1;
}

/* the mixer's floats: into the buffer being filled, submitted when full */
_Bool SDL_PutAudioStreamData(void *stream, const void *data, int length)
{
	const float *samples = data;
	int count = length / (int)sizeof(float), index;

	if (stream != STREAM || !audio.voice)
		return 0;
	for (index = 0; index < count; index++)
	{
		float value = samples[index] * 32767.0f;

		if (value > 32767.0f)
			value = 32767.0f;
		if (value < -32768.0f)
			value = -32768.0f;
		audio.buffers[audio.current][audio.filled++] = (short)value;
		if (audio.filled == BUFFER_FRAMES * CHANNELS)
		{
			XAUDIO2_BUFFER buffer;

			memset(&buffer, 0, sizeof(buffer));
			buffer.AudioBytes = sizeof(audio.buffers[0]);
			buffer.pAudioData = (const BYTE *)audio.buffers[audio.current];
			IXAudio2SourceVoice_SubmitSourceBuffer(audio.voice, &buffer, NULL);
			audio.submitted++;
			audio.current = (audio.current + 1) % BUFFERS;
			audio.filled = 0;
		}
	}
	return 1;
}
