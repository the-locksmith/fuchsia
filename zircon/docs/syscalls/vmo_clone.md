# zx_vmo_clone

## NAME

<!-- Updated by update-docs-from-abigen, do not edit. -->

Create a clone of a VM Object.

## SYNOPSIS

<!-- Updated by update-docs-from-abigen, do not edit. -->

```
#include <zircon/syscalls.h>

zx_status_t zx_vmo_clone(zx_handle_t handle,
                         uint32_t options,
                         uint64_t offset,
                         uint64_t size,
                         zx_handle_t* out);
```

## DESCRIPTION

`zx_vmo_clone()` creates a new virtual memory object (VMO) that clones a range
of an existing vmo.

One handle is returned on success, representing an object with the requested
size.

*options* must contain **ZX_VMO_CLONE_COPY_ON_WRITE** and zero or more flags to control
clone creation.

Valid flags:

- **ZX_VMO_CLONE_COPY_ON_WRITE** - Create a copy-on-write clone. The cloned vmo will
behave the same way the parent does, except that any write operation on the clone
will bring in a copy of the page at the offset the write occurred. The new page in
the cloned vmo is now a copy and may diverge from the parent. Any reads from
ranges outside of the parent vmo's size will contain zeros, and writes will
allocate new zero filled pages.  See the NOTES section below for details on
VMO syscall interactions with clones.

- **ZX_VMO_CLONE_NON_RESIZEABLE** - Create a non-resizeable clone VMO.

*offset* must be page aligned.

*offset* + *size* may not exceed the range of a 64bit unsigned value.

Both offset and size may start or extend beyond the original VMO's size.

The size of the VMO will be rounded up to the next page size boundary.

By default the rights of the cloned handled will be the same as the
original with a few exceptions. See [`zx_vmo_create()`] for a
discussion of the details of each right.

If *options* is **ZX_VMO_CLONE_COPY_ON_WRITE** the following rights are added:

- **ZX_RIGHT_WRITE**

## NOTES

Cloning a VMO causes the existing (source) VMO **ZX_VMO_ZERO_CHILDREN** signal
to become inactive. Only when the last clone is destroyed and no mappings
of those clones into address spaces exist, will **ZX_VMO_ZERO_CHILDREN** become
active again.

### ZX_VMO_CLONE_COPY_ON_WRITE

VMOs produced by this mode will interact with the VMO syscalls in the following
ways:

- The DECOMMIT and COMMIT modes of [`zx_vmo_op_range()`] on a clone will only affect pages
  allocated to the clone, never its parent.
- If a page in a clone is decommitted (e.g. with [`zx_vmo_op_range()`]), the parent's page will
  become visible once again, still with copy-on-write semantics.
- If a page is committed to a clone using the [`zx_vmo_op_range()`] COMMIT mode, a
  the new page will have the same contents as the parent's corresponding page
  (or zero-filled if no such page exists).
- If the [`zx_vmo_op_range()`] LOOKUP mode is used, the parent's pages will be visible
  where the clone has not modified them.

## RIGHTS

<!-- Updated by update-docs-from-abigen, do not edit. -->

*handle* must be of type **ZX_OBJ_TYPE_VMO** and have **ZX_RIGHT_DUPLICATE** and have **ZX_RIGHT_READ**.

## RETURN VALUE

`zx_vmo_clone()` returns **ZX_OK** on success. In the event
of failure, a negative error value is returned.

## ERRORS

**ERR_BAD_TYPE**  Input handle is not a VMO.

**ZX_ERR_ACCESS_DENIED**  Input handle does not have sufficient rights.

**ZX_ERR_INVALID_ARGS**  *out* is an invalid pointer or NULL
or the offset is not page aligned.

**ZX_ERR_OUT_OF_RANGE**  *offset* + *size* is too large.

**ZX_ERR_NO_MEMORY**  Failure due to lack of memory.
There is no good way for userspace to handle this (unlikely) error.
In a future build this error will no longer occur.

## SEE ALSO

 - [`zx_vmar_map()`]
 - [`zx_vmo_create()`]
 - [`zx_vmo_get_size()`]
 - [`zx_vmo_op_range()`]
 - [`zx_vmo_read()`]
 - [`zx_vmo_set_size()`]
 - [`zx_vmo_write()`]

<!-- References updated by update-docs-from-abigen, do not edit. -->

[`zx_vmar_map()`]: vmar_map.md
[`zx_vmo_create()`]: vmo_create.md
[`zx_vmo_get_size()`]: vmo_get_size.md
[`zx_vmo_op_range()`]: vmo_op_range.md
[`zx_vmo_read()`]: vmo_read.md
[`zx_vmo_set_size()`]: vmo_set_size.md
[`zx_vmo_write()`]: vmo_write.md
