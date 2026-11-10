# VST3-renku

> This repository is the source of the `vst3_renku` and `vst3_renku_jack` packages for RenkuOS. Built
> packages are on its [Releases](https://github.com/rations/VST3-renku/releases) page, and
> [renku-audio](https://github.com/rations/renku-audio) lists the whole audio stack.

A native port of the [Steinberg VST 3 SDK](https://github.com/steinbergmedia/vst3sdk)
(3.8.0, MIT-licensed) to [RenkuOS](https://github.com/RenkuOS), added as a first-class platform (`SMTG_OS_HAIKU`)
rather than a Linux masquerade.

This is part of a native RenkuOS pro-audio stack: a multitrack DAW ([Kasen](https://github.com/rations/kasen)) that runs on
the native Media Kit or on a full [JACK2 port](https://github.com/rations/jack-port-renku),
a graphical patchbay, and — with this repository — VST3 plugin hosting and plugins.

VST3 plug-ins never talk to an audio system themselves: the host hands them buffers. A
plug-in built against this SDK therefore runs in Kasen (either backend) and in the
standalone `vst3host` here, with no JACK needed anywhere.

## What's here

- `vst3sdk/` — the vendored SDK with the port's patches applied in place at the canonical
  upstream file locations, so the whole port is one reviewable diff against the
  pristine 3.8.0 import commit. Key additions:
  - `pluginterfaces/base/fplatform.h`: `__HAIKU__` platform branch (`SMTG_OS_HAIKU`)
  - `public.sdk/source/vst/hosting/module_haiku.cpp`: module loading with native
    application-path lookup and plugin search via `find_paths()` add-on directories
  - CMake: `SMTG_HAIKU` platform detection and `Contents/x86_64-haiku/` bundle layout
  - `public.sdk/samples/vst-hosting/audiohost/source/media/jdbackend/`: a media server for
    the SDK's audiohost sample, beside its JACK one, on Kasen's audio backends (the
    Media Kit, or JACK through an add-on). On RenkuOS the audiohost sample runs on it.
- `hosts/vst3host/` — a headless VST3 host with an interactive parameter REPL (list
  parameters, set values live) and the plug-in's native editor window, built twice:
  - `vst3host` on the native Media Kit, or on JACK with `--backend jack` (the `jack.so`
    add-on, so `vst3host` itself never needs libjack). On the Media Kit the plug-in becomes
    one media node named after it and its first two outputs go to the system mixer; on JACK
    they go to the first two playback ports. Inputs are connected with `--in <source>` or the
    `connect` command (on the Media Kit, a source patched to the node in Cortex shows up in
    `inputs` and still has to be connected). MIDI inputs are Midi Kit endpoints
    (`--midi-in`, `midiconnect`, or PatchBay) or JACK MIDI ports. The buffer size can
    change while it runs.
  - `vst3jackhost` on the SDK's own JACK client, built only when JACK is installed.
- `haiku/jdbackend/` — the audio backends behind `vst3host`, the audiohost sample and
  the NAMku and DRUMku apps: Kasen's Media Kit backend, JACK backend and the loader that finds
  the JACK add-on, copied unchanged from [Kasen](https://github.com/rations/kasen)
  by `tools/sync-jdbackend.sh` (see `haiku/jdbackend/PROVENANCE`). Fix them there, then sync.
  `haiku/jdbackend.cmake` builds them (`jdbackend`, and `jack.so` when JACK is installed)
  for this project and for plug-in projects that build against this SDK.

Plug-ins built on this port live in their own repositories —
[NAMku](https://github.com/rations/NAMku), a Neural Amp Modeler plugin written directly
against the SDK (no framework), is built automatically when checked out as a sibling
directory of this one (see the `NAMKU_DIR` cache variable).

Plugin bundles use `<name>.vst3/Contents/x86_64-haiku/<name>.so` and are searched in the
canonical add-on directory `add-ons/media/VST3` across every install root
(`~/config/non-packaged/add-ons/media/VST3` and the packaged/system equivalents). No
dot-folders — RenkuOS organises per-user files under the `~/config` hierarchy.

## Status

Work in progress. Milestones: SDK core libraries + `validator` running natively → SDK
sample plugins (adelay, mda suite) passing validation → live JACK audio through
vst3jackhost → NAMku → FX chains in Kasen → plug-in editors. A plug-in that ships a native
editor (an IPlugView on a BView, as NAMku and DRUMku do) shows it in `vst3host` and in Kasen;
the others get a generic parameter window.

## Building (on RenkuOS)

Requires RenkuOS x86_64, gcc 13, cmake ≥ 3.25, ninja. JACK is optional (only
for `vst3jackhost`).

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build
```

or use the convenience `Makefile` wrapper: `make configure build`.

## Packaging (RenkuOS)

Build the packages with `packaging/make-hpkg.sh`:
`vst3_renku-0.4.0-3-x86_64.hpkg` (`vst3host` plus the SDK example plug-ins; needs no
JACK; replaces the old `vst3_haiku` package) and, when JACK is installed,
`vst3_renku_jack-0.4.0-3-x86_64.hpkg` (`vst3jackhost`). Or install from source with
`build-from-source.sh`. See the [stack overview](https://github.com/rations/renku-audio).

## License

MIT — see [LICENSE](LICENSE). The vendored VST 3 SDK is MIT-licensed by Steinberg Media
Technologies GmbH (see `vst3sdk/LICENSE.txt`). "VST" is a trademark of Steinberg Media
Technologies GmbH; this project is an unofficial community port and is not affiliated with
or endorsed by Steinberg.
