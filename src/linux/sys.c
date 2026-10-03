/*
 * zf_host_sys(): the core syscalls, fmt and fmt-buf, and the Linux-only
 * development syscalls. Anything else goes to the host program's own
 * syscalls (zfl_config.sys).
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

#include "host.h"


const uint8_t *zfl_dict_range(zf_ctx *ctx, zf_cell addr, zf_cell len)
{
	if(addr < 0 || len < 0) {
		zf_abort(ctx, ZF_ABORT_OUTSIDE_MEM);
	}
	return (const uint8_t *)zf_dict_addr(ctx, (zf_addr)addr, (size_t)len);
}


static zfl_sys_fn user_sys = NULL;

void zfl_set_sys(zfl_sys_fn fn)
{
	user_sys = fn;
}

/* Milliseconds since an arbitrary start, like millis() on a device */
static uint64_t millis(void)
{
	static uint64_t start = 0;
	struct timespec ts;
	uint64_t now;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	now = (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
	if(start == 0) start = now;
	return now - start;
}

/*
 * Sys callback function
 */

zf_input_state zf_host_sys(zf_ctx *ctx, zf_syscall_id id, const char *input)
{
	switch((int)id) {


		/* The core system callbacks */

		case ZF_SYSCALL_EMIT:
			putchar((char)zf_pop(ctx));
			fflush(stdout);
			break;

		case ZF_SYSCALL_PRINT:
			printf(ZF_CELL_FMT " ", zf_pop(ctx));
			break;

		case ZF_SYSCALL_TELL: {
			zf_cell len = zf_pop(ctx);
			zf_cell addr = zf_pop(ctx);
			const void *buf = zfl_dict_range(ctx, addr, len);
			(void)fwrite(buf, 1, (size_t)len, stdout);
			fflush(stdout); }
			break;


		/* Application specific callbacks */

		case ZF_SYSCALL_USER + 0:
			printf("\n");
			exit(0);
			break;

		case ZF_SYSCALL_USER + 1:
			zf_push(ctx, zf_float_to_cell(sinf(zf_cell_to_float(zf_pop(ctx)))));
			break;

		case ZF_SYSCALL_USER + 2:
			if(input == NULL) {
				return ZF_INPUT_PASS_WORD;
			}
			zfl_include(ctx, input);
			break;
		
		case ZF_SYSCALL_USER + 3:
			zfl_save(ctx, "zforth.save");
			break;

		case ZF_SYSCALL_USER + 4:
			zfl_fmt(ctx);
			break;

		case ZF_SYSCALL_USER + 5:
			zfl_fmt_buf(ctx);
			break;

		case ZF_SYSCALL_USER + 6: {
			/* ms ( u -- ): delay for u milliseconds */
			zf_ucell ms = (zf_ucell)zf_pop(ctx);
			struct timespec ts;
			ts.tv_sec = ms / 1000u;
			ts.tv_nsec = (long)(ms % 1000u) * 1000000L;
			fflush(stdout);
			nanosleep(&ts, NULL);
			break;
		}

		case ZF_SYSCALL_USER + 7: {
			/* millis ( -- ud ): milliseconds since start, 64-bit */
			uint64_t v = millis();
			zf_push(ctx, (zf_cell)(zf_ucell)(v & 0xffffffffu));
			zf_push(ctx, (zf_cell)(zf_ucell)(v >> 32));
			break;
		}

		default:
			if(user_sys == NULL || !user_sys(ctx, id)) {
				printf("unhandled syscall %d\n", id);
			}
			break;
	}

	return ZF_INPUT_INTERPRET;
}
