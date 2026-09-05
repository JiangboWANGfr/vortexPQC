// Copyright © 2019-2023
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

// mlkem-native is a single translation unit, and this pulls it into the kernel
// image without copying or patching a line of it.
//
// The wrapper exists because the harness compiles every VX_SRCS entry with
// clang++, which rejects a .c input outright ("treating 'c' input as 'c++' ...
// is deprecated", fatal under -Werror). Including it from a .cpp sidesteps
// that; the extern "C" keeps the definitions unmangled so they match the
// declarations in mlkem_native.h, which is already extern "C" for C++ callers.
//
// Verified: the library compiles clean as both C and C++ under the kernel's
// -Wall -Wextra -Werror.

extern "C" {
#include "mlkem_native.c"
}
