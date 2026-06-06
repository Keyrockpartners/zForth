#ifndef zforth_h
#define zforth_h

#ifdef __cplusplus
extern "C"
{
#endif

#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>
#include <setjmp.h>

#include "zfconf.h"

#ifndef ZF_ENABLE_DYNAMIC_DICT
#define ZF_ENABLE_DYNAMIC_DICT 0
#endif

#ifndef ZF_ENABLE_ROM_DICT
#define ZF_ENABLE_ROM_DICT 0
#endif

/* Abort reasons */

typedef enum {
	ZF_OK,
	ZF_ABORT_INTERNAL_ERROR,
	ZF_ABORT_OUTSIDE_MEM,
	ZF_ABORT_DSTACK_UNDERRUN,
	ZF_ABORT_DSTACK_OVERRUN,
	ZF_ABORT_RSTACK_UNDERRUN,
	ZF_ABORT_RSTACK_OVERRUN,
	ZF_ABORT_NOT_A_WORD,
	ZF_ABORT_COMPILE_ONLY_WORD,
	ZF_ABORT_INVALID_SIZE,
	ZF_ABORT_DIVISION_BY_ZERO,
	ZF_ABORT_INVALID_USERVAR,
	ZF_ABORT_EXTERNAL,
	ZF_ABORT_INVALID_CELL
} zf_result;

typedef enum {
	ZF_INPUT_INTERPRET,
	ZF_INPUT_PASS_CHAR,
	ZF_INPUT_PASS_WORD
} zf_input_state;

typedef enum {
	ZF_SYSCALL_EMIT,
	ZF_SYSCALL_PRINT,
	ZF_SYSCALL_TELL,
	ZF_SYSCALL_USER = 128,
	ZF_SYSCALL_OS_TO_STR = ZF_SYSCALL_USER + 16,
	ZF_SYSCALL_OS_FROM_STR,
	ZF_SYSCALL_OS_CALL,
	ZF_SYSCALL_OS_SEND,
	ZF_SYSCALL_OS_GC,
	ZF_SYSCALL_OS_TYPE
} zf_syscall_id;

#if ZFORTH_EXT_OS_OBJECTS
#if !defined(__STDC_IEC_559__) && !defined(__cplusplus)
/* Most embedded toolchains do not define __STDC_IEC_559__ even for IEEE-754
 * doubles, so the hard check below is sizeof-based. This warning documents the
 * representation assumption for less strict compilers. */
#endif
#if !defined(ZF_CELL_TYPE) || !defined(ZF_SCAN_FMT)
#error "ZFORTH_EXT_OS_OBJECTS requires the shared zfconf path with zf_cell configured as double"
#endif
typedef char zf_ext_objects_require_64_bit_cell[(sizeof(zf_cell) == sizeof(uint64_t)) ? 1 : -1];
typedef uint32_t zf_ext_id;
typedef uint8_t zf_ext_kind;
struct zf_ctx;
typedef struct zf_ctx zf_ctx;
typedef struct {
	zf_addr addr;
	zf_cell cell;
} zf_ext_dict_owner;
typedef void (*zf_ext_root_cb)(zf_ctx *ctx, zf_cell cell, void *user);
#endif

typedef enum {
    ZF_USERVAR_HERE = 0,
    ZF_USERVAR_LATEST,
    ZF_USERVAR_TRACE,
    ZF_USERVAR_COMPILING,
    ZF_USERVAR_POSTPONE,
    ZF_USERVAR_DSP,
    ZF_USERVAR_RSP,

    ZF_USERVAR_COUNT
} zf_uservar_id;


typedef struct zf_ctx {
	/* Stacks and dictionary memory */
	zf_cell rstack[ZF_RSTACK_SIZE];
	zf_cell dstack[ZF_DSTACK_SIZE];
	#if ZF_ENABLE_DYNAMIC_DICT
	uint8_t *dict;
	size_t dict_cap;
	#else
	uint8_t dict[ZF_DICT_SIZE];
	#endif
#if ZF_ENABLE_ROM_DICT
	zf_addr uservars[ZF_USERVAR_COUNT];
#endif
	zf_addr data_base;
	zf_addr data_len;
	zf_addr data_here;
	uint8_t data_compile;
#if ZF_ENABLE_ROM_DICT
	const uint8_t *rom_dict;
	zf_addr rom_len;
	zf_addr dict_base;
#endif

	/* State and stack and interpreter pointers */
	zf_input_state input_state;
	zf_addr ip;

	/* setjmp env for handling aborts */
	jmp_buf jmpbuf;
	int abort_jmp_valid;
	zf_result abort_reason;

	/* Input buffer */
	char read_buf[32];
	size_t read_len;

	/* Name buffer */
	char name_buf[32];

#if ZFORTH_EXT_OS_OBJECTS
	zf_ext_dict_owner ext_owners[ZF_EXT_DICT_OWNERS_MAX];
	uint16_t ext_owner_count;
#endif

} zf_ctx;


/* True is defined as the bitwise complement of false. */

#define ZF_FALSE ((zf_cell)0)
#define ZF_TRUE ((zf_cell)~(zf_int)ZF_FALSE)

/* ZForth API functions */

zf_result zf_init_checked(zf_ctx *ctx, int trace);
void zf_init(zf_ctx *ctx, int trace);
void zf_free(zf_ctx *ctx);
void zf_bootstrap(zf_ctx *ctx);
void *zf_dump(zf_ctx *ctx, size_t *len);
const void *zf_dict_addr(zf_ctx *ctx, zf_addr addr, size_t len);
void zf_dict_write_bytes(zf_ctx *ctx, zf_addr addr, const void *buf, size_t len);
void zf_dict_set_data_compile(zf_ctx *ctx, int enable);
size_t zf_dict_data_size(zf_ctx *ctx);
size_t zf_dict_size(zf_ctx *ctx);
size_t zf_dict_capacity(zf_ctx *ctx);
zf_result zf_dict_import(zf_ctx *ctx, const void *buf, size_t len);
zf_result zf_dict_import_with_data(zf_ctx *ctx, const void *buf, size_t len, size_t data_len);
zf_result zf_dict_mount_rom(zf_ctx *ctx, const void *buf, size_t len, size_t data_len);
zf_result zf_eval(zf_ctx *ctx, const char *buf);
void zf_abort(zf_ctx *ctx, zf_result reason);

void zf_push(zf_ctx *ctx, zf_cell v);
zf_cell zf_pop(zf_ctx *ctx);
zf_cell zf_pick(zf_ctx *ctx, zf_addr n);

zf_result zf_uservar_set(zf_ctx *ctx, zf_uservar_id uv, zf_cell v);
zf_result zf_uservar_get(zf_ctx *ctx, zf_uservar_id uv, zf_cell *v);

#if ZFORTH_EXT_OS_OBJECTS
int zf_cell_is_ext(zf_cell v);
zf_ext_kind zf_cell_ext_kind(zf_cell v);
zf_ext_id zf_cell_ext_id(zf_cell v);
zf_cell zf_cell_make_ext(zf_ext_kind kind, zf_ext_id id);
int zf_cell_equal(zf_cell a, zf_cell b);
void zf_ext_foreach_root(zf_ctx *ctx, zf_ext_root_cb cb, void *user);
#endif

/* Host provides these functions */

zf_input_state zf_host_sys(zf_ctx *ctx, zf_syscall_id id, const char *last_word);
void zf_host_trace(zf_ctx *ctx, const char *fmt, va_list va);
zf_cell zf_host_parse_num(zf_ctx *ctx, const char *buf);

#ifdef __cplusplus
}
#endif

#endif
