/*
XBOX_PLAYER_KEY.C

This copy's player identity from D:\game_list_player.key (xbox_player_key.h).
*/

#include <stdio.h>
#include <string.h>

#include "../include/xbox_player_key.h"

/* p2p_crypto.c's */
void p2p_sha256(const void *data, int size, unsigned char *digest);

#define MAXIMUM_FILE 256

static int hex_value(int character)
{
	if (character >= '0' && character <= '9')
		return character - '0';
	if (character >= 'a' && character <= 'f')
		return character - 'a' + 10;
	if (character >= 'A' && character <= 'F')
		return character - 'A' + 10;
	return -1;
}

static int white_space(int character)
{
	return character == ' ' || character == '\t' || character == '\r' || character == '\n';
}

static int all_zero(const unsigned char *key)
{
	int index;

	for (index = 0; index < XBOX_PLAYER_KEY_SIZE; index++)
	{
		if (key[index])
			return 0;
	}
	return 1;
}

int xbox_player_key_parse(const unsigned char *data, int size, unsigned char *key)
{
	int start = 0, end = size, index;

	if (!data || size <= 0)
		return 0;
	if (size == XBOX_PLAYER_KEY_SIZE)
	{
		/* (raw bytes; a 32-byte file of hex text cannot be 64 digits) */
		memcpy(key, data, XBOX_PLAYER_KEY_SIZE);
		return !all_zero(key);
	}
	while (start < end && white_space(data[start]))
		start++;
	while (end > start && white_space(data[end - 1]))
		end--;
	if (end - start != 2 * XBOX_PLAYER_KEY_SIZE)
		return 0;
	for (index = 0; index < XBOX_PLAYER_KEY_SIZE; index++)
	{
		int high = hex_value(data[start + 2 * index]);
		int low = hex_value(data[start + 2 * index + 1]);

		if (high < 0 || low < 0)
		{
			memset(key, 0, XBOX_PLAYER_KEY_SIZE);
			return 0;
		}
		key[index] = (unsigned char)(high << 4 | low);
	}
	return !all_zero(key);
}

void xbox_player_id_from_key(const unsigned char *key, unsigned char *id)
{
	static const char label[] = "halo-ce-universal player id\n";
	unsigned char data[sizeof(label) - 1 + XBOX_PLAYER_KEY_SIZE];
	unsigned char digest[32];

	memcpy(data, label, sizeof(label) - 1);
	memcpy(data + sizeof(label) - 1, key, XBOX_PLAYER_KEY_SIZE);
	p2p_sha256(data, (int)sizeof(data), digest);
	memcpy(id, digest, XBOX_PLAYER_ID_SIZE);
	memset(data, 0, sizeof(data));
	memset(digest, 0, sizeof(digest));
}

int xbox_player_key_from_file(const char *path, unsigned char *key)
{
	unsigned char data[MAXIMUM_FILE + 1];
	FILE *file = fopen(path, "rb");
	int size, ok;

	if (!file)
		return 0;
	size = (int)fread(data, 1, sizeof(data), file);
	fclose(file);
	ok = size > 0 && size <= MAXIMUM_FILE && xbox_player_key_parse(data, size, key);
	memset(data, 0, sizeof(data));
	return ok;
}

int xbox_player_id_from_file(const char *path, unsigned char *id)
{
	unsigned char key[XBOX_PLAYER_KEY_SIZE];
	int ok = xbox_player_key_from_file(path, key);

	if (ok)
		xbox_player_id_from_key(key, id);
	memset(key, 0, sizeof(key));
	return ok;
}

int xbox_player_id(unsigned char *id)
{
	static int read;
	static int have;
	static unsigned char kept[XBOX_PLAYER_ID_SIZE];

	if (!read)
	{
		read = 1;
		have = xbox_player_id_from_file("D:\\game_list_player.key", kept);
	}
	if (have)
		memcpy(id, kept, XBOX_PLAYER_ID_SIZE);
	return have;
}
