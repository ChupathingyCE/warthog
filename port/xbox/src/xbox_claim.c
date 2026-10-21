/*
XBOX_CLAIM.C

The signed platform claim's bytes and exchange (xbox_claim.h).
*/

#include <stdio.h>
#include <string.h>

#include "../include/xbox_claim.h"
#include "../include/xbox_player_key.h"

#ifdef _XBOX
/* (the console's C runtime has _snprintf; xbox_port.c gives snprintf) */
int snprintf(char *buffer, size_t size, const char *format, ...);
#endif

/* p2p_crypto.c's */
void p2p_sha256(const void *data, int size, unsigned char *digest);
void p2p_ed25519_public(const unsigned char *seed, unsigned char *public_key, unsigned char *x25519_secret);
void p2p_ed25519_sign(const unsigned char *seed, const unsigned char *public_key, const void *message, int size,
	unsigned char *signature);

static const char *const result_strings[NUMBER_OF_XBOX_CLAIM_RESULTS] = {
	"claimed", "no identity file", "no challenge from the site", "the site's challenge was not one",
	"the claim got no answer", "the site did not take the claim"
};

const char *xbox_claim_result_string(int result)
{
	return result >= 0 && result < NUMBER_OF_XBOX_CLAIM_RESULTS ? result_strings[result] : "unknown";
}

static void hex(const unsigned char *bytes, int size, char *text)
{
	static const char digits[] = "0123456789abcdef";
	int index;

	for (index = 0; index < size; index++)
	{
		text[2 * index] = digits[bytes[index] >> 4];
		text[2 * index + 1] = digits[bytes[index] & 15];
	}
	text[2 * size] = 0;
}

static int lowercase_hex(const char *text, int length)
{
	int index;

	for (index = 0; index < length; index++)
	{
		if (!((text[index] >= '0' && text[index] <= '9') || (text[index] >= 'a' && text[index] <= 'f')))
			return 0;
	}
	return 1;
}

/* a field that is plain text for a JSON string: no quote, backslash or
control character */
static int plain_text(const char *text)
{
	for (; *text; text++)
	{
		if (*text == '"' || *text == '\\' || (unsigned char)*text < 32 || (unsigned char)*text > 126)
			return 0;
	}
	return 1;
}

int xbox_claim_message(const char *player, const char *platform, const char *challenge, char *text, int size)
{
	int length = snprintf(text, size, "chupathingy claim v1\n%s\n%s\n%s\n", player, platform, challenge);

	return length > 0 && length < size ? length : 0;
}

/* the value after "key": in body (bounded by size): its first character, or NULL */
static const char *find_value(const char *body, unsigned long size, const char *key)
{
	unsigned long length = (unsigned long)strlen(key), index, at;

	for (index = 0; index + length + 2 <= size; index++)
	{
		if (body[index] != '"' || memcmp(body + index + 1, key, length) || body[index + 1 + length] != '"')
			continue;
		at = index + length + 2;
		while (at < size && (body[at] == ' ' || body[at] == '\t' || body[at] == '\r' || body[at] == '\n'))
			at++;
		if (at >= size || body[at] != ':')
			continue;
		at++;
		while (at < size && (body[at] == ' ' || body[at] == '\t' || body[at] == '\r' || body[at] == '\n'))
			at++;
		return at < size ? body + at : NULL;
	}
	return NULL;
}

int xbox_claim_parse_challenge(const char *body, unsigned long size, char *challenge)
{
	const char *value = find_value(body, size, "challenge");

	if (!value || (unsigned long)(body + size - value) < XBOX_CLAIM_CHALLENGE_LENGTH + 2 || value[0] != '"' ||
		value[XBOX_CLAIM_CHALLENGE_LENGTH + 1] != '"' || !lowercase_hex(value + 1, XBOX_CLAIM_CHALLENGE_LENGTH))
		return 0;
	memcpy(challenge, value + 1, XBOX_CLAIM_CHALLENGE_LENGTH);
	challenge[XBOX_CLAIM_CHALLENGE_LENGTH] = 0;
	return 1;
}

int xbox_claim_body(const char *player, const char *platform, const char *build, const char *challenge,
	const char *signature, char *text, int size)
{
	int length;

	if (strlen(player) != XBOX_CLAIM_PLAYER_LENGTH || strlen(challenge) != XBOX_CLAIM_CHALLENGE_LENGTH ||
		strlen(signature) != XBOX_CLAIM_SIGNATURE_LENGTH || strlen(build) > 64 || !plain_text(platform) ||
		!plain_text(build))
		return 0;
	length = snprintf(text, size,
		"{\"player\": \"%s\", \"platform\": \"%s\", \"build\": \"%s\", \"challenge\": \"%s\", \"signature\": \"%s\"}",
		player, platform, build, challenge, signature);
	return length > 0 && length < size ? length : 0;
}

int xbox_claim_parse_ok(const char *body, unsigned long size)
{
	const char *value = find_value(body, size, "ok");

	return value && (unsigned long)(body + size - value) >= 4 && !memcmp(value, "true", 4);
}

void xbox_claim_sign(const unsigned char *key, const void *message, int size, char *public_hex, char *signature_hex)
{
	static const char label[] = "halo-ce-universal moderator key\n";
	unsigned char data[sizeof(label) - 1 + XBOX_PLAYER_KEY_SIZE];
	unsigned char seed[32], public_key[32], signature[64];

	memcpy(data, label, sizeof(label) - 1);
	memcpy(data + sizeof(label) - 1, key, XBOX_PLAYER_KEY_SIZE);
	p2p_sha256(data, (int)sizeof(data), seed);
	p2p_ed25519_public(seed, public_key, NULL);
	p2p_ed25519_sign(seed, public_key, message, size, signature);
	if (public_hex)
		hex(public_key, 32, public_hex);
	if (signature_hex)
		hex(signature, 64, signature_hex);
	memset(data, 0, sizeof(data));
	memset(seed, 0, sizeof(seed));
	memset(signature, 0, sizeof(signature));
}

int xbox_claim_run(const char *key_path, const char *platform, const char *build, xbox_claim_transport transport,
	void *context)
{
	unsigned char key[XBOX_PLAYER_KEY_SIZE], id[XBOX_PLAYER_ID_SIZE];
	char player[XBOX_CLAIM_PLAYER_LENGTH + 1], challenge[XBOX_CLAIM_CHALLENGE_LENGTH + 1];
	char signature[XBOX_CLAIM_SIGNATURE_LENGTH + 1];
	char message[XBOX_CLAIM_MESSAGE_SIZE], body[XBOX_CLAIM_BODY_SIZE];
	const char *reply;
	unsigned long reply_size;
	int status, length, result;

	if (!xbox_player_key_from_file(key_path, key))
		return XBOX_CLAIM_NO_IDENTITY;
	xbox_player_id_from_key(key, id);
	hex(id, XBOX_PLAYER_ID_SIZE, player);
	if (!transport(context, XBOX_CLAIM_CHALLENGE_PATH, NULL, &status, &reply, &reply_size) || status != 200)
	{
		memset(key, 0, sizeof(key));
		return XBOX_CLAIM_CHALLENGE_FAILED;
	}
	if (!xbox_claim_parse_challenge(reply, reply_size, challenge))
	{
		memset(key, 0, sizeof(key));
		return XBOX_CLAIM_CHALLENGE_BAD;
	}
	length = xbox_claim_message(player, platform, challenge, message, sizeof(message));
	if (!length)
	{
		memset(key, 0, sizeof(key));
		return XBOX_CLAIM_CLAIM_FAILED;
	}
	xbox_claim_sign(key, message, length, NULL, signature);
	memset(key, 0, sizeof(key));
	length = xbox_claim_body(player, platform, build, challenge, signature, body, sizeof(body));
	memset(signature, 0, sizeof(signature));
	if (!length)
	{
		memset(body, 0, sizeof(body));
		return XBOX_CLAIM_CLAIM_FAILED;
	}
	result = transport(context, XBOX_CLAIM_PATH, body, &status, &reply, &reply_size);
	memset(body, 0, sizeof(body));
	if (!result)
		return XBOX_CLAIM_CLAIM_FAILED;
	return status == 200 && xbox_claim_parse_ok(reply, reply_size) ? XBOX_CLAIM_OK : XBOX_CLAIM_REFUSED;
}
