/* See COPYING.txt for license details. */

#ifndef M1_T1000_VERSION_H_
#define M1_T1000_VERSION_H_

#include "m1_fw_update_bl.h"

#define T1000_VERSION_MAJOR        0
#define T1000_VERSION_MINOR        1
#define T1000_VERSION_PATCH        2

#define T1000_STR_HELPER(x)        #x
#define T1000_STR(x)               T1000_STR_HELPER(x)

#define T1000_VERSION_TAG \
    T1000_STR(T1000_VERSION_MAJOR) "." \
    T1000_STR(T1000_VERSION_MINOR) "." \
    T1000_STR(T1000_VERSION_PATCH)

#define T1000_VERSION_STRING       "v" T1000_VERSION_TAG
#define T1000_COMPAT_VERSION_STRING \
    "Compat " \
    T1000_STR(FW_VERSION_MAJOR) "." \
    T1000_STR(FW_VERSION_MINOR) "." \
    T1000_STR(FW_VERSION_BUILD) "." \
    T1000_STR(FW_VERSION_RC) "-C3." T1000_STR(M1_C3_REVISION)

#endif /* M1_T1000_VERSION_H_ */
