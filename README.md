# MakosizerX

**Lo-fi mastering suite** — vinyl, tape, noise, crush and limit in one VST3 / AU plugin.
Built with [JUCE](https://juce.com/) 8.

## Controls

| Section | Control | What it does |
| --- | --- | --- |
| Main | **Destroy** | Macro: pushes Drive, Vinyl, Wow, Flutter, Noise, Dust and Crush towards their maximum together |
| Main | **Drive** | `tanh` saturation (0–30 dB) |
| Main | **Vinyl Sim** | Rolls off the highs and adds soft saturation |
| Main | **Noise / Dust** | Hiss and random crackle |
| Main | **Wow / Flutter** | Slow and fast pitch drift (modulated delay) |
| Main | **Crush** | Bit-depth reduction (16 → 4 bit) |
| Main | **Comp** | Fixed-threshold compressor, ratio 1:1 – 8:1 |
| Filter | **HPF / LPF** | One-pole high-pass (20 Hz – 1 kHz) and low-pass (1 – 20 kHz) |
| Limiter | **Ceiling** | Soft limiter |
| Stereo | **Width / Mix** | Mid/side width (0–200 %) and dry/wet |
| Quality | **Oversampling** | 1x / 2x / 4x / 8x for the non-linear processing (see below) |

### Oversampling and latency

The whole processing chain runs at 2x, 4x or 8x the host sample rate when
oversampling is enabled. Half-band FIR filters (65 taps, Kaiser window) are used for
up- and down-sampling, which keeps saturation and crushing from folding back as
aliasing. The dry path goes through the same filters, so Mix stays phase-aligned.

The plugin always reports its latency to the host: 5 ms for the wow/flutter delay
line, plus 32 / 48 / 56 samples (at the host rate) of filter delay at 2x / 4x / 8x.
Oversampled modes roll off slightly above ~21 kHz (about −1 dB at 22 kHz at a 48 kHz
host rate).

Changing the oversampling factor while playing clears the plugin's internal state and
may cause a short click.

## Presets

Eight factory presets (INIT, SP303 VIBES, DILLA DUST, MPC CRUNCH, CASSETTE MELT,
LOFI RADIO, UNDERGROUND, DIRTY MASTER) and a CUSTOM slot. Presets can be saved to and
loaded from `.makopreset` files with the SAVE / LOAD buttons.

## Building

Requirements: CMake 3.22+, a C++17 compiler. JUCE 8.0.6 is downloaded automatically.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target MakosizerX_VST3
```

The bundle ends up in `build/MakosizerX_artefacts/Release/VST3/`.
Other targets: `MakosizerX_Standalone`, and `MakosizerX_AU` on macOS.

### DSP tests

The DSP core (`DSP/`) has no JUCE dependency and has its own tests (latency
reporting, aliasing, noise level, robustness):

```bash
cmake -S tests -B build-tests -DCMAKE_BUILD_TYPE=Release
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

## Releases

Push a tag such as `v1.0.1`; GitHub Actions builds the Windows VST3 + installer and the
macOS VST3 / AU + installer and attaches them to a GitHub Release.
Prebuilt binaries are not stored in the repository.

## Layout

```
DSP/        signal processing (no JUCE)
GUI/        LCD display, LED meter, knob look-and-feel
Source/     plugin processor and editor
Assets/     background artwork
tests/      DSP tests
packaging/  Windows (Inno Setup) and macOS (pkgbuild) installers
```

## License

See [LICENSE](LICENSE).
