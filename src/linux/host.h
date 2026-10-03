/*
 * The Linux host's reusable pieces. main.c is the reference host: the REPL
 * and the ROM exporter with the engine's syscalls. A program can build its
 * own host from the same pieces by linking repl.c, sys.c, fmt.c and export.c
 * with its own main(), passing its extra syscalls in zfl_config.
 */

#ifndef ZF_LINUX_HOST_H
#define ZF_LINUX_HOST_H

#include <stddef.h>

#include "zforth.h"

/* Runs a syscall the host does not handle itself. Returns 0 if id is not
 * one of them. */
typedef int (*zfl_sys_fn)(zf_ctx *ctx, zf_syscall_id id);

typedef struct {
	/* A prebuilt dictionary to mount (from zforth -H), or NULL to
	 * bootstrap. Needs ZF_ENABLE_ROM_DICT. */
	const unsigned char *rom;
	size_t rom_len;
	size_t rom_data_len;
	/* The host program's own syscalls, or NULL */
	zfl_sys_fn sys;
	/* Called about every poll_ms while the REPL waits for input, or NULL
	 * for a plain REPL. The REPL still ends with its input. */
	void (*poll)(zf_ctx *ctx);
	int poll_ms;
} zfl_config;

/* repl.c: the command line, file and dictionary loading, the REPL */
int zfl_main(int argc, char **argv, const zfl_config *cfg);
/* Evaluates text, printing the reason if it aborts (src and line label
 * the message, src may be NULL) */
zf_result zfl_eval(zf_ctx *ctx, const char *src, int line, const char *text);
void zfl_include(zf_ctx *ctx, const char *fname);
void zfl_save(zf_ctx *ctx, const char *fname);

/* export.c: write the loaded dictionary as a C header (-H) */
void zfl_export_header(zf_ctx *ctx, const char *name);

/* sys.c: zf_host_sys() */
void zfl_set_sys(zfl_sys_fn fn);
/* Resolves a dictionary range from the stack, aborting if it is invalid */
const uint8_t *zfl_dict_range(zf_ctx *ctx, zf_cell addr, zf_cell len);

/* fmt.c: the fmt and fmt-buf syscalls */
void zfl_fmt(zf_ctx *ctx);
void zfl_fmt_buf(zf_ctx *ctx);

#endif
