/*
PLAYER_KEY_TEST.C

The console's player identity (port/xbox/src/xbox_player_key.c): a key file
as the desktop builds keep it (32 raw bytes) or as hex text, the ID worked
out as browser.c works it out, and everything else refused.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/xbox_player_key.h"

static int failures;

#define CHECK(condition) \
	do { if (!(condition)) { printf("FAILED: %s (line %d)\n", #condition, __LINE__); failures++; } } while (0)

/* SHA-256, for the ID (the console's is p2p_crypto.c's) */
static unsigned long rotate(unsigned long value, int bits)
{
	return ((value >> bits) | (value << (32 - bits))) & 0xFFFFFFFFUL;
}

void p2p_sha256(const void *data, int size, unsigned char *digest)
{
	static const unsigned long k[64] = {
		0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
		0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
		0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
		0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
		0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
		0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
		0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
		0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2 };
	unsigned long h[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab,
		0x5be0cd19 };
	unsigned char *message = calloc(1, (size_t)size + 72);
	unsigned long long bits = (unsigned long long)size * 8;
	int length = size + 1, offset, i;

	memcpy(message, data, (size_t)size);
	message[size] = 0x80;
	while (length % 64 != 56)
		length++;
	for (i = 0; i < 8; i++)
		message[length + i] = (unsigned char)(bits >> (56 - 8 * i));
	length += 8;
	for (offset = 0; offset < length; offset += 64)
	{
		unsigned long w[64], a, b, c, d, e, f, g, hh;

		for (i = 0; i < 16; i++)
			w[i] = (unsigned long)message[offset + 4 * i] << 24 | (unsigned long)message[offset + 4 * i + 1] << 16 |
				(unsigned long)message[offset + 4 * i + 2] << 8 | message[offset + 4 * i + 3];
		for (i = 16; i < 64; i++)
		{
			unsigned long s0 = rotate(w[i - 15], 7) ^ rotate(w[i - 15], 18) ^ (w[i - 15] >> 3);
			unsigned long s1 = rotate(w[i - 2], 17) ^ rotate(w[i - 2], 19) ^ (w[i - 2] >> 10);

			w[i] = (w[i - 16] + s0 + w[i - 7] + s1) & 0xFFFFFFFFUL;
		}
		a = h[0]; b = h[1]; c = h[2]; d = h[3]; e = h[4]; f = h[5]; g = h[6]; hh = h[7];
		for (i = 0; i < 64; i++)
		{
			unsigned long t1 = (hh + (rotate(e, 6) ^ rotate(e, 11) ^ rotate(e, 25)) + ((e & f) ^ (~e & g)) + k[i] +
				w[i]) & 0xFFFFFFFFUL;
			unsigned long t2 = ((rotate(a, 2) ^ rotate(a, 13) ^ rotate(a, 22)) + ((a & b) ^ (a & c) ^ (b & c))) &
				0xFFFFFFFFUL;

			hh = g; g = f; f = e; e = (d + t1) & 0xFFFFFFFFUL; d = c; c = b; b = a; a = (t1 + t2) & 0xFFFFFFFFUL;
		}
		h[0] = (h[0] + a) & 0xFFFFFFFFUL; h[1] = (h[1] + b) & 0xFFFFFFFFUL; h[2] = (h[2] + c) & 0xFFFFFFFFUL;
		h[3] = (h[3] + d) & 0xFFFFFFFFUL; h[4] = (h[4] + e) & 0xFFFFFFFFUL; h[5] = (h[5] + f) & 0xFFFFFFFFUL;
		h[6] = (h[6] + g) & 0xFFFFFFFFUL; h[7] = (h[7] + hh) & 0xFFFFFFFFUL;
	}
	for (i = 0; i < 32; i++)
		digest[i] = (unsigned char)(h[i / 4] >> (24 - 8 * (i % 4)));
	free(message);
}

static void hex(const unsigned char *bytes, int size, char *text)
{
	int i;

	for (i = 0; i < size; i++)
		snprintf(text + 2 * i, 3, "%02x", bytes[i]);
}

static int write_file(const char *path, const void *data, int size)
{
	FILE *file = fopen(path, "wb");
	int ok = file && fwrite(data, 1, (size_t)size, file) == (size_t)size;

	if (file)
		fclose(file);
	return ok;
}

int main(void)
{
	unsigned char key[XBOX_PLAYER_KEY_SIZE], got[XBOX_PLAYER_KEY_SIZE], id[XBOX_PLAYER_ID_SIZE], other[XBOX_PLAYER_ID_SIZE];
	unsigned char digest[32];
	char text[200], path[] = "player_key_test.tmp";
	int index;

	for (index = 0; index < XBOX_PLAYER_KEY_SIZE; index++)
		key[index] = (unsigned char)(index * 7 + 1);

	/* SHA-256 itself (the stand-in): "abc" */
	p2p_sha256("abc", 3, digest);
	hex(digest, 32, text);
	CHECK(!strcmp(text, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));

	/* the ID: the first 16 bytes of SHA-256("halo-ce-universal player id\n" || key) */
	xbox_player_id_from_key(key, id);
	{
		unsigned char data[28 + XBOX_PLAYER_KEY_SIZE];

		memcpy(data, "halo-ce-universal player id\n", 28);
		memcpy(data + 28, key, sizeof(key));
		p2p_sha256(data, sizeof(data), digest);
		CHECK(!memcmp(digest, id, sizeof(id)));
	}

	/* raw, as the desktop builds keep it */
	CHECK(xbox_player_key_parse(key, 32, got) && !memcmp(got, key, 32));
	/* hex text, with white space around */
	hex(key, 32, text);
	CHECK(xbox_player_key_parse((unsigned char *)text, 64, got) && !memcmp(got, key, 32));
	strcat(text, "\r\n");
	CHECK(xbox_player_key_parse((unsigned char *)text, (int)strlen(text), got) && !memcmp(got, key, 32));
	memmove(text + 3, text, strlen(text) + 1);
	memcpy(text, " \t\n", 3);
	CHECK(xbox_player_key_parse((unsigned char *)text, (int)strlen(text), got) && !memcmp(got, key, 32));
	/* upper case */
	hex(key, 32, text);
	for (index = 0; text[index]; index++)
		if (text[index] >= 'a' && text[index] <= 'f')
			text[index] = (char)(text[index] - 32);
	CHECK(xbox_player_key_parse((unsigned char *)text, 64, got) && !memcmp(got, key, 32));

	/* refused: short, long, not hex, zeros, empty */
	hex(key, 32, text);
	CHECK(!xbox_player_key_parse((unsigned char *)text, 62, got));
	CHECK(!xbox_player_key_parse((unsigned char *)text, 66, got));
	text[10] = 'g';
	CHECK(!xbox_player_key_parse((unsigned char *)text, 64, got));
	memset(got, 0, sizeof(got));
	CHECK(!xbox_player_key_parse(got, 32, got));
	CHECK(!xbox_player_key_parse(key, 0, got));
	CHECK(!xbox_player_key_parse(NULL, 32, got));
	hex(key, 32, text);
	CHECK(!xbox_player_key_parse((unsigned char *)text, 31, got));
	CHECK(!xbox_player_key_parse((unsigned char *)text, 33, got));

	/* files */
	CHECK(write_file(path, key, 32) && xbox_player_id_from_file(path, other) && !memcmp(id, other, sizeof(id)));
	hex(key, 32, text);
	CHECK(write_file(path, text, 64) && xbox_player_id_from_file(path, other) && !memcmp(id, other, sizeof(id)));
	CHECK(write_file(path, "short", 5) && !xbox_player_id_from_file(path, other));
	{
		unsigned char big[1000];

		memset(big, 'a', sizeof(big));
		CHECK(write_file(path, big, sizeof(big)) && !xbox_player_id_from_file(path, other));
	}
	remove(path);
	CHECK(!xbox_player_id_from_file(path, other));

	printf("%s\n", failures ? "player_key_test: FAILED" : "player_key_test: ok");
	return failures ? 1 : 0;
}
