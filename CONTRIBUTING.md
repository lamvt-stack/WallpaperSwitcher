# Contributing

## Ground rules

- **Win32, Unicode only.** Call the explicit `...W` APIs; `win_util.h` refuses to compile without `UNICODE`.
- **Own every resource.** Use the RAII types in `src/win_util.h` (`Unique*`, `ComPtr`, `CoString`) - no bare
  `CloseHandle` / `DeleteObject` / `Release` pairs.
- **Check every call.** Wrap Win32 calls in `Check(...)` and HRESULTs in `Succeeded(...)` (they log to the
  debugger). Read `GetLastError()` immediately after the failing call.
- **No exceptions across OS callbacks.** Window procedures, `EnumWindows` callbacks and thread entry points catch
  everything.
- **No `sprintf`-family or `wsprintf`.** Use `Format()` (`StringCchVPrintfW`) and `%ls` for wide strings.
- **Logic goes in `src/core/`** when it can be written without Windows types, together with a test.

## Building and testing

```bash
cmake -S . -B build-tests -DWS_BUILD_APP=OFF
cmake --build build-tests && ctest --test-dir build-tests --output-on-failure
```

CI builds the portable tests on Linux, and the app with MSVC and MinGW-w64.

## Pull requests

Keep changes focused, add or update tests for behaviour changes in `src/core/`, and describe any manual testing
on real multi-monitor setups (monitor layouts and Windows version).
