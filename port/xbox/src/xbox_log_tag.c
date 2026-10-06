/*
XBOX_LOG_TAG.C

The log's tags (xbox_log_tag.h): FNV-1a over the salt and the bytes, in 32
bits on every machine.
*/

#include <string.h>

#include "../include/xbox_log_tag.h"

char *xbox_log_tag(const char *prefix, const unsigned char *bytes, int length, unsigned long salt, char *text,
	int size)
{
	static const char digits[] = "0123456789abcdef";
	char tag[32];
	unsigned long hash = (2166136261UL ^ salt) & 0xFFFFFFFFUL;
	size_t prefix_length = prefix ? strlen(prefix) : 0;
	int index;

	if (!text || size <= 0)
		return text;
	for (index = 0; bytes && index < length; index++)
		hash = ((hash ^ bytes[index]) * 16777619UL) & 0xFFFFFFFFUL;
	if (prefix_length > 16)
		prefix_length = 16;
	memcpy(tag, prefix ? prefix : "", prefix_length);
	tag[prefix_length] = '#';
	for (index = 0; index < 6; index++)
		tag[prefix_length + 1 + index] = digits[(hash >> (20 - 4 * index)) & 0xF];
	tag[prefix_length + 7] = 0;
	strncpy(text, tag, (size_t)size - 1);
	text[size - 1] = 0;
	return text;
}
