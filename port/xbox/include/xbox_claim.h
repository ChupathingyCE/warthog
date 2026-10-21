/*
XBOX_CLAIM.H

The console's signed platform claim (the site's POST /v1/claim_signed): a
copy that has no TLS says "this player played on the Xbox" without sending
its player key, by signing a one-time challenge the site gave it. Plain
HTTP is enough: nothing secret travels, and the challenge stops a replay.

  1. GET /v1/claim_challenge          {"challenge": "<32 hex>", "expires_in": 300}
  2. POST /v1/claim_signed            {"player", "platform", "build", "challenge", "signature"}

The signature is Ed25519, by the key the desktop builds' moderator key is
(browser.c: the seed is the SHA-256 of "halo-ce-universal moderator key\n"
and the player key), over
"chupathingy claim v1\n" player "\n" platform "\n" challenge "\n"
(lowercase hex). There are no timestamps: Xbox clocks are often wrong.

The identity file (D:\game_list_player.key) being there is the player's
consent. Without it nothing is made and nothing is sent. Neither the key,
the seed nor the signature is ever logged.

Plain C: port/xbox/tests builds it on the host too.
*/

#ifndef __XBOX_CLAIM_H
#define __XBOX_CLAIM_H

#define XBOX_CLAIM_CHALLENGE_PATH "/v1/claim_challenge"
#define XBOX_CLAIM_PATH "/v1/claim_signed"
#define XBOX_CLAIM_PLATFORM "xbox"
#define XBOX_CLAIM_BUILD "Warthog"
#define XBOX_CLAIM_KEY_FILE "D:\\game_list_player.key"

/* the sizes, in characters (without the terminator) */
#define XBOX_CLAIM_PLAYER_LENGTH 32
#define XBOX_CLAIM_CHALLENGE_LENGTH 32
#define XBOX_CLAIM_SIGNATURE_LENGTH 128
/* the longest message and body, with their terminators */
#define XBOX_CLAIM_MESSAGE_SIZE 160
#define XBOX_CLAIM_BODY_SIZE 384

enum
{
	XBOX_CLAIM_OK = 0,
	XBOX_CLAIM_NO_IDENTITY,
	XBOX_CLAIM_CHALLENGE_FAILED,
	XBOX_CLAIM_CHALLENGE_BAD,
	XBOX_CLAIM_CLAIM_FAILED,
	XBOX_CLAIM_REFUSED,
	NUMBER_OF_XBOX_CLAIM_RESULTS
};

const char *xbox_claim_result_string(int result);

/* the signed message into text: its length, or 0 if it does not fit */
int xbox_claim_message(const char *player, const char *platform, const char *challenge, char *text, int size);

/* the challenge in the site's reply (32 lowercase hex digits after the key
"challenge"; nothing else is believed): 1 and it in challenge (33 bytes) */
int xbox_claim_parse_challenge(const char *body, unsigned long size, char *challenge);

/* the claim's request body: its length, or 0 if it does not fit */
int xbox_claim_body(const char *player, const char *platform, const char *build, const char *challenge,
	const char *signature, char *text, int size);

/* whether the site's answer to the claim says {"ok": true} */
int xbox_claim_parse_ok(const char *body, unsigned long size);

/* the moderator key's Ed25519 public key of a player key, and the signature
of a message by it (both as lowercase hex when asked: public_hex 64 digits
and a terminator, signature_hex 128 and one); the key pair is made and
wiped inside */
void xbox_claim_sign(const unsigned char *key, const void *message, int size, char *public_hex, char *signature_hex);

/* one request to the site: path, and the body (NULL for a GET); the
answer's HTTP status and body (pointing into the transport's buffer, valid
until its next call). 1 if a response came, else 0 */
typedef int (*xbox_claim_transport)(void *context, const char *path, const char *body, int *status,
	const char **reply, unsigned long *reply_size);

/* the whole claim for the key at key_path: XBOX_CLAIM_NO_IDENTITY (with
transport never called) if there is no key there, else the exchange */
int xbox_claim_run(const char *key_path, const char *platform, const char *build, xbox_claim_transport transport,
	void *context);

/* ---------- the console (xbox_claim_run.c) */

/* the main thread's, when an online game has ended: starts the claim on a
thread of its own, once per boot (a later call does nothing) */
void xbox_claim_game_ended(void);

#endif
