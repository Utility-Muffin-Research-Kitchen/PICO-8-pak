# PICO-8 for Leaf

You can browse Splore and play local PICO-8 carts natively on MLP1 with this
optional Leaf integration. You purchase [PICO-8 by Joseph White / Lexaloffle](https://www.lexaloffle.com/pico-8.php)
separately. The paid runtime and vendor artwork are not included.

This is an unreleased development integration. The `9999.0.0` minimum in both
metadata files deliberately blocks production store distribution until a Leaf
release with `native-pico8-v1` has been qualified and named. Do not lower it
just to bypass that release check. The wrapper also checks the daemon feature
and its per-launch protected-input flag.

Copy `pico8_64` and `pico8.dat` from the same **Raspberry Pi** download into
`BIOS/PICO8/` on your primary card. Install the integration as
`Apps/mlp1/PICO8.pak`. Open **Apps > PICO-8 for Leaf** for Splore, or choose
**PICO-8 (native)** through the existing **Core** picker for local carts.
FAKE-08 stays the default. If the native runtime is unavailable before a local
launch, the supporting launcher uses its normal FAKE-08 path and retains your
saved core choice. Native crashes or exits do not trigger a second emulator.

Your native configuration, progress, and Splore cache live in primary
`.userdata/mlp1/pico8/`, shared by both entry points and both cards. Captures
use primary `Recordings/PICO8/`. Native saves are separate from FAKE-08;
there is no migration or shared savestate format. Splore's local browser opens
primary `Roms/PICO8/`; direct Leaf launches can use either card. Explicit cart
file writes stay with the selected ROM source. No card libraries are merged.

Press **MENU** to show the return-to-Leaf prompt, then press **MENU** again
within four seconds to quit normally. Wait for the prompt to disappear to
keep playing. This also works in Splore with no cart selected.

Use **START** for PICO-8's native menu. Direct carts offer **Shutdown**;
Splore carts offer **Exit to Splore** and **Options > Shutdown PICO-8**.
The Splore browser also offers **Options > Shutdown PICO-8** for a selected
cart. Paired controllers and sleep/wake still require final qualification.
RetroArch shaders, savestates, rewind, achievements, and its Leaf menu do not
apply to native PICO-8.

Select **START > Favourite** in Splore to keep a game in Leaf. After you exit
Splore, Leaf imports downloaded favorites into primary `Roms/PICO8/Splore/`
and scans the library. New imports use native PICO-8 and your existing native
progress. Later core choices and Leaf favorites stay independent.

Unfavoriting keeps your copy. Newer versions already downloaded by Splore
update imported copies without changing their library identity. Edited files
are kept. Missing or incomplete downloads are retried after the next Splore
session. Deleting a copy while it remains a Splore favorite allows reimport.
Multi-cart games can still require additional downloads.

Supplied game titles keep their spelling. When Splore only supplies a BBS ID,
Leaf adds spaces and capitalization for its display name, keeping numbers.
You can set your own display name in Leaf; imports preserve that choice.

Removing or upgrading this pak leaves your purchased runtime and native home
alone. For an intentional reset, stop PICO-8 and back up the relevant file in
its native home before removing it. Upstream recreates its config and default
controller mapping file; the integration does not overwrite your edits.

## Build and check

You need Docker, make, and Python 3; `make check` also needs a host C compiler,
pkg-config, and libcurl, libpng, OpenSSL, and SQLite development headers. A sibling checkout and a purchased copy
of PICO-8 are not needed to build the integration:

```sh
make check
make package-mlp1
# With an existing local toolchain image:
make package-mlp1 TOOLCHAIN_IMAGE=ghcr.io/utility-muffin-research-kitchen/mlp1-toolchain:local
```

The package uses the firmware's SDL2, SDL2_ttf, libcurl, libpng, OpenSSL, zlib,
and SQLite. The importer keeps its ownership journal in native home as
`splore-imports.sqlite3`; leave that file in place to preserve safe updates.
Its small `wget`
adapter handles PICO-8's GET and POST-file invocations, using the system CA
bundle with TLS verification. No replacement shared libraries are bundled.
The adapter limits request bodies to 1 MiB and requests to 30 seconds.

`make dist-pakrat` refuses the development minimum. Before distribution, set
both metadata mirrors to the qualified supporting Leaf version, validate
CONTENT-1 against `leaf-contracts`, and complete
the device acceptance checks. Packaging uses an explicit file allowlist and
rejects vendor payloads and unexpected files.
The default toolchain image is pinned to a published multi-platform digest.

Native paths are resolved by Jawaka before it rebinds the per-cart source
environment. The integration consumes its `UMRK_PICO8_*` per-launch variables;
it never saves device mount paths into package metadata. `--check` is a bounded,
read-only wrapper preflight for Jawaka and never queries the waiting daemon.

The integration source is MIT licensed. SDL2 uses the zlib license,
SDL2_ttf uses the zlib license, and libcurl uses the curl license. These are
system dependencies, not bundled copies. API references:
[POST bodies](https://curl.se/libcurl/c/CURLOPT_POSTFIELDS.html) and
[TLS verification](https://curl.se/libcurl/c/CURLOPT_SSL_VERIFYPEER.html).
