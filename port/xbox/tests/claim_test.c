/*
The console's signed platform claim (port/xbox/src/xbox_claim.c): the signed
message's bytes, a signature checked against the desktop's derivation (made
here again, from the moderator key's label and monocypher), the request
bodies, the challenge's parsing, and that nothing is sent without the
identity file.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "monocypher.h"
#include "monocypher-ed25519.h"

#include "../include/xbox_claim.h"
#include "../include/xbox_player_key.h"

/* p2p_crypto.c's (SHA-256 is no Monocypher function; its known answer is checked below) */
void p2p_sha256(const void *data, int size, unsigned char *digest);

/* (what p2p_crypto.c asks of the platform; the claim uses neither) */
void platform_log(const char *format, ...)
{
	(void)format;
}

void posix_random_bytes(void *bytes, unsigned long size)
{
	memset(bytes, 0, size);
}

static int failures;
#define CHECK(condition) \
	do { if (!(condition)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #condition); failures++; } } while (0)

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

/* the desktop's derivation (browser.c moderator_seed), here again: the seed
is the SHA-256 of the label and the key; the pair is Ed25519's of that seed */
static void reference_pair(const unsigned char *key, unsigned char *secret_key, unsigned char *public_key)
{
	static const char label[] = "halo-ce-universal moderator key\n";
	unsigned char data[sizeof(label) - 1 + 32], seed[32];

	memcpy(data, label, sizeof(label) - 1);
	memcpy(data + sizeof(label) - 1, key, 32);
	p2p_sha256(data, (int)sizeof(data), seed);
	crypto_ed25519_key_pair(secret_key, public_key, seed);
}

struct mock
{
	int calls, gets, posts;
	char last_body[XBOX_CLAIM_BODY_SIZE];
	char challenge_reply[128];
	char claim_reply[64];
	int challenge_status, claim_status;
};

static int mock_transport(void *context, const char *path, const char *body, int *status, const char **reply,
	unsigned long *reply_size)
{
	struct mock *mock = context;

	mock->calls++;
	if (!strcmp(path, XBOX_CLAIM_CHALLENGE_PATH))
	{
		CHECK(!body);
		mock->gets++;
		*status = mock->challenge_status;
		*reply = mock->challenge_reply;
		*reply_size = (unsigned long)strlen(mock->challenge_reply);
		return 1;
	}
	CHECK(!strcmp(path, XBOX_CLAIM_PATH));
	CHECK(body != NULL);
	mock->posts++;
	snprintf(mock->last_body, sizeof(mock->last_body), "%s", body);
	*status = mock->claim_status;
	*reply = mock->claim_reply;
	*reply_size = (unsigned long)strlen(mock->claim_reply);
	return 1;
}

int main(void)
{
	static const char challenge[] = "0123456789abcdef0123456789abcdef";
	unsigned char key[32], id[16], secret_key[64], public_key[32], signature[64];
	char player[33], public_hex[65], reference_public[65], signature_hex[129], message[XBOX_CLAIM_MESSAGE_SIZE];
	char body[XBOX_CLAIM_BODY_SIZE], found[33], expected[XBOX_CLAIM_BODY_SIZE];
	struct mock mock;
	FILE *file;
	int index, length;

	for (index = 0; index < 32; index++)
		key[index] = (unsigned char)(index * 7 + 3);
	xbox_player_id_from_key(key, id);
	hex(id, 16, player);

	{
		unsigned char digest[32];

		p2p_sha256("abc", 3, digest);
		hex(digest, 32, expected);
		CHECK(!strcmp(expected, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
	}

	/* the signed message's bytes */
	length = xbox_claim_message(player, "xbox", challenge, message, sizeof(message));
	snprintf(expected, sizeof(expected), "chupathingy claim v1\n%s\nxbox\n%s\n", player, challenge);
	CHECK(length == (int)strlen(expected));
	CHECK(!memcmp(message, expected, (size_t)length + 1));
	CHECK(message[length - 1] == '\n');
	CHECK(xbox_claim_message(player, "xbox", challenge, message, 20) == 0);

	/* the signature against the reference derivation, and verified */
	reference_pair(key, secret_key, public_key);
	hex(public_key, 32, reference_public);
	xbox_claim_sign(key, message, length, public_hex, signature_hex);
	CHECK(!strcmp(public_hex, reference_public));
	CHECK(strlen(signature_hex) == 128);
	crypto_ed25519_sign(signature, secret_key, (const unsigned char *)message, (size_t)length);
	hex(signature, 64, expected);
	CHECK(!strcmp(signature_hex, expected));
	for (index = 0; index < 64; index++)
	{
		unsigned int byte;

		sscanf(signature_hex + 2 * index, "%2x", &byte);
		signature[index] = (unsigned char)byte;
	}
	CHECK(crypto_ed25519_check(signature, public_key, (const unsigned char *)message, (size_t)length) == 0);
	message[5] ^= 1;
	CHECK(crypto_ed25519_check(signature, public_key, (const unsigned char *)message, (size_t)length) != 0);
	message[5] ^= 1;

	/* the body's format */
	length = xbox_claim_body(player, "xbox", "Warthog", challenge, signature_hex, body, sizeof(body));
	snprintf(expected, sizeof(expected),
		"{\"player\": \"%s\", \"platform\": \"xbox\", \"build\": \"Warthog\", \"challenge\": \"%s\", "
		"\"signature\": \"%s\"}", player, challenge, signature_hex);
	CHECK(length == (int)strlen(expected) && !strcmp(body, expected));
	CHECK(xbox_claim_body(player, "xbox", "Warthog", challenge, signature_hex, body, 50) == 0);
	CHECK(xbox_claim_body(player, "xb\"ox", "Warthog", challenge, signature_hex, body, sizeof(body)) == 0);
	CHECK(xbox_claim_body(player, "xbox", "Warthog", "short", signature_hex, body, sizeof(body)) == 0);

	/* the challenge's parsing */
	{
		const char *good = "{\"challenge\": \"0123456789abcdef0123456789abcdef\", \"expires_in\": 300}";
		const char *upper = "{\"challenge\": \"0123456789ABCDEF0123456789abcdef\"}";
		const char *shortc = "{\"challenge\": \"0123456789abcdef\"}";
		const char *longc = "{\"challenge\": \"0123456789abcdef0123456789abcdef0\"}";

		CHECK(xbox_claim_parse_challenge(good, (unsigned long)strlen(good), found) && !strcmp(found, challenge));
		CHECK(xbox_claim_parse_challenge("{\"challenge\":\"0123456789abcdef0123456789abcdef\"}", 48, found));
		CHECK(!xbox_claim_parse_challenge(upper, (unsigned long)strlen(upper), found));
		CHECK(!xbox_claim_parse_challenge(shortc, (unsigned long)strlen(shortc), found));
		CHECK(!xbox_claim_parse_challenge(longc, (unsigned long)strlen(longc), found));
		/* (bounded by the size given, not a terminator) */
		CHECK(!xbox_claim_parse_challenge(good, 30, found));
		CHECK(!xbox_claim_parse_challenge("", 0, found));
		CHECK(xbox_claim_parse_ok("{\"ok\": true}", 12));
		CHECK(!xbox_claim_parse_ok("{\"ok\": false}", 13));
		CHECK(!xbox_claim_parse_ok("{\"ok\": tru", 10));
	}

	/* nothing is sent without the identity file */
	memset(&mock, 0, sizeof(mock));
	CHECK(xbox_claim_run("build/xbox-tests/no_such_identity.key", "xbox", "Warthog", mock_transport, &mock) ==
		XBOX_CLAIM_NO_IDENTITY);
	CHECK(mock.calls == 0);
	file = fopen("build/xbox-tests/claim_bad.key", "wb");
	fwrite("not a key", 1, 9, file);
	fclose(file);
	CHECK(xbox_claim_run("build/xbox-tests/claim_bad.key", "xbox", "Warthog", mock_transport, &mock) ==
		XBOX_CLAIM_NO_IDENTITY);
	CHECK(mock.calls == 0);

	/* with the file: the exchange, and what is posted verifies */
	file = fopen("build/xbox-tests/claim_good.key", "wb");
	fwrite(key, 1, 32, file);
	fclose(file);
	snprintf(mock.challenge_reply, sizeof(mock.challenge_reply), "{\"challenge\": \"%s\", \"expires_in\": 300}",
		challenge);
	mock.challenge_status = 200;
	mock.claim_status = 200;
	snprintf(mock.claim_reply, sizeof(mock.claim_reply), "{\"ok\": true}");
	CHECK(xbox_claim_run("build/xbox-tests/claim_good.key", "xbox", "Warthog", mock_transport, &mock) ==
		XBOX_CLAIM_OK);
	CHECK(mock.gets == 1 && mock.posts == 1);
	{
		const char *at = strstr(mock.last_body, "\"signature\": \"");

		CHECK(at != NULL);
		if (at)
		{
			snprintf(signature_hex, sizeof(signature_hex), "%.128s", at + 14);
			length = xbox_claim_message(player, "xbox", challenge, message, sizeof(message));
			for (index = 0; index < 64; index++)
			{
				unsigned int byte;

				sscanf(signature_hex + 2 * index, "%2x", &byte);
				signature[index] = (unsigned char)byte;
			}
			CHECK(crypto_ed25519_check(signature, public_key, (const unsigned char *)message, (size_t)length) == 0);
		}
	}
	/* the failures: one exchange each, no second try */
	memset(&mock, 0, sizeof(mock));
	mock.challenge_status = 500;
	CHECK(xbox_claim_run("build/xbox-tests/claim_good.key", "xbox", "Warthog", mock_transport, &mock) ==
		XBOX_CLAIM_CHALLENGE_FAILED);
	CHECK(mock.calls == 1);
	mock.calls = 0;
	mock.challenge_status = 200;
	snprintf(mock.challenge_reply, sizeof(mock.challenge_reply), "{\"challenge\": \"nope\"}");
	CHECK(xbox_claim_run("build/xbox-tests/claim_good.key", "xbox", "Warthog", mock_transport, &mock) ==
		XBOX_CLAIM_CHALLENGE_BAD);
	CHECK(mock.calls == 1);
	snprintf(mock.challenge_reply, sizeof(mock.challenge_reply), "{\"challenge\": \"%s\"}", challenge);
	mock.claim_status = 403;
	snprintf(mock.claim_reply, sizeof(mock.claim_reply), "{\"error\": \"no\"}");
	mock.calls = 0;
	CHECK(xbox_claim_run("build/xbox-tests/claim_good.key", "xbox", "Warthog", mock_transport, &mock) ==
		XBOX_CLAIM_REFUSED);
	CHECK(mock.calls == 2);

	if (failures)
		return 1;
	printf("claim_test: ok\n");
	return 0;
}
