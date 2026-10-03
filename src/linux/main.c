/*
 * The reference Linux host: the REPL and the ROM exporter with the engine's
 * syscalls (see host.h). Built with -DZF_LINUX_ROM_DICT=1, it mounts a
 * prebuilt dictionary instead of bootstrapping.
 */

#include <stdio.h>

#include "host.h"

#if ZF_LINUX_ROM_DICT
/* -DZF_DICT_HEADER='"path/image.h"' selects another prebuilt image; the
 * default "zforth_dict.h" is found next to this file first */
#ifdef ZF_DICT_HEADER
#include ZF_DICT_HEADER
#else
#include "zforth_dict.h"
#endif
#endif

int main(int argc, char **argv)
{
	zfl_config cfg = { NULL, 0, 0, NULL };
#if ZF_LINUX_ROM_DICT
	cfg.rom = zforth_dict;
	cfg.rom_len = zforth_dict_len;
	cfg.rom_data_len = zforth_dict_data_len;
#endif
	return zfl_main(argc, argv, &cfg);
}
