# Development

How to build and run the project on your machine.

## One-time setup

1. Install Visual Studio 2022 or newer with the **"Desktop development with C++"** workload.
   Make sure **"C++ CMake tools for Windows"** is selected (it is part of the workload by default).
2. Install Git.
3. Install vcpkg and set its path as an environment variable:

   ```powershell
   git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
   C:\vcpkg\bootstrap-vcpkg.bat
   setx VCPKG_ROOT C:\vcpkg
   ```

   Close and reopen your terminal after `setx`.

4. Enable the data protection check (inside the repository folder):

   ```powershell
   git config core.hooksPath tools/hooks
   ```

   From now on every `git commit` checks the staged files. The commit is stopped if a photo, PDF,
   label file, model file or API key tries to get in.

## Which terminal?

Use **Developer PowerShell for VS** (search for it in the Start menu). It puts CMake and the
MSVC compiler on the `PATH`. A plain PowerShell window usually cannot find `cmake`.

Check that everything is visible:

```powershell
cmake --version
echo $env:VCPKG_ROOT
```

Both commands must print something.

## Build and test

```powershell
cmake --preset windows-release
cmake --build --preset windows-release
ctest --preset windows-release
```

Verify it works:

```powershell
.\build\windows-release\bin\Release\summarizer.exe --version
.\build\windows-release\bin\Release\summarizer.exe --info
```

Use `windows-debug` instead of `windows-release` for debugging.

## Unity front end

The core build copies `smz_core.dll` into `unity/Assets/Plugins/x86_64/` automatically.
The `unity/` folder must exist when you run `cmake --preset ...`, so configure again after
cloning or after the Unity project is first added.

1. Build the core (see above).
2. In Unity Hub, choose **Add → Add project from disk** and select the `unity/` folder.
3. Open `Assets/Scenes/SampleScene` and press **Play**.
4. The Game view shows "C++ core loaded" and the core version. If it shows an error instead,
   the message says what to do.

**Important:** the Unity Editor keeps `smz_core.dll` loaded until it is closed. Close Unity before
rebuilding the core, otherwise the copy step fails because the file is in use.

## Troubleshooting

| Error | Fix |
| --- | --- |
| `cmake : The term 'cmake' is not recognized` | Open **Developer PowerShell for VS** instead of plain PowerShell. If it still fails, open Visual Studio Installer → Modify → select **C++ CMake tools for Windows**. |
| `Could not find toolchain file: /scripts/buildsystems/vcpkg.cmake` | `VCPKG_ROOT` is not set. Follow step 3 above, then reopen the terminal. |
| `could not find any instance of Visual Studio` | Install the **Desktop development with C++** workload. |
| Unity: `smz_core.dll was not found` | Build the core, then restart Unity. If `unity/Assets/Plugins/x86_64/` is still empty, run `cmake --preset windows-release` again (the `unity/` folder must exist at configure time). |
| Build: copying `smz_core.dll` fails (file in use) | Close Unity, then build again. |

## Folder layout

| Folder | Contents |
| --- | --- |
| `core/` | C++ core library (`smz_core.dll`). The only public contract: `core/include/smz/smz_api.h` |
| `core/tests/` | Core tests |
| `cli/` | Command-line tool (`summarizer.exe`) |
| `unity/` | Unity front-end project. C# code lives in `unity/Assets/Scripts/` |
| `unity/Assets/Scripts/Core/` | `NativeCore.cs`: the C# layer over `smz_api.h` |
| `tools/` | Data protection check and git hooks |
| `docs/` | Development documentation |

## Rules

- **Business logic lives in the C++ core.** Unity only calls functions from `smz_api.h` and displays the result.
- **When changing the C interface**, follow the contract rules at the top of `smz_api.h`. Any backward-incompatible change increments `SMZ_ABI_VERSION`.
- **Real course files never enter the repository.** Test data lives in any folder outside the repository. Tests only use synthetic files under `core/tests/fixtures/`.
- **Everything in the repository is in English**: code, comments, documentation, commit messages and pull requests.
