// Copyright © 2019-2023
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef __VX_PQC_DEFS_H__
#define __VX_PQC_DEFS_H__

// Shared definitions for the PQC extension's kernel-side API.
//
// FIPS 203 / FIPS 204 constants are NOT mirrored here: mlkem-native and
// mldsa-native already define them, the tests build against those, and a third
// copy would be one more thing to drift. What belongs here is what the
// extension itself owns -- instruction encodings and the types its intrinsics
// exchange -- added as the ISA is designed.

#endif // __VX_PQC_DEFS_H__
