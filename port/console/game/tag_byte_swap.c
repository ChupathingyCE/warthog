/*
TAG_BYTE_SWAP.C

Applies the generated layouts (tag_byte_swap.h) to a map's tags as they
load: each tag's fields by its group's layout, then each of its blocks'
elements by theirs, recursively. Everything is checked against the data the
tags were read into, so a layout that does not match a map's data stops at
the bad block instead of writing past it, and says so.
*/

#include "cseries.h"
#include "errors.h"

#include "tag_byte_swap.h"

#define MAXIMUM_DEPTH 16
#define MAXIMUM_REPORTS 32

struct swap_region
{
	unsigned char *base;
	unsigned long address;
	unsigned long size;
	/* what was done, for the log */
	unsigned long tags;
	unsigned long blocks;
	unsigned long elements;
	unsigned long bad_blocks;
	unsigned long reports;
};

static unsigned short swap16(unsigned short value)
{
	return (unsigned short)((value << 8) | (value >> 8));
}

static unsigned long swap32(unsigned long value)
{
	return ((value & 0xFFUL) << 24) | ((value & 0xFF00UL) << 8) | ((value >> 8) & 0xFF00UL) |
		((value >> 24) & 0xFFUL);
}

static unsigned long read32(const unsigned char *at)
{
	unsigned long value;

	memcpy(&value, at, 4);
	return value;
}

static void swap_fields(unsigned char *at, unsigned long op, unsigned long count)
{
	unsigned long index;

	for (index = 0; index < count; index++, at += op)
	{
		switch (op)
		{
		case 2:
		{
			unsigned short value;

			memcpy(&value, at, 2);
			value = swap16(value);
			memcpy(at, &value, 2);
			break;
		}
		case 4:
		{
			unsigned long value = swap32(read32(at));

			memcpy(at, &value, 4);
			break;
		}
		case 8:
		{
			unsigned long low = swap32(read32(at)), high = swap32(read32(at + 4));

			memcpy(at, &high, 4);
			memcpy(at + 4, &low, 4);
			break;
		}
		}
	}
}

/* whether [address, address + size) is inside the region */
static int region_holds(const struct swap_region *region, unsigned long address, unsigned long size)
{
	unsigned long offset = address - region->address;

	return address >= region->address && offset <= region->size && size <= region->size - offset;
}

static void swap_element(struct swap_region *region, unsigned long layout_index, unsigned long address,
	int depth);

static void report(struct swap_region *region, unsigned long layout_index, unsigned long offset,
	long count, unsigned long address)
{
	region->bad_blocks++;
	if (region->reports++ < MAXIMUM_REPORTS)
	{
		error(_error_silent, "tag byte swap: layout %lu's block at +0x%lx is not one (count %ld, address 0x%08lx)",
			layout_index, offset, count, address);
	}
}

static void swap_layout(struct swap_region *region, unsigned long layout_index, unsigned long address, int depth)
{
	const struct tag_swap_layout *layout = &tag_swap_layouts[layout_index];
	unsigned char *at = region->base + (address - region->address);
	unsigned long op_index;

	/* the fields first: the blocks' counts and addresses among them */
	for (op_index = 0; op_index < layout->op_count; op_index++)
	{
		const struct tag_swap_op *op = &tag_swap_ops[layout->first_op + op_index];

		if (op->op)
			swap_fields(at + op->offset, op->op, op->argument);
	}
	if (depth >= MAXIMUM_DEPTH)
		return;
	for (op_index = 0; op_index < layout->op_count; op_index++)
	{
		const struct tag_swap_op *op = &tag_swap_ops[layout->first_op + op_index];
		long count;
		unsigned long element_address, element_size, index;

		if (op->op)
			continue;
		count = (long)read32(at + op->offset);
		element_address = read32(at + op->offset + 4);
		element_size = tag_swap_layouts[op->argument].size;
		if (!count)
			continue;
		if (count < 0 || !element_size || (unsigned long)count > region->size / element_size ||
			!region_holds(region, element_address, (unsigned long)count * element_size))
		{
			report(region, layout_index, op->offset, count, element_address);
			continue;
		}
		region->blocks++;
		for (index = 0; index < (unsigned long)count; index++)
			swap_element(region, op->argument, element_address + index * element_size, depth + 1);
	}
}

static void swap_element(struct swap_region *region, unsigned long layout_index, unsigned long address, int depth)
{
	region->elements++;
	swap_layout(region, layout_index, address, depth);
}

static long group_layout(unsigned long group_tag)
{
	int low = 0, high = tag_swap_group_count - 1;

	while (low <= high)
	{
		int middle = (low + high) / 2;

		if (tag_swap_groups[middle].group_tag == group_tag)
			return (long)tag_swap_groups[middle].layout;
		if (tag_swap_groups[middle].group_tag < group_tag)
			low = middle + 1;
		else
			high = middle - 1;
	}
	return NONE;
}

/* the tag header and instances (cache_files.c's layouts) */
#define TAG_HEADER_INSTANCES 0x0
#define TAG_HEADER_COUNT 0xC
#define TAG_INSTANCE_SIZE 0x20
#define TAG_INSTANCE_GROUP 0x0
#define TAG_INSTANCE_ADDRESS 0x14
#define STRUCTURE_BSP_GROUP 0x73627370UL /* 'sbsp' */

void tag_byte_swap_tags(void *base, unsigned long base_address, unsigned long size)
{
	struct swap_region region;
	unsigned long instances, count, index;
	unsigned long missing_groups[16];
	unsigned long missing_counts[16];
	int missing = 0, missing_index;

	csmemset(&region, 0, sizeof(region));
	region.base = base;
	region.address = base_address;
	region.size = size;
	instances = read32(region.base + TAG_HEADER_INSTANCES);
	count = read32(region.base + TAG_HEADER_COUNT);
	if (!region_holds(&region, instances, count * TAG_INSTANCE_SIZE))
	{
		error(_error_silent, "tag byte swap: the tag index is not in the tag data");
		return;
	}
	for (index = 0; index < count; index++)
	{
		unsigned char *instance = region.base + (instances - base_address) + index * TAG_INSTANCE_SIZE;
		unsigned long group_tag = read32(instance + TAG_INSTANCE_GROUP);
		unsigned long address = read32(instance + TAG_INSTANCE_ADDRESS);
		long layout = group_layout(group_tag);

		/* (a structure bsp's tag is read with its bsp) */
		if (group_tag == STRUCTURE_BSP_GROUP || !region_holds(&region, address, 4))
			continue;
		if (layout == NONE)
		{
			for (missing_index = 0; missing_index < missing; missing_index++)
			{
				if (missing_groups[missing_index] == group_tag)
					break;
			}
			if (missing_index == missing && missing < 16)
			{
				missing_groups[missing] = group_tag;
				missing_counts[missing++] = 0;
			}
			if (missing_index < 16)
				missing_counts[missing_index]++;
			continue;
		}
		if (!region_holds(&region, address, tag_swap_layouts[layout].size))
		{
			report(&region, (unsigned long)layout, 0, 1, address);
			continue;
		}
		region.tags++;
		swap_layout(&region, (unsigned long)layout, address, 0);
	}
	error(_error_silent, "tag byte swap: %lu of %lu tags, %lu blocks, %lu elements; %lu blocks not swapped",
		region.tags, count, region.blocks, region.elements, region.bad_blocks);
	for (missing_index = 0; missing_index < missing; missing_index++)
	{
		error(_error_silent, "tag byte swap: no layout for '%c%c%c%c' (%lu tags, left as they are)",
			(char)(missing_groups[missing_index] >> 24), (char)(missing_groups[missing_index] >> 16),
			(char)(missing_groups[missing_index] >> 8), (char)missing_groups[missing_index],
			missing_counts[missing_index]);
	}
}

void tag_byte_swap_structure_bsp(void *bsp, unsigned long bsp_address, unsigned long region_address,
	unsigned long region_size)
{
	struct swap_region region;
	long layout = group_layout(STRUCTURE_BSP_GROUP);

	csmemset(&region, 0, sizeof(region));
	region.base = (unsigned char *)bsp - (bsp_address - region_address);
	region.address = region_address;
	region.size = region_size;
	if (layout == NONE || !region_holds(&region, bsp_address, tag_swap_layouts[layout].size))
	{
		error(_error_silent, "tag byte swap: the structure bsp cannot be swapped");
		return;
	}
	swap_layout(&region, (unsigned long)layout, bsp_address, 0);
	error(_error_silent, "tag byte swap: structure bsp: %lu blocks, %lu elements; %lu blocks not swapped",
		region.blocks, region.elements, region.bad_blocks);
}
