# System Manager Application

This directory contains sysmgr, an application which is responsible
for setting up an environment which provides access to global system
services.

This application runs quite early in the Fuchsia boot process. See the
[boot sequence](https://fuchsia.googlesource.com/fuchsia/+/master/docs/the-book/boot_sequence.md)
for more information.

sysmgr is designed to be fairly robust.  If any of the services
dies, they will be restarted automatically the next time an
application attempts to connect to that service.

By default, sysmgr reads all configuration files from `/system/data/sysmgr/`, which
have one of the following formats.

## CONFIGURATION

### Services

The sysmgr services configuration is a JSON file consisting of service
registrations.  Each entry in the "services" map consists of a service
name and the application URL which provides it.

    {
      "services": {
        "service-name-1": "app_without_args",
        "service-name-2": [
           "app_with_args", "arg1", "arg2", "arg3"
        ]
      }
    }

### Apps

The sysmgr apps configuration is a JSON file consisting of apps to run at
startup.  Each entry in the "apps" list consists of either an app URL or a list
of an app URL and its arguments.

    {
      "apps": [
        "app_without_args",
        [ "app_with_args", "arg1", "arg2", "arg3" ]
      ]
    }
