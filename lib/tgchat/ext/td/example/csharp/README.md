# TDLib C# example

This is an example of building TDLib with `C++/CLI` support and an example of TDLib usage from C#.

## Building TDLib

* Download and install Microsoft Visual Studio 2015 or later.
* Download and install [CMake](https://cmake.org/download/); choose "Add CMake to the system PATH" option while installing.
* Install [vcpkg](https://github.com/Microsoft/vcpkg#quick-start) or update it to the latest version using `vcpkg update` and following received instructions.
* Install `zlib` and `openssl` for using `vcpkg`:
```
cd <path to vcpkg>
.\vcpkg.exe install openssl:x64-windows openssl:x86-windows zlib:x64-windows zlib:x86-windows
```
* (Optional. For XML documentation generation.) Download [PHP](https://windows.php.net/download#php-7.2). Add the path to php.exe to the PATH environment variable.
* Download and install [gperf](https://sourceforge.net/projects/gnuwin32/files/gperf/3.0.1/). Add the path to gperf.exe to the PATH environment variable.
* Build `TDLib` with CMake enabling `.NET` support and specifying correct path to `vcpkg` toolchain file:
```
cd <path to TDLib sources>/example/csharp
mkdir build
cd build
cmake -DTD_ENABLE_DOTNET=ON -DCMAKE_TOOLCHAIN_FILE=<path to vcpkg>\scripts\buildsystems\vcpkg.cmake ../../..
cmake --build . --config Release
cmake --build . --config Debug
```

## Example of usage

After `TDLib` is built you can open and run TdExample project.
It contains a simple console C# application with implementation of authorization and message sending.
Just open it with Visual Studio 2015 or 2017 and run.

Also see TdExample.csproj for example of including TDLib in C# project with all native shared library dependencies.
