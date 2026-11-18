// musl's exp2.c (third_party/musl_math, MIT) on its own, as musl builds it, in namespace det: lockstep's math
// (cpu/det_math.h)
#include "third_party/musl_math/prelude.h"

namespace det {
#include "third_party/musl_math/exp2.c"
}  // namespace det
