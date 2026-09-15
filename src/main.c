#include <assert.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DATA_CAPACITY 2
#define ITEM_CAPACITY 1
#define GROWTH_FACTOR 2

/*
 * TODO
 *
 * - Store item alignment in array_item_header.
 *
 * - Rework array growth.
 *   Current realloc can move the backing allocation after data,
 *   padding and destination_start have already been calculated.
 *
 * - Consider making growth explicit with array_grow().
 *   array_append() can fail when there is not enough space.
 *   array_grow() will invalidate previously returned pointers.
 *
 * - For data growth, experiment with malloc + repacking instead of
 *   realloc:
 *      allocate new arena
 *      recalculate alignment/padding
 *      copy each item
 *      update offsets
 *      free old arena
 *
 * - Grow item_header independently from the data arena.
 *
 * - Clean up array_get:
 *      NULL checks
 *      bounds checks
 *
 * - Rework array_remove using tombstones/free chunks.
 *   Do not compact data for now so unrelated pointers stay valid.
 *
 * - Track freed chunks so later inserts can reuse space.
 *
 * - Add a second insertion strategy that searches free chunks for
 *   suitable size/alignment.
 *
 * - Define pointer policy:
 *      get() pointer remains valid until its item is removed,
 *      the arena is grown/repacked, or the array is destroyed.
 *
 * - Add array_destroy() to free item_header and arr.
 *
 * - Add integer overflow checks for allocation/size calculations.
 *
 * - Improve error handling.
 *
 * - Later: convenience macros / _Generic for size and alignment.
 */

struct array_item_header
{
	size_t offset;
	size_t size;
};

struct array_header
{
	size_t length;
	size_t data_used;
	size_t data_capacity;
	size_t item_capacity;
	struct array_item_header *item_header;
};

struct array_header *array_init(void)
{
	struct array_header *arr = malloc(sizeof(*arr) + sizeof(unsigned char) * DATA_CAPACITY);

	if (arr == NULL)
	{
		return NULL;
	}

	struct array_item_header *arr_entries = malloc(sizeof(*arr_entries) * ITEM_CAPACITY);

	if (arr_entries == NULL)
	{
		free(arr);
		return NULL;
	}

	arr->length = 0;
	arr->data_used = 0;
	arr->data_capacity = DATA_CAPACITY;
	arr->item_capacity = ITEM_CAPACITY;
	arr->item_header = arr_entries;

	return arr;
}

#define array_append(arr, item, item_size, item_align)                                             \
	do                                                                                             \
	{                                                                                              \
		if ((arr) == NULL)                                                                         \
		{                                                                                          \
			(arr) = array_init();                                                                  \
			if ((arr) == NULL)                                                                     \
			{                                                                                      \
				fputs("Error: malloc failed to initialize memory\n", stderr);                      \
				break;                                                                             \
			}                                                                                      \
		}                                                                                          \
                                                                                                   \
		struct array_header *header = (struct array_header *)(arr);                                \
		struct array_item_header *item_header = header->item_header;                               \
                                                                                                   \
		if (item_header == NULL)                                                                   \
		{                                                                                          \
			fputs("Error: Invalid array_header\n", stderr);                                        \
			break;                                                                                 \
		}                                                                                          \
                                                                                                   \
		unsigned char *data = (unsigned char *)(header + 1);                                       \
                                                                                                   \
		const size_t append_item_size = (item_size);                                               \
		const size_t append_item_align = (item_align);                                             \
                                                                                                   \
		if (append_item_align == 0)                                                                \
		{                                                                                          \
			fputs("array_append: item alignment must be greater than zero\n", stderr);             \
			break;                                                                                 \
		}                                                                                          \
                                                                                                   \
		uintptr_t current_address = (uintptr_t)(data + header->data_used);                         \
                                                                                                   \
		size_t alignment_remainder = current_address % append_item_align;                          \
                                                                                                   \
		size_t padding = alignment_remainder == 0 ? 0 : append_item_align - alignment_remainder;   \
                                                                                                   \
		size_t destination_start = header->data_used + padding;                                    \
                                                                                                   \
		if (destination_start + append_item_size > header->data_capacity)                          \
		{                                                                                          \
			size_t new_capacity = header->data_capacity * GROWTH_FACTOR;                           \
                                                                                                   \
			size_t new_allocation_size = sizeof(*header) + new_capacity;                           \
                                                                                                   \
			struct array_header *temporary_header = realloc(header, new_allocation_size);          \
                                                                                                   \
			if (temporary_header == NULL)                                                          \
			{                                                                                      \
				perror("array_append: realloc");                                                   \
				break;                                                                             \
			}                                                                                      \
                                                                                                   \
			header = temporary_header;                                                             \
			header->data_capacity = new_capacity;                                                  \
			(arr) = (void *)header;                                                                \
		}                                                                                          \
                                                                                                   \
		if (header->length >= header->item_capacity)                                               \
		{                                                                                          \
			size_t new_capacity = header->item_capacity * GROWTH_FACTOR;                           \
                                                                                                   \
			size_t new_allocation_size = sizeof(*item_header) * new_capacity;                      \
                                                                                                   \
			struct array_item_header *temporary_item_header =                                      \
			    realloc(item_header, new_allocation_size);                                         \
                                                                                                   \
			if (temporary_item_header == NULL)                                                     \
			{                                                                                      \
				perror("array_append: realloc");                                                   \
				break;                                                                             \
			}                                                                                      \
                                                                                                   \
			item_header = temporary_item_header;                                                   \
			header->item_capacity = new_capacity;                                                  \
			header->item_header = (void *)item_header;                                             \
		}                                                                                          \
                                                                                                   \
		memcpy(data + destination_start, (item), append_item_size);                                \
                                                                                                   \
		size_t length = header->length;                                                            \
                                                                                                   \
		item_header[length].offset = destination_start;                                            \
		item_header[length].size = append_item_size;                                               \
                                                                                                   \
		header->length += 1;                                                                       \
		header->data_used = destination_start + append_item_size;                                  \
                                                                                                   \
	} while (0)

void *array_get(struct array_header *arr, size_t index)
{
	struct array_item_header *item_header = arr->item_header;

	struct array_item_header *item = item_header + index;

	unsigned char *data = (unsigned char *)(arr + 1);

	return data + item->offset;
}

#define array_remove(arr, index)                                                                   \
	do                                                                                             \
	{                                                                                              \
		if ((arr) == NULL)                                                                         \
		{                                                                                          \
			fputs("Error: malloc failed to initialize memory\n", stderr);                          \
			break;                                                                                 \
		}                                                                                          \
                                                                                                   \
		struct array_header *header = (struct array_header *)(arr);                                \
                                                                                                   \
		if (index > header->length || index < 0)                                                   \
		{                                                                                          \
			perror("out of bound");                                                                \
			break;                                                                                 \
		}                                                                                          \
                                                                                                   \
		struct array_item_header *item_header = header->item_header;                               \
                                                                                                   \
		if (item_header == NULL)                                                                   \
		{                                                                                          \
			fputs("Error: Invalid array_header\n", stderr);                                        \
			break;                                                                                 \
		}                                                                                          \
                                                                                                   \
		for (size_t i = index; i < header->length - 1; ++i)                                        \
		{                                                                                          \
			item_header[i] = item_header[i + 1];                                                   \
		}                                                                                          \
                                                                                                   \
		header->length -= 1;                                                                       \
                                                                                                   \
	} while (0)

int main(void)
{
	void *arr = NULL;

	char item[] = "skills";
	size_t item_size = strlen(item) + 1;

	int item1 = 676;
	float item2 = 212.0;

	array_append(arr, item, item_size, alignof(char));

	array_append(arr, &item1, sizeof(item1), alignof(int));

	array_append(arr, &item2, sizeof(item2), alignof(float));

	char *arr_item = (char *)array_get(arr, 0);

	int *arr_item1 = (int *)array_get(arr, 1);

	float *arr_item2 = (float *)array_get(arr, 2);

	printf("%s\n", arr_item);
	printf("%d\n", *arr_item1);
	printf("%f\n", *arr_item2);

	free(arr);

	return 0;
}
