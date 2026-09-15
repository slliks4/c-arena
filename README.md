# C Packed Generic Arena

> A packed, heterogeneous memory container written in C.

**Status:** Work in progress.

This project started with a simple question:

> How would I build a dynamic array in C that can store objects of different types and sizes while still supporting constant-time indexed lookup?

Following that question led into memory layout, alignment, padding, fragmentation, pointer invalidation, tombstones, and allocator-like memory management.

The result is an experimental packed arena that stores arbitrary object representations inside a shared memory region while maintaining fixed-size metadata for indexed access.

---

## Why?

A normal C array works well because every element has the same size:

```text
[int][int][int][int]
````

The address of an element is simply:

```text
base + index * sizeof(element)
```

But a heterogeneous array could contain:

```text
[string][int][struct][float][...]
```

where every object may have a different size and alignment requirement.

Walking through the payload to find element `n` would make indexed access O(n).

This project uses a separate fixed-size metadata table instead.

```text
                logical index
                     |
                     v
        +-------------------------+
        | offset | size | align   |
        | offset | size | align   |
        | offset | size | align   |
        +-------------------------+
                     |
                     v
        +------------------------------------+
        | object | pad | object | object ...|
        +------------------------------------+
                  packed arena
```

Because metadata entries have a fixed size, locating the metadata for an item remains O(1).

The stored offset then resolves the physical object inside the arena.

---

## Design

Each logical item has metadata describing its position within the backing storage.

Conceptually:

```c
struct array_item_header {
    size_t offset;
    size_t size;
    size_t alignment;
};
```

The main array tracks information such as:

```text
length
data used
data capacity
item capacity
metadata table
```

Objects themselves are copied into the arena as raw bytes.

The arena does not need to know whether those bytes represent:

```text
int
float
struct
string
array
or another object representation
```

The caller provides:

```text
source address
object size
alignment requirement
```

and is responsible for interpreting the returned memory as the correct type.

---

## Alignment

Objects are not simply packed directly after one another.

Before storing an object, the arena calculates the padding required for its alignment.

For example:

```text
"skills\0" = 7 bytes

next object:
int
alignment = 4

memory:

[s][k][i][l][l][s][\0][pad][ int ]
                              ^
                         aligned address
```

The actual destination address must satisfy:

```text
address % alignment == 0
```

This allows pointers returned from the arena to be safely interpreted as their original types, subject to the pointer validity rules below.

---

## Current API Direction

The low-level interface intentionally remains type-agnostic.

Conceptually:

```c
array_append(array, source, size, alignment);
array_get(array, index);
array_remove(array, index);
array_grow(array, new_capacity);
```

Example values may be inserted using their address, size, and alignment:

```c
int value = 42;
float number = 3.14f;

array_append(
    arr,
    &value,
    sizeof(value),
    alignof(int)
);

array_append(
    arr,
    &number,
    sizeof(number),
    alignof(float)
);
```

`array_get()` returns a pointer into the arena.

The caller knows the stored type and may interpret the returned memory accordingly.

A convenience interface using macros and possibly `_Generic` may be added later.

---

## Memory Growth

Growth is intentionally being designed as an **explicit operation** rather than something silently performed by append.

An append that cannot fit inside the current arena should fail rather than unexpectedly relocating the entire backing store.

The caller can then explicitly request growth.

Conceptually:

```text
array_append(...)
        |
        +-- enough capacity --> append
        |
        +-- insufficient capacity --> fail

array_grow(...)
        |
        +-- allocate larger arena
        +-- repack objects
        +-- recalculate alignment
        +-- update offsets
        +-- release old arena
```

This makes an important pointer rule visible to the caller.

---

## Pointer Validity

`array_get()` returns a pointer directly into the arena's backing memory.

That pointer remains valid while the corresponding object remains in place.

Operations that relocate or rebuild the arena invalidate previously returned pointers.

Current intended policy:

```text
array_get()
    returns pointer into arena

array_append()
    does not relocate existing storage
    existing pointers remain valid

array_remove()
    invalidates the removed object
    does not move unrelated objects

array_grow()
    may relocate/repack the arena
    ALL previously returned pointers are invalidated

array_destroy()
    invalidates everything
```

This is an intentional tradeoff for maintaining packed storage and direct access to stored objects.

---

## Removal and Tombstones

Instead of immediately compacting the payload when an item is removed, the current direction is to use a tombstone/free-space approach.

```text
before:

[A][B][C][D]

remove B:

[A][ free ][C][D]
```

This has several useful properties:

* removal can remain O(1)
* unrelated objects are not moved
* pointers to unrelated objects remain valid
* alignment of existing objects does not change

The tradeoff is fragmentation.

Removed regions can later be tracked and reused for new objects.

A future insertion strategy may search free regions for one that can satisfy both:

```text
requested size
requested alignment
```

For example:

```text
free chunks:

offset    size
------    ----
64        16
128       40
256       12
```

An alternative insertion function may attempt to reuse one of these regions before extending the end of the arena.

The exact free-space strategy is still being explored.

---

## Operations and Complexity

Current intended behavior:

| Operation            | Expected complexity | Notes                                              |
| -------------------- | ------------------: | -------------------------------------------------- |
| `get(index)`         |                O(1) | Fixed-size metadata lookup                         |
| `append()`           |                O(1) | When appending at the end with sufficient capacity |
| `remove(index)`      |                O(1) | Using logical deletion/tombstones                  |
| free-space-aware add |      O(n) initially | Searches reusable chunks                           |
| `grow()`             |                O(n) | Objects may need to be repacked and realigned      |

These complexities describe the current design goals and may change as the implementation develops.

---

## Why Not Just Store Pointers?

Another common heterogeneous container design is:

```text
[pointer][pointer][pointer][pointer]
    |       |       |       |
    v       v       v       v
 object  object  object  object
```

This has several advantages, including stable object addresses when the pointer array grows.

This project intentionally explores a different tradeoff:

```text
shared packed storage
+ offset-based lookup
+ fewer independent allocations
+ locality

vs.

pointer stability
+ fragmentation management
+ alignment complexity
```

The goal is not to replace every normal C array or pointer-based container, but to explore packed heterogeneous storage and the memory-management problems that come with it.

---

## Potential Uses

A structure like this could be useful as the basis for experiments involving:

* heterogeneous event or command buffers
* packed object stores
* arena-backed temporary objects
* serialization formats
* memory-mapped structures
* database-style slotted pages
* game-engine resource storage
* custom runtime or interpreter objects
* allocator and fragmentation experiments

A regular homogeneous C array will generally remain simpler and more efficient when every element has the same type.

---

## Roadmap

The project is currently being developed incrementally.

Planned work includes:

* [x] Raw byte storage
* [x] Variable-size objects
* [x] O(1) metadata lookup
* [x] Type-independent append
* [x] Alignment-aware placement
* [x] Padding calculation
* [x] Basic indexed access
* [ ] Clean up `get`
* [ ] Tombstone-based removal
* [ ] Free-chunk tracking
* [ ] Free-space reuse
* [ ] Explicit arena growth
* [ ] Alignment-aware repacking during growth
* [ ] Pointer invalidation policy
* [ ] Safe destruction / cleanup API
* [ ] Integer-overflow checks
* [ ] Better error handling
* [ ] Convenience macros / `_Generic`
* [ ] Tests
* [ ] Benchmarks
* [ ] Explore metadata + payload layout alternatives

---

## What I Learned

Some of the concepts encountered while building this include:

* `sizeof(T)` and `alignof(T)` describe different properties
* object size is implementation-dependent
* raw bytes do not retain C type information
* an address and byte count are enough to copy an object's representation
* direct typed access to raw storage requires correct alignment
* padding is part of memory layout
* logical position does not have to match physical position
* fixed-size metadata can provide O(1) lookup into variable-size storage
* offsets remain meaningful even when the underlying allocation moves
* moving packed objects requires updating metadata
* avoiding movement introduces fragmentation
* tombstones trade memory efficiency for pointer stability and cheap removal
* container growth introduces pointer invalidation
* free-space reuse begins to resemble allocator design

More detailed notes are available under [`docs/`](docs/).

---

## Motivation

This is primarily a recreational systems-programming project.

Rather than starting from an existing implementation, the goal is to discover the design constraints by building the structure incrementally:

```text
heterogeneous values
        ↓
variable-sized storage
        ↓
metadata + offsets
        ↓
alignment and padding
        ↓
removal
        ↓
fragmentation
        ↓
free-space reuse
        ↓
relocation and pointer invalidation
        ↓
allocator-like behavior
```

The project is intentionally still evolving as those tradeoffs are explored.
