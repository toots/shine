#include "priv_types.h"

#if defined(__arm__)
#include "mult_sarm_gcc.h"
#else
#include "mult_noarch_gcc.h"
#endif
