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

#ifndef ZF_DATA_ADDR
#define ZF_DATA_ADDR 0x40000000u
#endif

#ifndef ZF_DICT_INITIAL_SIZE
#define ZF_DICT_INITIAL_SIZE ZF_DICT_SIZE
#endif

#ifndef ZF_DICT_GROW_SIZE
#define ZF_DICT_GROW_SIZE ZF_DICT_SIZE
#endif

#ifndef ZF_DICT_MAX_SIZE
#define ZF_DICT_MAX_SIZE 0
#endif

#ifndef ZF_ENABLE_FLOAT
#define ZF_ENABLE_FLOAT 0
#endif

#ifndef ZF_ENABLE_DOUBLE_CELL
#define ZF_ENABLE_DOUBLE_CELL 0
#endif

#ifndef ZF_ENABLE_DFLOAT
#define ZF_ENABLE_DFLOAT 0
#endif

#ifndef ZF_ENABLE_NAMED_LOCALS
#define ZF_ENABLE_NAMED_LOCALS 0
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
	ZF_ABORT_BAD_LOCALS,
	ZF_ABORT_BOUNDS,
	ZF_ABORT_USER,
	ZF_ABORT_IMAGE_MISMATCH,
	ZF_ABORT_NIL
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
	ZF_SYSCALL_USER = 128
} zf_syscall_id;

typedef enum {
    ZF_USERVAR_HERE = 0,
    ZF_USERVAR_LATEST,
    ZF_USERVAR_TRACE,
    ZF_USERVAR_COMPILING,
    ZF_USERVAR_POSTPONE,
    ZF_USERVAR_DSP,
    ZF_USERVAR_RSP,
    /* The configuration a dictionary image was built with, checked when one
     * is imported or mounted (ZF_ABORT_IMAGE_MISMATCH) */
    ZF_USERVAR_CONFIG,

    ZF_USERVAR_COUNT
} zf_uservar_id;


typedef struct {
	/* Stacks and dictionary memory */
	zf_cell rstack[ZF_RSTACK_SIZE];
	zf_cell dstack[ZF_DSTACK_SIZE];
	#if ZF_ENABLE_DYNAMIC_DICT
	uint8_t *dict;
	size_t dict_cap;
	size_t dict_max;
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
#if ZF_ENABLE_DYNAMIC_DICT
	uint8_t *data_buf;
	size_t data_buf_cap;
#endif
#if ZF_ENABLE_ROM_DICT
	const uint8_t *rom_dict;
	zf_addr rom_len;
	zf_addr dict_base;
#endif

	/* State and stack and interpreter pointers */
	zf_input_state input_state;
	zf_addr fp; /* local variable frame pointer into rstack, 0 if none */
	zf_addr ip;
	zf_addr last_lit; /* the lit16 just compiled, for fusing with what follows */

	/* setjmp env for handling aborts */
	jmp_buf jmpbuf;
	int abort_jmp_valid;
	zf_result abort_reason;

	/* Input buffer */
	char read_buf[32];
	size_t read_len;

	/* Name buffer */
	char name_buf[32];

#if ZF_ENABLE_NAMED_LOCALS
	/* Named locals of the definition being compiled: entries of
	 * [length][slot][width][name...] */
	char lnames[ZF_LOCAL_NAMES_SIZE];
	uint16_t lnames_len;
	uint8_t lframe;   /* a {: :} frame is open in this definition */
	uint8_t lmode;    /* parsing state inside {: :} */
	uint8_t lslots;   /* slots declared so far */
	uint8_t largs;    /* slots filled from the data stack */
	uint8_t ldouble;  /* next name is a two-cell local (d:) */
#endif

} zf_ctx;


/* True is defined as the bitwise complement of false. */

#define ZF_FALSE ((zf_cell)0)
#define ZF_TRUE ((zf_cell)~(zf_int)ZF_FALSE)

/* ZForth API functions */

zf_result zf_init_checked(zf_ctx *ctx, int trace);
void zf_init(zf_ctx *ctx, int trace);
void zf_free(zf_ctx *ctx);
zf_result zf_dict_set_limit(zf_ctx *ctx, size_t max);
void zf_bootstrap(zf_ctx *ctx);
void *zf_dump(zf_ctx *ctx, size_t *len);
/* Returns a pointer to len bytes of dictionary memory at addr, aborting if the
 * range is invalid. The pointer is only valid until the dictionary is next
 * written or evaluated (zf_eval(), zf_dict_write_bytes(), a dictionary import,
 * ...): with a dynamic dictionary any write may grow and move it. Copy the
 * bytes out, or call zf_dict_addr() again, instead of holding the pointer. */
const void *zf_dict_addr(zf_ctx *ctx, zf_addr addr, size_t len);
void zf_dict_write_bytes(zf_ctx *ctx, zf_addr addr, const void *buf, size_t len);
void zf_dict_set_data_compile(zf_ctx *ctx, int enable);
size_t zf_dict_data_size(zf_ctx *ctx);
size_t zf_dict_size(zf_ctx *ctx);
size_t zf_dict_capacity(zf_ctx *ctx);
const void *zf_dict_data(zf_ctx *ctx);
zf_result zf_dict_import(zf_ctx *ctx, const void *buf, size_t len);
zf_result zf_dict_import_with_data(zf_ctx *ctx, const void *buf, size_t len, size_t data_len);
zf_result zf_dict_mount_rom(zf_ctx *ctx, const void *buf, size_t len, size_t data_len);
zf_result zf_eval(zf_ctx *ctx, const char *buf);
/* The execution token of the word called name in *xt: ZF_OK, or
 * ZF_ABORT_NOT_A_WORD. A lookup walks the dictionary: look a word that is
 * called often up once (again when the latest word changes), and run it with
 * zf_execute(). */
zf_result zf_find(zf_ctx *ctx, const char *name, zf_addr *xt);
/* Run the word at xt, as zf_eval() runs its name, without the lookup */
zf_result zf_execute(zf_ctx *ctx, zf_addr xt);
#if defined(__GNUC__)
__attribute__((noreturn))
#endif
void zf_abort(zf_ctx *ctx, zf_result reason);

void zf_push(zf_ctx *ctx, zf_cell v);
zf_cell zf_pop(zf_ctx *ctx);
zf_cell zf_pick(zf_ctx *ctx, zf_addr n);

zf_result zf_uservar_set(zf_ctx *ctx, zf_uservar_id uv, zf_cell v);
zf_result zf_uservar_get(zf_ctx *ctx, zf_uservar_id uv, zf_cell *v);

/* Parse a single-cell number token the standard way: decimal or 0x hex
 * integers from -2147483648 to 4294967295 (values above 2147483647 wrap to
 * negative), and, with ZF_ENABLE_FLOAT, floats for tokens containing '.', 'e'
 * or 'E'. A token ending in '.' is not a float: with ZF_ENABLE_DOUBLE_CELL the
 * interpreter reads it as a double-cell literal before calling
 * zf_host_parse_num(), and likewise a double float with a d exponent (1.5d0)
 * with ZF_ENABLE_DFLOAT. Returns 1 and sets *v on success, 0 if buf is not a
 * number. zf_host_parse_num() can simply call this. */
int zf_parse_num(const char *buf, zf_cell *v);

#if ZF_ENABLE_FLOAT
/* Convert between a float and the cell holding its bit pattern */
zf_cell zf_float_to_cell(float f);
float zf_cell_to_float(zf_cell v);
#endif

#if ZF_ENABLE_DFLOAT
/* Convert between a double and the two cells ( lo hi ) holding its bits */
double zf_cells_to_dfloat(zf_cell lo, zf_cell hi);
void zf_dfloat_to_cells(double v, zf_cell *lo, zf_cell *hi);
#endif

/* Host provides these functions */

zf_input_state zf_host_sys(zf_ctx *ctx, zf_syscall_id id, const char *last_word);
void zf_host_trace(zf_ctx *ctx, const char *fmt, va_list va);
zf_cell zf_host_parse_num(zf_ctx *ctx, const char *buf);

#ifdef __cplusplus
}
#endif

#endif
