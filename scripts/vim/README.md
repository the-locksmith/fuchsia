# Helpful Vim tools for Fuchsia development

## Features

* Configure YouCompleteMe to provide error checking, completion and source
  navigation within the Fuchsia tree.
* Set path so that `:find` and `gf` know how to find files.
* Fidl syntax highlighting (using /lib/fidl/tools/vim/).

## Installation

1. Update your login script

   Steps #2 and #3 depend on configuration set by the `fx set` command. Add
   these lines to your startup script (typically `~/.bashrc`).

   ```shell
   export FUCHSIA_DIR=/path/to/fuchsia-dir
   fx set x64
   ```

1. Update your vim startup file

   If this line exists in your ~/.vimrc file, remove it:

   ```
   filetype plugin indent on
   ```

   Then add these lines to your `~/.vimrc`.

   ```
   if $FUCHSIA_DIR != ""
     source $FUCHSIA_DIR/scripts/vim/fuchsia.vim
   endif
   filetype plugin indent on
   ```

1. Install YouCompleteMe (ycm)

   Optionally [install YouCompleteMe](
   https://fuchsia.googlesource.com/fuchsia/+/master/scripts/youcompleteme/README.md)
   for fancy completion, source navigation and inline errors.

   If it's installed, `fuchsia.vim` will configure it properly.

   If everything is working properly, you can place the cursor on an
   identifier in a .cc or .h file, hit Ctrl-], and YCM will take you
   to the definition of the identifier.

   If you build a compilation database, YCM will use it, which may be more
   reliable and efficient than the default `ycm_extra_config.py` configuration.
   Use `fx compdb` to build a compilation database.

## See also

[Zircon editor integration](
https://fuchsia.googlesource.com/fuchsia/+/master/zircon/docs/editors.md)

## TODO

In the future it would be nice to support:
* Fidl indentation
* GN indentation
* Dart, Go and Rust support
* Build system integration
* Navigate between generated files and fidl source
