#ifndef ZFCONF_LINUX_H
#define ZFCONF_LINUX_H

#define ZF_ENABLE_TRACE 1
#define ZF_ENABLE_BOOTSTRAP 1

/* No dictionary limit: the -H exporter builds the whole image in RAM */
#ifndef ZF_DICT_MAX_SIZE
#define ZF_DICT_MAX_SIZE 0
#endif

#include "../zfconf_common.h"

#endif
