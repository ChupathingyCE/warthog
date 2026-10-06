/*
XBOX_LOG_TAG.H

Tags for what the log must not show whole: a public address ("addr#"), or
another machine's Ethernet address ("enet#"). A tag is the bytes hashed
with a salt made once per run, so the same machine has the same tag
throughout one log, and the tag says nothing of the bytes themselves.
Plain C: port/xbox/tests builds it on the host too.
*/

#ifndef __XBOX_LOG_TAG_H
#define __XBOX_LOG_TAG_H

/* "prefix#xxxxxx" (six hex digits of the salted hash; the prefix at most 16
characters) into text, cut to size; text */
char *xbox_log_tag(const char *prefix, const unsigned char *bytes, int length, unsigned long salt, char *text,
	int size);

#endif
