#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define GROWTH_FACTOR 2
#define INIT_CAPACITY 1

/*
 * These conditions must always be true for the growth logic to work.
 */
_Static_assert(INIT_CAPACITY > 0, "INIT_CAPACITY must be greater than zero");
_Static_assert(GROWTH_FACTOR > 1, "GROWTH_FACTOR must be greater than one");

/*
 * Memory layout:
 *
 * +----------------------+-------------------------+
 * | struct array_header  | array elements          |
 * | capacity, count      | arr[0], arr[1], ...     |
 * +----------------------+-------------------------+
 * ^                      ^
 * header                 arr
 */
struct array_header
{
	size_t capacity;
	size_t count;
};

/*
 * Recover the hidden header stored immediately before the array data.
 *
 * Casting arr to struct array_header * and subtracting one moves backwards
 * by exactly sizeof(struct array_header).
 */
static inline struct array_header *get_array_header(void *arr)
{
	if (arr == NULL)
	{
		return NULL;
	}

	return ((struct array_header *)arr) - 1;
}

/*
 * Free the complete allocation, starting from the hidden header.
 *
 * This function cannot set the caller's pointer to NULL because arr is passed
 * by value. The caller should set its pointer to NULL after calling this.
 */
void array_free(void *arr)
{
	if (arr == NULL)
	{
		return;
	}

	free(get_array_header(arr));
}

/*
 * Return the number of elements currently stored in the array.
 *
 * A NULL array is treated as an empty array.
 */
size_t array_len(void *arr)
{
	struct array_header *header = get_array_header(arr);

	if (header == NULL)
	{
		return 0;
	}

	return header->count;
}

/*
 * Return the number of elements the current allocation can hold.
 *
 * A NULL array has zero capacity.
 */
size_t array_capacity(void *arr)
{
	struct array_header *header = get_array_header(arr);

	if (header == NULL)
	{
		return 0;
	}

	return header->capacity;
}

/*
 * Append an item to the array.
 *
 * Behaviour:
 * 1. If arr is NULL, allocate the initial array.
 * 2. If the array is full, increase its capacity.
 * 3. Store the item and increase the count.
 *
 * On allocation failure, the append is cancelled and the existing array
 * remains unchanged.
 */
#define array_append(arr, item)                                                                    \
	do                                                                                             \
	{                                                                                              \
		if ((arr) == NULL)                                                                         \
		{                                                                                          \
			/* Prevent overflow in the initial allocation-size calculation. */                     \
			if (INIT_CAPACITY > (SIZE_MAX - sizeof(struct array_header)) / sizeof(*(arr)))         \
			{                                                                                      \
				fputs("array_append: initial allocation size overflow\n", stderr);                 \
				break;                                                                             \
			}                                                                                      \
                                                                                                   \
			size_t allocation_size = sizeof(struct array_header) + sizeof(*(arr)) * INIT_CAPACITY; \
                                                                                                   \
			struct array_header *new_header = malloc(allocation_size);                             \
                                                                                                   \
			if (new_header == NULL)                                                                \
			{                                                                                      \
				perror("array_append: malloc");                                                    \
				break;                                                                             \
			}                                                                                      \
                                                                                                   \
			new_header->capacity = INIT_CAPACITY;                                                  \
			new_header->count = 0;                                                                 \
                                                                                                   \
			(arr) = (void *)(new_header + 1);                                                      \
		}                                                                                          \
                                                                                                   \
		struct array_header *header = get_array_header((arr));                                     \
                                                                                                   \
		if (header->count >= header->capacity)                                                     \
		{                                                                                          \
			/* Prevent overflow when multiplying the capacity. */                                  \
			if (header->capacity > SIZE_MAX / GROWTH_FACTOR)                                       \
			{                                                                                      \
				fputs("array_append: capacity overflow\n", stderr);                                \
				break;                                                                             \
			}                                                                                      \
                                                                                                   \
			size_t new_capacity = header->capacity * GROWTH_FACTOR;                                \
                                                                                                   \
			/* Prevent overflow in: header size + element size * capacity. */                      \
			if (new_capacity > (SIZE_MAX - sizeof(*header)) / sizeof(*(arr)))                      \
			{                                                                                      \
				fputs("array_append: allocation size overflow\n", stderr);                         \
				break;                                                                             \
			}                                                                                      \
                                                                                                   \
			size_t new_allocation_size = sizeof(*header) + sizeof(*(arr)) * new_capacity;          \
                                                                                                   \
			/*                                                                                     \
			 * Use a temporary pointer.                                                            \
			 *                                                                                     \
			 * If realloc fails, it returns NULL but the original allocation remains               \
			 * valid. Assigning directly to header would lose the original pointer.                \
			 */                                                                                    \
			struct array_header *temporary_header = realloc(header, new_allocation_size);          \
                                                                                                   \
			if (temporary_header == NULL)                                                          \
			{                                                                                      \
				perror("array_append: realloc");                                                   \
				break;                                                                             \
			}                                                                                      \
                                                                                                   \
			header = temporary_header;                                                             \
			header->capacity = new_capacity;                                                       \
                                                                                                   \
			/* realloc may move the allocation, so update the array pointer. */                    \
			(arr) = (void *)(header + 1);                                                          \
		}                                                                                          \
                                                                                                   \
		(arr)[header->count] = (item);                                                             \
		++header->count;                                                                           \
	} while (0)

/*
 * Remove an element at the supplied index.
 *
 * Elements after the removed item are shifted one position to the left.
 * Invalid indexes are ignored.
 *
 * A negative index becomes a large size_t value after conversion and will
 * fail the bounds check.
 */
#define array_remove(arr, index)                                                                   \
	do                                                                                             \
	{                                                                                              \
		struct array_header *header = get_array_header((arr));                                     \
		size_t remove_index = (size_t)(index);                                                     \
                                                                                                   \
		if (header == NULL || remove_index >= header->count)                                       \
		{                                                                                          \
			break;                                                                                 \
		}                                                                                          \
                                                                                                   \
		for (size_t i = remove_index; i + 1 < header->count; ++i)                                  \
		{                                                                                          \
			(arr)[i] = (arr)[i + 1];                                                               \
		}                                                                                          \
                                                                                                   \
		--header->count;                                                                           \
	} while (0)

int main(void)
{
	char *s = NULL;

	array_append(s, 'a');
	array_append(s, 'b');
	array_append(s, 'c');
	array_append(s, 'd');

	/*
	 * Avoid subtracting one from zero because array_len returns size_t,
	 * which is unsigned.
	 */
	if (array_len(s) > 0)
	{
		array_remove(s, array_len(s) - 1);
	}

	for (size_t i = 0; i < array_len(s); ++i)
	{
		printf("%c\n", s[i]);
	}

	printf("Array Length = %zu\n", array_len(s));
	printf("Array Capacity = %zu\n", array_capacity(s));

	array_free(s);

	/*
	 * array_free cannot modify the caller's variable, so clear it manually
	 * to prevent accidental use of a dangling pointer.
	 */
	s = NULL;

	return 0;
}
