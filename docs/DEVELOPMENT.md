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
.\build\windows-release\bin\Release\ders-arsivi.exe --version
.\build\windows-release\bin\Release\ders-arsivi.exe --info
```

Use `windows-debug` instead of `windows-release` for debugging.

## Troubleshooting

| Error | Fix |
| --- | --- |
| `cmake : The term 'cmake' is not recognized` | Open **Developer PowerShell for VS** instead of plain PowerShell. If it still fails, open Visual Studio Installer → Modify → select **C++ CMake tools for Windows**. |
| `Could not find toolchain file: /scripts/buildsystems/vcpkg.cmake` | `VCPKG_ROOT` is not set. Follow step 3 above, then reopen the terminal. |
| `could not find any instance of Visual Studio` | Install the **Desktop development with C++** workload. |

## Folder layout

| Folder | Contents |
| --- | --- |
| `core/` | C++ core library (`da_core.dll`). The only public contract: `core/include/da/da_api.h` |
| `core/tests/` | Core tests |
| `cli/` | Command-line tool (`ders-arsivi.exe`) |
| `unity/` | Unity front-end project (added in the second half of step 1) |
| `tools/` | Data protection check and git hooks |
| `docs/` | Development documentation |

## Rules

- **Business logic lives in the C++ core.** Unity only calls functions from `da_api.h` and displays the result.
- **When changing the C interface**, follow the contract rules at the top of `da_api.h`. Any backward-incompatible change increments `DA_ABI_VERSION`.
- **Real course files never enter the repository.** Test data lives outside the repo (e.g. `C:\DersArsivi-veri\`). Tests only use synthetic files under `core/tests/fixtures/`.
- **Everything in the repository is in English**: code, comments, documentation, commit messages and pull requests.
