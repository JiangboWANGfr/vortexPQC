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

`ifndef VX_PQC_PKG_VH
`define VX_PQC_PKG_VH

`include "VX_define.vh"

// Scheme parameters (ring degree, the two moduli, coefficient widths) belong
// here, but only once something reads them: the build treats UNUSEDPARAM as an
// error, and a constant declared ahead of its consumer has to be lint-waived,
// which is just a warning-suppressed placeholder. Add each one with the module
// that uses it.
package VX_pqc_pkg;

    import VX_gpu_pkg::*;

endpackage

`endif // VX_PQC_PKG_VH
