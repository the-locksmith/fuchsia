# zx_clock_get_monotonic

## NAME

<!-- Updated by update-docs-from-abigen, do not edit. -->

Acquire the current monotonic time.

## SYNOPSIS

<!-- Updated by update-docs-from-abigen, do not edit. -->

```
#include <zircon/syscalls.h>

zx_time_t zx_clock_get_monotonic(void);
```

## DESCRIPTION

`zx_clock_get_monotonic()` returns the current time in the system
monotonic clock. This is the number of nanoseconds since the system was
powered on.

## RIGHTS

<!-- Updated by update-docs-from-abigen, do not edit. -->

TODO(ZX-2399)

## RETURN VALUE

[`zx_clock_get()`] returns the current monotonic time.

## ERRORS

`zx_clock_get_monotonic()` cannot fail.

<!-- References updated by update-docs-from-abigen, do not edit. -->

[`zx_clock_get()`]: clock_get.md
