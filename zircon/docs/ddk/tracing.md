# Adding tracing to device drivers

This document describes how to add tracing to device drivers.

## Overview

Please read [Fuchsia Tracing System Design](../tracing/design.md)
for an overview of tracing.

## Trace Provider

Drivers don't have to specify a Trace Provider, the devhost process
via `libdriver.so` provides it. It is mentioned here in case the topic
comes up.

## Adding trace records

### Source additions

Trace records are easiest to add by invoking the `TRACE_*()` macros
from `ddk/trace/event.h`.

There are various kinds of trace records that can be emitted.
Please see `trace/internal/event_common.h` for a description
of the various macros.

Looking up macro documentation from internal implementation files
is a temporary situation. Ultimately such documentation will live
in a more appropriate place.

Example:

```c++
#include <ddk/trace/event.h>

void DoSomething(int a, std::string b) {
  TRACE_DURATION("example:example1", "DoSomething", "a", a, "b", b);

  // Do something
}
```

The first two arguments to most macros are the "category" and the
event name. Here they are "example:example1" and "DoSomething" respectively.

Trace categories are how the tracing system lets the user specify
what data to collect. If a category is not requested by the user
then the data is not collected.

Categories don't need to be unique across the driver.
One typically groups several events under the same category.
By convention categories have the format
"<provider-name>:<category-name>[:<subcategory1-name>...]".
"<provider-name>" for drivers should generally by the driver name.
This is done to avoid collisions in category names across the
entire system. A potential augmentation to this convention is to prefix
all driver categories with "driver:". E.g., "driver:ethernet:packets".
Avoiding naming collisions with other trace providers is important,
otherwise the user may ask for a particular category and get completely
unrelated data from a different trace provider.

The event name is included in the trace to describe what the event
is about. It is typically unique for each event.

### BUILD.gn additions

The following addition to your driver's `BUILD.gn` target is needed to
pick up tracing support:

```gn
driver("my_driver") {
  data_deps += [ "$zx/system/ulib/trace.driver" ]
  public_deps += [ $zx/system/ulib/trace system/ulib/trace-engine" ]
}
```

## Booting with tracing

To be conservative, tracing uses a kernel command line flag to enable it:
`driver.tracing.enable=1`.
`driver.tracing.enable=1` is the default. To disable partipation
of drivers in Fuchsia tracing, boot the kernel with `driver.tracing.enable=0`.

Example:

First build:

```sh
$ fx set $arch
$ fx build-zircon
$ fx build
```

Then boot. With QEMU:

```sh
$ fx run -k -N
```

Or on h/w (augment with options specific to your h/w):

```sh
$ fx serve
```

These extra requirements will be removed once tracing support stabilizes.

## Using tracing

Once the system is booted you can collect traces on the target and
then manually copy them to your development host.
These examples use the category from the source additions described above.

Example:

```sh
fuchsia$ trace record --categories=example:example1
host$ fx cp --to-host /data/trace.json trace.json
```

However, it's easier to invoke the `traceutil` program on your development
host and it will copy the files directly to your host and prepare them for
viewing with the Chrome trace viewer.

```sh
host$ fx traceutil record --categories=example:example1
```

See the [Tracing Usage Guide](https://fuchsia.googlesource.com/fuchsia/+/master/garnet/docs/tracing_usage_guide.md)
for further info.
