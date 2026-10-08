# Build

Back to the [README](../README.md).

Building turns the source code into a program. Run these commands in the project folder.

## Build modes

There are two modes. Pick one.

| Mode | Use it for | Build command | Program is in |
| --- | --- | --- | --- |
| Release | Everyday use. Fastest, smallest, no debug tools | `xmake f -m release` then `xmake` | `bin/release/` |
| Debug | Developers. Adds a debug window, render diagnostics and the effect sandbox | `xmake f -m debug` then `xmake` | `bin/debug/` |

## Good to know

| Topic | What to know |
| --- | --- |
| Debug-only options | `--sandbox`, `--diagnose` and the rest are only listed in a debug build's help. Use a debug build for them |
| Effect sandbox | Run `xmake sandbox`. It builds the debug build and starts the sandbox |
| Diagnostics | They save output to a folder, and they can be slow. Use them when you are finding a bug |
| Switch back | Run `xmake f -m release` again, then `xmake` |
| What each command does | `xmake f -m debug` only saves the mode. It does not compile. `xmake` does the compiling |
| Set the mode once | The mode is saved, so after `xmake f -m debug` you only need `xmake` to rebuild |

## More

| I want to... | Read |
| --- | --- |
| See every option | [Options](options.md) |
| Run the tests and the checks | [Development](development.md) |
