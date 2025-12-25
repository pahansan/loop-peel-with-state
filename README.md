# loop-peel-with-state
## How to build
You first need an LLVM installation (or your own build from source).

**Note:** This plugin has only been tested with LLVM 22.0.0.

Configure the project:
```
cmake -S . -B build -DLLVM_DIR=/path/to/lib/cmake/llvm
```
Build it:
```
cmake --build build
```
If you built LLVM from source, `/path/to/lib/cmake/llvm` is located inside your LLVM build directory.
## How to use
Load the pass into Clang:
```
clang -fpass-plugin=/path/to/LoopPeelWithState.so
```
By default the shared library appears in `loop-peel-with-state/build/`
## Debugging
To see debug output you must compile LLVM with
```
-DCMAKE_BUILD_TYPE=RelWithDebInfo -DLLVM_ENABLE_ASSERTIONS=ON
```
Then invoke Clang as follows:
```
clang -fpass-plugin=$PWD/build/LoopPeelWithState.so \
      -mllvm -debug-only=loop-peel-with-state …
```
If you also need the source code to be printed, add the -g option as well.
