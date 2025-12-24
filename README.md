# loop-peel-with-state
## How to build
To build this project you need to build or install llvm first.

*Only version of llvm plugin was tested with is 22.0.0.*

Then to build
```
cmake -S . -B build -DLLVM_DIR=/path/to/your/lib/cmake/llvm
```
```
cmake --build build
```
*If you built llvm from source, then "your" in path would be **build** dir.*
## How to use
To use plugin with clang
```
clang -fpass-plugin=/path/to/LoopPeelWithState.so
```
Plugin will be at the loop-peel-with-state/build/ dir by default.
