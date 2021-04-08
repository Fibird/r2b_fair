# r2b

This repository contains C++ 11 code that implements the r2b fair scheduling algorithm.

## Running cmake

When running cmake, set the build type with either:

    -DCMAKE_BUILD_TYPE=Debug
    -DCMAKE_BUILD_TYPE=Release

To turn on profiling, run cmake with an additional:

    -DPROFILE=yes

An optimization/fix to the published algorithm has been added and is
on by default. To disable this optimization/fix run cmake with:

    -DDO_NOT_DELAY_TAG_CALC=yes

## Running make

### Building the dmclock library

The `make` command builds a library libdmclock.a. That plus the header
files in the src directory allow one to use the implementation in
their code.
