# fo4CS

F4SE plugins for Fallout 4 that provide frame generation and upscaling support.

This repository can build two plugin targets:

- `FrameGen` — frame generation support.
- `Upscaler` — upscaling support.

## Requirements

### Build requirements

- Windows 11 or Windows 10 x64
- [Visual Studio Community 2022](https://visualstudio.microsoft.com/) with **Desktop development with C++**
- [CMake](https://cmake.org/)
- [Git](https://git-scm.com/downloads)
- [vcpkg](https://github.com/microsoft/vcpkg)
  - Set the `VCPKG_ROOT` environment variable to your vcpkg installation path.

### Runtime requirements

- Fallout 4 with F4SE.
- Address Library / matching CommonLibF4 runtime data for your target runtime flavor.
- For NVIDIA DLSS / DLSS Frame Generation, see [NVIDIA Streamline / DLSS Runtime Files](#nvidia-streamline--dlss-runtime-files).

## Runtime flavors

The project has separate CMake presets and build scripts for supported runtime families:

| Runtime flavor | CMake preset | Build script             |
| -------------- | ------------ | ------------------------ |
| Pre-NG         | `PreNG`      | `BuildReleasePreNG.bat`  |
| Post-NG        | `PostNG`     | `BuildReleasePostNG.bat` |
| Post-AE        | `PostAE`     | `BuildReleasePostAE.bat` |

Each build script builds both plugin targets by default. You can disable a target by setting an environment variable before running the script:

```bat
set FRAMEGEN=OFF && BuildReleasePostAE.bat
set UPSCALER=OFF && BuildReleasePostAE.bat
```

## Clone and build

Clone with submodules:

```bat
git clone <repo-url> --recursive
cd fo4CS
```

Build a release package for your runtime flavor:

```bat
BuildReleasePostAE.bat
```

Or run CMake directly:

```bat
cmake -S . --preset=PostAE -DFRAMEGEN=ON -DUPSCALER=ON
cmake --build build\PostAE --config Release --target package
```

The package zip is written to `dist/`.

## Package layout

The generated package installs files under the normal Fallout 4 `Data` layout. Important paths include:

```text
F4SE\Plugins\FrameGen.dll
F4SE\Plugins\FrameGeneration.ini
F4SE\Plugins\Upscaler.dll
F4SE\Plugins\Upscaler.ini
F4SE\Plugins\FrameGeneration\
F4SE\Plugins\Upscaler\
F4SE\Plugins\Streamline\
MCM\Config\FrameGen\
MCM\Config\Upscaler\
```

`F4SE\Plugins\Streamline\` is a shared runtime dependency folder used by both FrameGen and Upscaler.

## NVIDIA Streamline / DLSS Runtime Files

NVIDIA Streamline and DLSS DLLs are shared by Frame Generation and Upscaling, but are **not distributed** with this package. This is intentional: the project must not redistribute NVIDIA Streamline / DLSS runtime binaries unless redistribution rights are explicitly confirmed under NVIDIA's current terms.

After installing the mod, place the required NVIDIA DLLs in this shared runtime folder:

```text
Data\F4SE\Plugins\Streamline\
```

The plugins search this shared folder first. Legacy fallback folders under `Data\F4SE\Plugins\FrameGeneration\Streamline\` and `Data\F4SE\Plugins\Upscaling\Streamline\` are still supported, but new installations should use the shared folder above.

### Required DLLs

| File                | Required for          | Source                                                                          |
| ------------------- | --------------------- | ------------------------------------------------------------------------------- |
| `sl.interposer.dll` | DLSS / DLSS-G         | [NVIDIA Streamline SDK](https://github.com/NVIDIAGameWorks/Streamline)          |
| `sl.common.dll`     | DLSS / DLSS-G         | NVIDIA Streamline SDK                                                           |
| `sl.dlss.dll`       | DLSS Upscaling        | NVIDIA Streamline SDK                                                           |
| `sl.dlss_g.dll`     | DLSS Frame Generation | NVIDIA Streamline SDK                                                           |
| `sl.reflex.dll`     | DLSS Frame Generation | NVIDIA Streamline SDK                                                           |
| `sl.pcl.dll`        | DLSS Frame Generation | NVIDIA Streamline SDK                                                           |
| `nvngx_dlss.dll`    | DLSS Upscaling        | NVIDIA driver / NVIDIA DLSS runtime                                             |
| `nvngx_dlssg.dll`   | DLSS Frame Generation | NVIDIA driver installation, commonly `C:\Windows\System32` on supported systems |

The Streamline SDK DLLs must be obtained from the [NVIDIA Streamline SDK releases](https://github.com/NVIDIAGameWorks/Streamline/releases) according to NVIDIA's current access and redistribution terms.

The generated package includes `F4SE\Plugins\Streamline\README.txt` as an installation reminder, but it does not include the NVIDIA DLLs themselves.

## License

[GPL-3.0-or-later](COPYING) WITH [Modding Exception AND GPL-3.0 Linking Exception (with Corresponding Source)](EXCEPTIONS.md).

See individual third-party dependency licenses where applicable.
