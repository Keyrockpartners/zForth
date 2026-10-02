
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>

#include "zforth.h"

#if ZF_ENABLE_FLOAT || ZF_ENABLE_DFLOAT
#include <math.h>
#endif

/* zf_cell must be a signed integer type and zf_ucell its unsigned counterpart */
typedef char zf_cell_must_be_signed_integer[((zf_cell)0.5 == 0 && (zf_cell)-1 < 0) ? 1 : -1];
typedef char zf_ucell_must_match_cell[(sizeof(zf_ucell) == sizeof(zf_cell) && (zf_ucell)-1 > 0) ? 1 : -1];
#if ZF_ENABLE_FLOAT
typedef char zf_float_must_fit_cell[(sizeof(float) == sizeof(zf_cell)) ? 1 : -1];
#endif
#if ZF_ENABLE_DOUBLE_CELL
typedef char zf_double_cell_needs_32_bit_cells[(sizeof(zf_cell) == sizeof(uint32_t)) ? 1 : -1];
#endif
#if ZF_ENABLE_DFLOAT
typedef char zf_dfloat_needs_32_bit_cells[(sizeof(zf_cell) == sizeof(uint32_t) && sizeof(double) == sizeof(uint64_t)) ? 1 : -1];
#endif

#define ZF_CELL_BITS ((zf_cell)(sizeof(zf_cell) * 8))


/* Allocation hooks for the dynamic dictionary; override in zfconf.h to choose
 * the heap, e.g. heap_caps_realloc() on ESP32. ZF_REALLOC(NULL, n) must act
 * as malloc(n). */
#ifndef ZF_REALLOC
#define ZF_REALLOC(p, n) realloc((p), (n))
#endif
#ifndef ZF_FREE
#define ZF_FREE(p) free(p)
#endif

/* Flags and length encoded in words */

#define ZF_FLAG_IMMEDIATE (1<<6)
#define ZF_FLAG_PRIM      (1<<5)
#define ZF_FLAG_LEN(v)    (v & 0x1f)


/* This macro is used to perform boundary checks. If ZF_ENABLE_BOUNDARY_CHECKS
 * is set to 0, the boundary check code will not be compiled in to reduce size */

#if ZF_ENABLE_BOUNDARY_CHECKS
#define CHECK(ctx, exp, abort) if(!(exp)) zf_abort(ctx, abort);
#else
#define CHECK(ctx, exp, abort)
#endif

typedef enum {
	ZF_MEM_SIZE_VAR = 0,      /* Variable size encoding, 1, 2 or 1+sizeof(zf_cell) bytes */
	ZF_MEM_SIZE_CELL = 1,     /* sizeof(zf_cell) bytes */
	ZF_MEM_SIZE_U8 = 2,
	ZF_MEM_SIZE_U16 = 3,
	ZF_MEM_SIZE_U32 = 4,
	ZF_MEM_SIZE_S8 = 5,
	ZF_MEM_SIZE_S16 = 6,
	ZF_MEM_SIZE_S32 = 7,
	ZF_MEM_SIZE_VAR_MAX = 64, /* Variable size encoding, 1+sizeof(zf_cell) bytes */
} zf_mem_size;

/* Define all primitives, make sure the two tables below always match.  The
 * names are defined as a \0 separated list, terminated by double \0. This
 * saves space on the pointers compared to an array of strings. Immediates are
 * prefixed by an underscore, which is later stripped of when putting the name
 * in the dictionary. */

#define _(s) s "\0"

typedef enum {
	PRIM_EXIT,    PRIM_LIT,       PRIM_LTZ,  PRIM_COL,     PRIM_SEMICOL,  PRIM_ADD,
	PRIM_SUB,     PRIM_MUL,       PRIM_DIV,  PRIM_MOD,     PRIM_DROP,     PRIM_DUP,
	PRIM_PICKR,   PRIM_IMMEDIATE, PRIM_PEEK, PRIM_POKE,    PRIM_SWAP,     PRIM_ROT,
	PRIM_JMP,     PRIM_JMP0,      PRIM_TICK, PRIM_COMMENT, PRIM_PUSHR,    PRIM_POPR,
	PRIM_EQUAL,   PRIM_SYS,       PRIM_PICK, PRIM_COMMA,   PRIM_KEY,      PRIM_LITS,
	PRIM_LEN,     PRIM_AND,       PRIM_OR,   PRIM_XOR,     PRIM_SHL,      PRIM_SHR,
	PRIM_LITERAL, PRIM_CHECKPOINT, PRIM_RESTORE, PRIM_DATA_HERE, PRIM_DATA_ALLOT,
	PRIM_ULT,     PRIM_RSHIFT,    PRIM_UDIV, PRIM_UMOD,    PRIM_LT,
	PRIM_LOCALS,  PRIM_LGET,      PRIM_LSET, PRIM_ENDLOCALS, PRIM_2LGET,  PRIM_2LSET,
#if ZF_ENABLE_NAMED_LOCALS
	PRIM_LBRACE,  PRIM_TO,
#endif
#if ZF_ENABLE_DOUBLE_CELL
	PRIM_DADD,    PRIM_DSUB,      PRIM_DULT, PRIM_UMSTAR,  PRIM_UDSTAR,   PRIM_UDDIV,
	PRIM_UDMOD,   PRIM_DLT,       PRIM_MSTAR, PRIM_DDIV,   PRIM_DMOD,
#endif
#if ZF_ENABLE_FLOAT
	PRIM_FADD,    PRIM_FSUB,      PRIM_FMUL, PRIM_FDIV,    PRIM_FLT,      PRIM_FEQ,
	PRIM_STOF,    PRIM_UTOF,      PRIM_FTOS, PRIM_FSQRT,   PRIM_FFLOOR,   PRIM_FCEIL,
	PRIM_FROUND,  PRIM_FTRUNC,
#endif
#if ZF_ENABLE_DFLOAT
	PRIM_DFADD,   PRIM_DFSUB,     PRIM_DFMUL, PRIM_DFDIV,  PRIM_DFLT,     PRIM_DFEQ,
	PRIM_STODF,   PRIM_DFTOS,     PRIM_DTODF, PRIM_DFTOD,  PRIM_DFSQRT,   PRIM_DFFLOOR,
	PRIM_DFCEIL,  PRIM_DFROUND,   PRIM_DFTRUNC,
#if ZF_ENABLE_FLOAT
	PRIM_FTODF,   PRIM_DFTOF,
#endif
#endif
	PRIM_COUNT
} zf_prim;

#if ZF_ENABLE_BOOTSTRAP
static const char prim_names[] =
	_("exit")    _("lit")        _("<0")    _(":")     _("_;")        _("+")
	_("-")       _("*")          _("/")     _("%")     _("drop")      _("dup")
	_("pickr")   _("_immediate") _("@@")    _("!!")    _("swap")      _("rot")
	_("jmp")     _("jmp0")       _("'")     _("_(")    _(">r")        _("r>")
	_("=")       _("sys")        _("pick")  _(",,")    _("key")       _("lits")
	_("##")      _("&")          _("|")     _("^")     _("<<")        _(">>")
	_("_literal") _("chkpt!") _("chkpt-restore") _("data-here") _("data-allot")
	_("u<")      _("rshift")     _("u/")    _("umod")  _("<")
	_("locals")  _("l@")         _("l!")    _("endlocals") _("2l@")    _("2l!")
#if ZF_ENABLE_NAMED_LOCALS
	_("_{:")     _("_to")
#endif
#if ZF_ENABLE_DOUBLE_CELL
	_("d+")      _("d-")         _("du<")   _("um*")   _("ud*")       _("ud/")
	_("udmod")   _("d<")         _("m*")    _("d/")    _("dmod")
#endif
#if ZF_ENABLE_FLOAT
	_("f+")      _("f-")         _("f*")    _("f/")    _("f<")        _("f=")
	_("s>f")     _("u>f")        _("f>s")   _("fsqrt") _("ffloor")    _("fceil")
	_("fround")  _("ftrunc")
#endif
#if ZF_ENABLE_DFLOAT
	_("df+")     _("df-")        _("df*")   _("df/")   _("df<")       _("df=")
	_("s>df")    _("df>s")       _("d>df")  _("df>d")  _("dfsqrt")    _("dffloor")
	_("dfceil")  _("dfround")    _("dftrunc")
#if ZF_ENABLE_FLOAT
	_("f>df")    _("df>f")
#endif
#endif
	;
#endif

typedef struct {
	zf_addr here;
	zf_addr latest;
} zf_checkpoint;


/* User variables are variables which are shared between forth and C. From
 * forth these can be accessed with @ and ! at pseudo-indices in low memory, in
 * C they are stored in an array of zf_addr with friendly reference names
 * through some macros */

#if ZF_ENABLE_ROM_DICT
#define USERVAR(ctx)   ((ctx)->uservars)
#else
#define USERVAR(ctx)   ((zf_addr *)(ctx)->dict)
#endif
#define HERE(ctx)      USERVAR(ctx)[ZF_USERVAR_HERE]      /* compilation pointer in dictionary */
#define LATEST(ctx)    USERVAR(ctx)[ZF_USERVAR_LATEST]    /* pointer to last compiled word */
#define TRACE(ctx)     USERVAR(ctx)[ZF_USERVAR_TRACE]     /* trace enable flag */
#define COMPILING(ctx) USERVAR(ctx)[ZF_USERVAR_COMPILING] /* compiling flag */
#define POSTPONE(ctx)  USERVAR(ctx)[ZF_USERVAR_POSTPONE]  /* flag to indicate next imm word should be compiled */
#define DSP(ctx)       USERVAR(ctx)[ZF_USERVAR_DSP]       /* data stack pointer */
#define RSP(ctx)       USERVAR(ctx)[ZF_USERVAR_RSP]       /* return stack pointer */

static zf_addr dict_base(const zf_ctx *ctx)
{
#if ZF_ENABLE_ROM_DICT
	return ctx->dict_base;
#else
	(void)ctx;
	return 0;
#endif
}
#define DICT_BASE(ctx) dict_base(ctx)

#if ZF_ENABLE_BOOTSTRAP
static const char uservar_names[] =
	_("h")   _("latest") _("trace")  _("compiling")  _("_postpone")  _("dsp")
	_("rsp");
#endif



/* Prototypes */

static void do_prim(zf_ctx *ctx, zf_prim prim, const char *input);
static zf_addr dict_put_bytes(zf_ctx *ctx, zf_addr addr, const void *buf, size_t len);
static zf_addr dict_get_cell(zf_ctx *ctx, zf_addr addr, zf_cell *v);
static void dict_get_bytes(zf_ctx *ctx, zf_addr addr, void *buf, size_t len);
static int dict_has_range(const zf_ctx *ctx, zf_addr addr, size_t len);
static void uservars_from_image(zf_ctx *ctx, const void *buf);
static void uservars_to_image(zf_ctx *ctx);


#if ZF_ENABLE_NAMED_LOCALS
enum { LMODE_NONE, LMODE_ARGS, LMODE_VALS, LMODE_COMMENT };

/* Forget the named locals of the definition being compiled */
static void locals_reset(zf_ctx *ctx)
{
	ctx->lnames_len = 0;
	ctx->lframe = 0;
	ctx->lmode = LMODE_NONE;
	ctx->lslots = 0;
	ctx->largs = 0;
	ctx->ldouble = 0;
}

/* Find a named local; the most recently declared match wins */
static int local_find(zf_ctx *ctx, const char *name, zf_cell *slot, int *width)
{
	size_t len = strlen(name);
	uint16_t i = 0;
	int found = 0;

	while(i < ctx->lnames_len) {
		uint8_t l = (uint8_t)ctx->lnames[i];
		if(l == len && memcmp(&ctx->lnames[i + 3], name, len) == 0) {
			*slot = (uint8_t)ctx->lnames[i + 1];
			*width = (uint8_t)ctx->lnames[i + 2];
			found = 1;
		}
		i += 3 + l;
	}
	return found;
}
#else
#define locals_reset(ctx) ((void)0)
#endif

static void checkpoint_save(zf_ctx *ctx, zf_addr addr)
{
	zf_checkpoint cp;

	cp.here = HERE(ctx);
	cp.latest = LATEST(ctx);
	dict_put_bytes(ctx, addr, &cp, sizeof(cp));
}

static void checkpoint_restore(zf_ctx *ctx, zf_addr addr)
{
	zf_checkpoint cp;

	dict_get_bytes(ctx, addr, &cp, sizeof(cp));
	CHECK(ctx, dict_has_range(ctx, cp.here, 0), ZF_ABORT_OUTSIDE_MEM);
	CHECK(ctx, cp.latest <= cp.here, ZF_ABORT_INTERNAL_ERROR);
	/* A checkpoint compiled into a prebuilt image points into the image
	 * itself; new definitions start after it and its data window instead */
	if(cp.here < ctx->data_base + ctx->data_len) {
		cp.here = ctx->data_base + ctx->data_len;
	}
	HERE(ctx) = cp.here;
	LATEST(ctx) = cp.latest;
	ctx->input_state = ZF_INPUT_INTERPRET;
	ctx->ip = 0;
	ctx->read_len = 0;
	COMPILING(ctx) = 0;
	locals_reset(ctx);
	POSTPONE(ctx) = 0;
	/* Unwind the return stack: the code being run may have been discarded.
	 * The data stack is left alone, as for ANS MARKER */
	RSP(ctx) = 0;
	ctx->fp = 0;
}

static size_t dict_writable_capacity(const zf_ctx *ctx)
{
	#if ZF_ENABLE_DYNAMIC_DICT
	return ctx->dict_cap;
	#else
	(void)ctx;
	return ZF_DICT_SIZE;
	#endif
}

static size_t dict_capacity(const zf_ctx *ctx)
{
	return (size_t)DICT_BASE(ctx) + dict_writable_capacity(ctx);
}

static int range_end(zf_addr addr, size_t len, size_t *end)
{
	size_t start = (size_t)addr;
	if(start > (size_t)-1 - len) {
		return 0;
	}
	*end = start + len;
	return 1;
}

/* The data window [ZF_DATA_ADDR, ZF_DATA_ADDR + size) holds the storage of
 * variables compiled into a prebuilt image, so they stay writable when the
 * image itself is mounted read-only. While exporting (data_compile) it is
 * backed by data_buf; after import/mount it is backed by the data_len bytes
 * reserved at data_base in the writable dictionary. */

static int is_data_addr(zf_addr addr)
{
	return (size_t)addr >= (size_t)ZF_DATA_ADDR;
}

static size_t data_window_size(const zf_ctx *ctx)
{
	return ctx->data_compile ? (size_t)ctx->data_here : (size_t)ctx->data_len;
}

static int data_window_has_range(const zf_ctx *ctx, zf_addr addr, size_t len)
{
	size_t off = (size_t)addr - (size_t)ZF_DATA_ADDR;
	size_t size = data_window_size(ctx);
	return off <= size && len <= size - off;
}

static int dict_has_range(const zf_ctx *ctx, zf_addr addr, size_t len)
{
	size_t start = (size_t)addr;
	size_t end;

	if(is_data_addr(addr)) {
		return data_window_has_range(ctx, addr, len);
	}
	if(!range_end(addr, len, &end)) {
		return 0;
	}

#if ZF_ENABLE_ROM_DICT
	if(ctx->rom_dict != NULL) {
		if(start < (size_t)ctx->rom_len) {
			if(end <= (size_t)ctx->rom_len) return 1;
			return (size_t)ctx->dict_base == (size_t)ctx->rom_len &&
			       end <= (size_t)ctx->dict_base + dict_writable_capacity(ctx);
		}
		if(start < (size_t)ctx->dict_base) return 0;
		return end <= (size_t)ctx->dict_base + dict_writable_capacity(ctx);
	}
#endif

	if(start < (size_t)DICT_BASE(ctx)) {
		return 0;
	}
	return end <= (size_t)DICT_BASE(ctx) + dict_writable_capacity(ctx);
}

static int dict_has_writable_range(const zf_ctx *ctx, zf_addr addr, size_t len)
{
	size_t start = (size_t)addr;
	size_t end;

	if(is_data_addr(addr)) {
		return data_window_has_range(ctx, addr, len);
	}
	if(!range_end(addr, len, &end)) {
		return 0;
	}
	if(start < (size_t)DICT_BASE(ctx)) {
		return 0;
	}
	return end <= (size_t)DICT_BASE(ctx) + dict_writable_capacity(ctx);
}

static size_t dict_writable_offset(zf_ctx *ctx, zf_addr addr)
{
	if((size_t)addr < (size_t)DICT_BASE(ctx)) {
		zf_abort(ctx, ZF_ABORT_OUTSIDE_MEM);
	}
	return (size_t)addr - (size_t)DICT_BASE(ctx);
}

static uint8_t *data_window_ptr(zf_ctx *ctx, zf_addr addr, size_t len)
{
	size_t off = (size_t)addr - (size_t)ZF_DATA_ADDR;
	CHECK(ctx, data_window_has_range(ctx, addr, len), ZF_ABORT_OUTSIDE_MEM);
	if(ctx->data_compile) {
#if ZF_ENABLE_DYNAMIC_DICT
		return ctx->data_buf ? ctx->data_buf + off : NULL;
#else
		return NULL;
#endif
	}
	return ctx->dict + dict_writable_offset(ctx, ctx->data_base) + off;
}

static void uservars_from_image(zf_ctx *ctx, const void *buf)
{
#if ZF_ENABLE_ROM_DICT
	memcpy(ctx->uservars, buf, ZF_USERVAR_COUNT * sizeof(zf_addr));
#else
	(void)ctx;
	(void)buf;
#endif
}

static void uservars_to_image(zf_ctx *ctx)
{
#if ZF_ENABLE_ROM_DICT
	if(DICT_BASE(ctx) == 0 && dict_writable_capacity(ctx) >= ZF_USERVAR_COUNT * sizeof(zf_addr)) {
		memcpy(ctx->dict, ctx->uservars, ZF_USERVAR_COUNT * sizeof(zf_addr));
	}
#else
	(void)ctx;
#endif
}

#if ZF_ENABLE_DYNAMIC_DICT
/* Grow the writable dictionary to at least 'needed' bytes, mapped at address
 * 'base'. Returns an error instead of aborting so it is safe to call outside
 * zf_eval(); on failure the dictionary is left unchanged. */
static zf_result dict_grow(zf_ctx *ctx, size_t base, size_t needed)
{
	size_t new_cap;
	uint8_t *new_dict;

	if(needed <= ctx->dict_cap) {
		return ZF_OK;
	}
	if(base > (size_t)ZF_DATA_ADDR || needed > (size_t)ZF_DATA_ADDR - base) {
		return ZF_ABORT_OUTSIDE_MEM;
	}
	if(ctx->dict_max != 0 && needed > ctx->dict_max) {
		return ZF_ABORT_OUTSIDE_MEM;
	}

	new_cap = ctx->dict_cap;
	while(new_cap < needed) {
		if(new_cap > (size_t)-1 - ZF_DICT_GROW_SIZE) {
			return ZF_ABORT_OUTSIDE_MEM;
		}
		new_cap += ZF_DICT_GROW_SIZE;
	}
	if(ctx->dict_max != 0 && new_cap > ctx->dict_max) {
		new_cap = ctx->dict_max;
	}

	new_dict = (uint8_t *)ZF_REALLOC(ctx->dict, new_cap);
	if(new_dict == NULL) {
		return ZF_ABORT_OUTSIDE_MEM;
	}

	memset(new_dict + ctx->dict_cap, 0, new_cap - ctx->dict_cap);
	ctx->dict = new_dict;
	ctx->dict_cap = new_cap;
	return ZF_OK;
}

static void ensure_dict_capacity(zf_ctx *ctx, zf_addr addr, size_t len)
{
	size_t start = dict_writable_offset(ctx, addr);
	zf_result r;

	if(start > (size_t)-1 - len) {
		zf_abort(ctx, ZF_ABORT_OUTSIDE_MEM);
	}
	r = dict_grow(ctx, (size_t)DICT_BASE(ctx), start + len);
	if(r != ZF_OK) {
		zf_abort(ctx, r);
	}
}
#endif

static void data_buf_reserve(zf_ctx *ctx, zf_addr size)
{
#if ZF_ENABLE_DYNAMIC_DICT
	size_t new_cap;
	uint8_t *new_buf;

	CHECK(ctx, (size_t)size <= (size_t)(zf_addr)-1 - (size_t)ZF_DATA_ADDR, ZF_ABORT_OUTSIDE_MEM);
	if((size_t)size <= ctx->data_buf_cap) {
		return;
	}

	new_cap = ctx->data_buf_cap;
	while(new_cap < (size_t)size) {
		new_cap += ZF_DICT_GROW_SIZE;
	}

	new_buf = (uint8_t *)ZF_REALLOC(ctx->data_buf, new_cap);
	if(new_buf == NULL) {
		zf_abort(ctx, ZF_ABORT_OUTSIDE_MEM);
	}

	memset(new_buf + ctx->data_buf_cap, 0, new_cap - ctx->data_buf_cap);
	ctx->data_buf = new_buf;
	ctx->data_buf_cap = new_cap;
#else
	/* Exporting with a data window needs a heap-backed buffer */
	CHECK(ctx, size == 0, ZF_ABORT_OUTSIDE_MEM);
#endif
}


/* Tracing functions. If disabled, the trace() function is replaced by an empty
 * macro, allowing the compiler to optimize away the function calls to
 * op_name() */

#if ZF_ENABLE_TRACE

static void do_trace(zf_ctx *ctx, const char *fmt, ...)
{
	if(TRACE(ctx)) {
		va_list va;
		va_start(va, fmt);
		zf_host_trace(ctx, fmt, va);
		va_end(va);
	}
}

#define trace(ctx, ...) if(TRACE(ctx)) do_trace(ctx, __VA_ARGS__)

static const char *op_name(zf_ctx *ctx, zf_addr addr)
{
	zf_addr w = LATEST(ctx);
	char *name = ctx->name_buf;

	while(TRACE(ctx) && w) {
		zf_addr xt, p = w;
		zf_cell d, link, op2;
		int lenflags;

		p += dict_get_cell(ctx, p, &d);
		lenflags = d;
		p += dict_get_cell(ctx, p, &link);
		xt = p + ZF_FLAG_LEN(lenflags);
		dict_get_cell(ctx, xt, &op2);

		if(((lenflags & ZF_FLAG_PRIM) && addr == (zf_addr)op2) || addr == w || addr == xt) {
			int l = ZF_FLAG_LEN(lenflags);
			dict_get_bytes(ctx, p, name, l);
			name[l] = '\0';
			return name;
		}

		w = link;
	}
	return "?";
}

#else
static void trace(zf_ctx *ctx, const char *fmt, ...) { }
static const char *op_name(zf_ctx *ctx, zf_addr addr) { return NULL; }
#endif


/*
 * Handle abort by unwinding the C stack and sending control back into
 * zf_eval()
 */

void zf_abort(zf_ctx *ctx, zf_result reason)
{
	if(ctx != NULL) {
		if(reason == ZF_OK) {
			reason = ZF_ABORT_INTERNAL_ERROR;
		}
		ctx->abort_reason = reason;
		if(ctx->abort_jmp_valid > 0) {
			longjmp(ctx->jmpbuf, reason);
		}
	}

	/* zf_abort() is only recoverable while a caller has installed a
	 * setjmp handler (zf_eval(), or another protected wrapper). Falling
	 * through would let callers continue after failed boundary checks, so
	 * fail hard instead of longjmp'ing through an uninitialised jmp_buf. */
	abort();
}



/*
 * Stack operations. 
 */

void zf_push(zf_ctx *ctx, zf_cell v)
{
	CHECK(ctx, DSP(ctx) < ZF_DSTACK_SIZE, ZF_ABORT_DSTACK_OVERRUN);
	trace(ctx, "»" ZF_CELL_FMT " ", v);
	ctx->dstack[DSP(ctx)++] = v;
}


zf_cell zf_pop(zf_ctx *ctx)
{
	zf_cell v;
	CHECK(ctx, DSP(ctx) > 0, ZF_ABORT_DSTACK_UNDERRUN);
	CHECK(ctx, DSP(ctx) <= ZF_DSTACK_SIZE, ZF_ABORT_DSTACK_OVERRUN);
	v = ctx->dstack[--DSP(ctx)];
	trace(ctx, "«" ZF_CELL_FMT " ", v);
	return v;
}


zf_cell zf_pick(zf_ctx *ctx, zf_addr n)
{
	CHECK(ctx, n < DSP(ctx), ZF_ABORT_DSTACK_UNDERRUN);
	CHECK(ctx, DSP(ctx) <= ZF_DSTACK_SIZE, ZF_ABORT_DSTACK_OVERRUN);
	return ctx->dstack[DSP(ctx)-n-1];
}


static void zf_pushr(zf_ctx *ctx, zf_cell v)
{
	CHECK(ctx, RSP(ctx) < ZF_RSTACK_SIZE, ZF_ABORT_RSTACK_OVERRUN);
	trace(ctx, "r»" ZF_CELL_FMT " ", v);
	ctx->rstack[RSP(ctx)++] = v;
}


static zf_cell zf_popr(zf_ctx *ctx)
{
	zf_cell v;
	CHECK(ctx, RSP(ctx) > 0, ZF_ABORT_RSTACK_UNDERRUN);
	CHECK(ctx, RSP(ctx) <= ZF_RSTACK_SIZE, ZF_ABORT_RSTACK_OVERRUN);
	v = ctx->rstack[--RSP(ctx)];
	trace(ctx, "r«" ZF_CELL_FMT " ", v);
	return v;
}

zf_cell zf_pickr(zf_ctx *ctx, zf_addr n)
{
	CHECK(ctx, n < RSP(ctx), ZF_ABORT_RSTACK_UNDERRUN);
	CHECK(ctx, RSP(ctx) <= ZF_RSTACK_SIZE, ZF_ABORT_RSTACK_OVERRUN);
	return ctx->rstack[RSP(ctx)-n-1];
}



/*
 * All access to dictionary memory is done through these functions.
 */

static zf_addr dict_put_bytes(zf_ctx *ctx, zf_addr addr, const void *buf, size_t len)
{
	const uint8_t *p = (const uint8_t *)buf;
	size_t i = len;
	size_t off;
	if(is_data_addr(addr)) {
		uint8_t *dst = data_window_ptr(ctx, addr, len);
		if(len) memcpy(dst, buf, len);
		return len;
	}
	#if ZF_ENABLE_DYNAMIC_DICT
	/* Only grow for writes starting at or below HERE (compiling, or filling
	 * allotted space), so a stray store to a large address can't make the
	 * dictionary allocate up to it */
	CHECK(ctx, addr <= HERE(ctx) || dict_has_writable_range(ctx, addr, len), ZF_ABORT_OUTSIDE_MEM);
	ensure_dict_capacity(ctx, addr, len);
	#endif
	CHECK(ctx, dict_has_writable_range(ctx, addr, len), ZF_ABORT_OUTSIDE_MEM);
	off = dict_writable_offset(ctx, addr);
	while(i--) ctx->dict[off++] = *p++;
	return len;
}


static void dict_get_bytes(zf_ctx *ctx, zf_addr addr, void *buf, size_t len)
{
	uint8_t *p = (uint8_t *)buf;
	if(is_data_addr(addr)) {
		const uint8_t *src = data_window_ptr(ctx, addr, len);
		if(len) memcpy(buf, src, len);
		return;
	}
	CHECK(ctx, dict_has_range(ctx, addr, len), ZF_ABORT_OUTSIDE_MEM);
	while(len--) {
#if ZF_ENABLE_ROM_DICT
		if(ctx->rom_dict != NULL && addr < ctx->rom_len) {
			*p++ = ctx->rom_dict[addr++];
			continue;
		}
#endif
		*p++ = ctx->dict[dict_writable_offset(ctx, addr++)];
	}
}

const void *zf_dict_addr(zf_ctx *ctx, zf_addr addr, size_t len)
{
	size_t start = (size_t)addr;
	size_t end;
	if(is_data_addr(addr)) {
		return data_window_ptr(ctx, addr, len);
	}
	CHECK(ctx, dict_has_range(ctx, addr, len), ZF_ABORT_OUTSIDE_MEM);
	if(!range_end(addr, len, &end)) {
		zf_abort(ctx, ZF_ABORT_OUTSIDE_MEM);
	}
#if ZF_ENABLE_ROM_DICT
	if(ctx->rom_dict != NULL && start < (size_t)ctx->rom_len && end <= (size_t)ctx->rom_len) {
		return ctx->rom_dict + start;
	}
#endif
	if(start >= (size_t)DICT_BASE(ctx) && end <= (size_t)DICT_BASE(ctx) + dict_writable_capacity(ctx)) {
		return ctx->dict + (start - (size_t)DICT_BASE(ctx));
	}
	zf_abort(ctx, ZF_ABORT_OUTSIDE_MEM);
	return NULL;
}

void zf_dict_write_bytes(zf_ctx *ctx, zf_addr addr, const void *buf, size_t len)
{
	dict_put_bytes(ctx, addr, buf, len);
}


/*
 * zf_cells are encoded in the dictionary with a variable length:
 *
 * encode:
 *
 *    integer   0 ..   127  0xxxxxxx
 *    integer 128 .. 16383  10xxxxxx xxxxxxxx
 *    else                  11111111 <raw copy of zf_cell>
 */

#if ZF_ENABLE_TYPED_MEM_ACCESS
#define GET(s, t) if(size == s) { t v ## t; dict_get_bytes(ctx, addr, &v ## t, sizeof(t)); *v = v ## t; return sizeof(t); };
#define PUT(s, t, val) if(size == s) { t v ## t = val; return dict_put_bytes(ctx, addr, &v ## t, sizeof(t)); }
#else
#define GET(s, t)
#define PUT(s, t, val)
#endif

static zf_addr dict_put_cell_typed(zf_ctx *ctx, zf_addr addr, zf_cell v, zf_mem_size size)
{
	uint8_t t[2];

	trace(ctx, "\n+" ZF_ADDR_FMT " " ZF_ADDR_FMT, addr, (zf_addr)v);

	if(size == ZF_MEM_SIZE_VAR) {
		if(v >= 0 && v < 16384) {
			unsigned int vi = (unsigned int)v;
			if((zf_cell)vi == v) {
				if(vi < 128) {
					trace(ctx, " ¹");
					t[0] = vi;
					return dict_put_bytes(ctx, addr, t, 1);
				}
				if(vi < 16384) {
					trace(ctx, " ²");
					t[0] = (vi >> 8) | 0x80;
					t[1] = vi;
					return dict_put_bytes(ctx, addr, t, sizeof(t));
				}
			}
		}
	}

	if(size == ZF_MEM_SIZE_VAR || size == ZF_MEM_SIZE_VAR_MAX) {
		trace(ctx, " ⁵");
		t[0] = 0xff;
		return dict_put_bytes(ctx, addr+0, t, 1) + 
		       dict_put_bytes(ctx, addr+1, &v, sizeof(v));
	} 
	
	PUT(ZF_MEM_SIZE_CELL, zf_cell, v);
	PUT(ZF_MEM_SIZE_U8, uint8_t, v);
	PUT(ZF_MEM_SIZE_U16, uint16_t, v);
	PUT(ZF_MEM_SIZE_U32, uint32_t, v);
	PUT(ZF_MEM_SIZE_S8, int8_t, v);
	PUT(ZF_MEM_SIZE_S16, int16_t, v);
	PUT(ZF_MEM_SIZE_S32, int32_t, v);

	zf_abort(ctx, ZF_ABORT_INVALID_SIZE);
	return 0;
}


/* 
 * Get cell from dictionary memory, with specified cell size encoding;
 * returns the number of bytes read
 */
static zf_addr dict_get_cell_typed(zf_ctx *ctx, zf_addr addr, zf_cell *v, zf_mem_size size)
{
	uint8_t t[2];

	if(size == ZF_MEM_SIZE_VAR) {
		/* Only read the bytes the encoding uses, so a cell at the very end
		 * of the dictionary or data window can be read */
		dict_get_bytes(ctx, addr, t, 1);
		if(t[0] & 0x80) {
			if(t[0] == 0xff) {
				dict_get_bytes(ctx, addr+1, v, sizeof(*v));
				return 1 + sizeof(*v);
			} else {
				dict_get_bytes(ctx, addr+1, &t[1], 1);
				*v = ((t[0] & 0x3f) << 8) + t[1];
				return 2;
			}
		} else {
			*v = t[0];
			return 1;
		}
	} 
	
	GET(ZF_MEM_SIZE_CELL, zf_cell);
	GET(ZF_MEM_SIZE_U8, uint8_t);
	GET(ZF_MEM_SIZE_U16, uint16_t);
	GET(ZF_MEM_SIZE_U32, uint32_t);
	GET(ZF_MEM_SIZE_S8, int8_t);
	GET(ZF_MEM_SIZE_S16, int16_t);
	GET(ZF_MEM_SIZE_S32, int32_t);

	zf_abort(ctx, ZF_ABORT_INVALID_SIZE);
	return 0;
}


/*
 * Shortcut functions for cell access with variable cell size
 */

static zf_addr dict_put_cell(zf_ctx *ctx, zf_addr addr, zf_cell v)
{
	return dict_put_cell_typed(ctx, addr, v, ZF_MEM_SIZE_VAR);
}


static zf_addr dict_get_cell(zf_ctx *ctx, zf_addr addr, zf_cell *v)
{
	return dict_get_cell_typed(ctx, addr, v, ZF_MEM_SIZE_VAR);
}


/*
 * Generic dictionary adding, these functions all add at the HERE(ctx) pointer and
 * increase the pointer
 */

static void dict_add_cell_typed(zf_ctx *ctx, zf_cell v, zf_mem_size size)
{
	HERE(ctx) += dict_put_cell_typed(ctx, HERE(ctx), v, size);
	trace(ctx, " ");
}


static void dict_add_cell(zf_ctx *ctx, zf_cell v)
{
	dict_add_cell_typed(ctx, v, ZF_MEM_SIZE_VAR);
}


static void dict_add_op(zf_ctx *ctx, zf_addr op)
{
	dict_add_cell(ctx, op);
	trace(ctx, "+%s ", op_name(ctx, op));
}


static void dict_add_lit(zf_ctx *ctx, zf_cell v)
{
	dict_add_op(ctx, PRIM_LIT);
	dict_add_cell(ctx, v);
}


static void dict_add_str(zf_ctx *ctx, const char *s)
{
	size_t l;
	trace(ctx, "\n+" ZF_ADDR_FMT " " ZF_ADDR_FMT " s '%s'", HERE(ctx), 0, s);
	l = strlen(s);
	HERE(ctx) += dict_put_bytes(ctx, HERE(ctx), s, l);
}


/*
 * Create new word, adjusting HERE(ctx) and LATEST(ctx) accordingly
 */

static void create(zf_ctx *ctx, const char *name, int flags)
{
	zf_addr here_prev;
	trace(ctx, "\n=== create '%s'", name);
	here_prev = HERE(ctx);
	dict_add_cell(ctx, (strlen(name)) | flags);
	dict_add_cell(ctx, LATEST(ctx));
	dict_add_str(ctx, name);
	LATEST(ctx) = here_prev;
	trace(ctx, "\n===");
}


/*
 * Find word in dictionary, returning address and execution token
 */

static int find_word(zf_ctx *ctx, const char *name, zf_addr *word, zf_addr *code)
{
	zf_addr w = LATEST(ctx);
	size_t namelen = strlen(name);

	while(w) {
		zf_cell link, d;
		zf_addr p = w;
		size_t len;
		p += dict_get_cell(ctx, p, &d);
		p += dict_get_cell(ctx, p, &link);
		len = ZF_FLAG_LEN((int)d);
		if(len == namelen) {
			dict_get_bytes(ctx, p, ctx->name_buf, len);
			if(memcmp(name, ctx->name_buf, len) == 0) {
				*word = w;
				*code = p + len;
				return 1;
			}
		}
		w = link;
	}

	return 0;
}


/*
 * Set 'immediate' flag in last compiled word
 */

static void make_immediate(zf_ctx *ctx)
{
	zf_cell lenflags;
	dict_get_cell(ctx, LATEST(ctx), &lenflags);
	dict_put_cell(ctx, LATEST(ctx), (int)lenflags | ZF_FLAG_IMMEDIATE);
}


/*
 * Inner interpreter
 */

static void run(zf_ctx *ctx, const char *input)
{
	while(ctx->ip != 0) {
		zf_cell d;
		zf_addr i, ip_org = ctx->ip;
		zf_addr l = dict_get_cell(ctx, ctx->ip, &d);
		zf_addr code = d;

		trace(ctx, "\n "ZF_ADDR_FMT " " ZF_ADDR_FMT " ", ctx->ip, code);
		for(i=0; i<RSP(ctx); i++) trace(ctx, "┊  ");
		
		ctx->ip += l;

		if(code < PRIM_COUNT) {
			do_prim(ctx, (zf_prim)code, input);

			/* If the prim requests input, restore IP so that the
			 * next time around we call the same prim again */

			if(ctx->input_state != ZF_INPUT_INTERPRET) {
				ctx->ip = ip_org;
				break;
			}

		} else {
			trace(ctx, "%s/" ZF_ADDR_FMT " ", op_name(ctx, code), code);
			zf_pushr(ctx, ctx->ip);
			ctx->ip = code;
		}

		input = NULL;
	} 
}


/*
 * Execute bytecode from given address
 */

static void execute(zf_ctx *ctx, zf_addr addr)
{
	ctx->ip = addr;
	RSP(ctx) = 0;
	ctx->fp = 0;
	zf_pushr(ctx, 0);

	trace(ctx, "\n[%s/" ZF_ADDR_FMT "] ", op_name(ctx, ctx->ip), ctx->ip);
	run(ctx, NULL);

}


/* 
 * Peek at memory, either user variables or dictionary memory,
 * returns number of bytes read
 */
static zf_addr peek(zf_ctx *ctx, zf_addr addr, zf_cell *val, zf_mem_size size)
{
	if(addr < ZF_USERVAR_COUNT) {
		/* Special case for user variables */
		*val = USERVAR(ctx)[addr];
		return 1;
	} else {
		/* General case for dictionary memory */
		return dict_get_cell_typed(ctx, addr, val, size);
	}

}

static void allot_addr(zf_ctx *ctx, zf_addr *addr, zf_cell delta)
{
	if(delta < 0) {
		zf_addr n = (zf_addr)(0 - (zf_ucell)delta);
		CHECK(ctx, *addr >= n, ZF_ABORT_OUTSIDE_MEM);
		*addr -= n;
	} else {
		zf_addr n = (zf_addr)delta;
		CHECK(ctx, *addr <= (zf_addr)-1 - n, ZF_ABORT_OUTSIDE_MEM);
		*addr += n;
	}
}


/*
 * Run primitive opcode
 */

#if ZF_ENABLE_DOUBLE_CELL || ZF_ENABLE_DFLOAT
/* Two-cell values are ( lo hi ): the high cell is on top of the stack */
static uint64_t zf_popud(zf_ctx *ctx)
{
	uint64_t hi = (zf_ucell)zf_pop(ctx);
	uint64_t lo = (zf_ucell)zf_pop(ctx);
	return (hi << 32) | lo;
}

static void zf_pushud(zf_ctx *ctx, uint64_t v)
{
	zf_push(ctx, (zf_cell)(zf_ucell)v);
	zf_push(ctx, (zf_cell)(zf_ucell)(v >> 32));
}

/* Push a two-cell literal, or compile it as two literals */
static void push_or_compile_ud(zf_ctx *ctx, uint64_t v)
{
	if(COMPILING(ctx)) {
		dict_add_lit(ctx, (zf_cell)(zf_ucell)v);
		dict_add_lit(ctx, (zf_cell)(zf_ucell)(v >> 32));
	} else {
		zf_pushud(ctx, v);
	}
}
#endif

#if ZF_ENABLE_DFLOAT
static double zf_popdf(zf_ctx *ctx)
{
	uint64_t u = zf_popud(ctx);
	double v;
	memcpy(&v, &u, sizeof(v));
	return v;
}

static void zf_pushdf(zf_ctx *ctx, double v)
{
	uint64_t u;
	memcpy(&u, &v, sizeof(u));
	zf_pushud(ctx, u);
}

/* Truncate toward zero, saturating out-of-range values and mapping NaN to 0 */
static zf_cell dfloat_to_cell_int(double v)
{
	if(v != v) return 0;
	if(v >= 2147483648.0) return (zf_cell)0x7fffffff;
	if(v < -2147483648.0) return (zf_cell)(-0x7fffffff - 1);
	return (zf_cell)v;
}

static int64_t dfloat_to_int64(double v)
{
	if(v != v) return 0;
	if(v >= 9223372036854775808.0) return INT64_MAX;
	if(v < -9223372036854775808.0) return INT64_MIN;
	return (int64_t)v;
}

/* Parse a double float literal: a decimal float with a d or D exponent in
 * place of e, as in Fortran (e.g. 1.5d0 6.02d23 -2.25d-3 1d6). Returns 0 if
 * buf is not one. */
static int parse_dfloat_literal(const char *buf, double *v)
{
	char tmp[sizeof(((zf_ctx *)0)->read_buf) + 1];
	const char *p = buf;
	char *d, *end;
	size_t len = strlen(buf);

	if(len >= sizeof(tmp)) return 0;
	if(*p == '-' || *p == '+') p++;
	if(!isdigit((unsigned char)*p) && !(*p == '.' && isdigit((unsigned char)p[1]))) return 0;
	if(p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) return 0;
	memcpy(tmp, buf, len + 1);
	d = strpbrk(tmp, "dD");
	if(d == NULL || strpbrk(tmp, "eE") != NULL || strpbrk(d + 1, "dD") != NULL) return 0;
	if(!isdigit((unsigned char)d[1]) &&
	   !((d[1] == '-' || d[1] == '+') && isdigit((unsigned char)d[2]))) return 0;
	*d = 'e';
	*v = strtod(tmp, &end);
	return end != tmp && *end == '\0';
}
#endif

#if ZF_ENABLE_DOUBLE_CELL

/* Parse a double-cell literal: an optionally signed decimal or 0x hex number
 * followed by a final '.', as in standard Forth (e.g. 123. -5. or 0xFF.),
 * from -2^63 to 2^64-1. Returns 0 if buf is not one. */
static int parse_double_literal(const char *buf, uint64_t *v)
{
	const char *p = buf;
	char *end;
	unsigned long long n;
	int neg = 0, hex;

	if(*p == '-' || *p == '+') {
		neg = *p == '-';
		p++;
	}
	if(!isdigit((unsigned char)*p)) return 0;
	hex = p[0] == '0' && (p[1] == 'x' || p[1] == 'X');
	errno = 0;
	n = strtoull(p, &end, hex ? 16 : 10);
	if(end[0] != '.' || end[1] != '\0' || errno == ERANGE) return 0;
	if(neg) {
		if(n > (1ULL << 63)) return 0;
		n = 0 - n;
	}
	*v = (uint64_t)n;
	return 1;
}
#endif

#if ZF_ENABLE_FLOAT
static float zf_popf(zf_ctx *ctx)
{
	return zf_cell_to_float(zf_pop(ctx));
}

static void zf_pushf(zf_ctx *ctx, float f)
{
	zf_push(ctx, zf_float_to_cell(f));
}

/* Truncate toward zero, saturating out-of-range values and mapping NaN to 0
 * (a plain C conversion is undefined for those) */
static zf_cell float_to_cell_int(float f)
{
	if(f != f) return 0;
	if(f >= 2147483648.0f) return (zf_cell)0x7fffffff;
	if(f < -2147483648.0f) return (zf_cell)(-0x7fffffff - 1);
	return (zf_cell)f;
}
#endif

/* Return-stack index of local k in the open frame, aborting if there is no
 * frame or k is out of range */
static zf_addr local_slot(zf_ctx *ctx, zf_cell k)
{
	zf_addr fp = ctx->fp;
	CHECK(ctx, fp >= 2 && fp <= RSP(ctx), ZF_ABORT_OUTSIDE_MEM);
	CHECK(ctx, k >= 0 && k < ctx->rstack[fp - 1], ZF_ABORT_OUTSIDE_MEM);
	CHECK(ctx, fp + (zf_addr)k < RSP(ctx), ZF_ABORT_OUTSIDE_MEM);
	return fp + (zf_addr)k;
}

static void do_prim(zf_ctx *ctx, zf_prim op, const char *input)
{
	zf_cell d1, d2, d3;
	zf_addr addr, code;
	zf_mem_size size;
#if ZF_ENABLE_DOUBLE_CELL
	uint64_t u1, u2;
	int64_t n1, n2;
#endif
#if ZF_ENABLE_DFLOAT
	double g1, g2;
#endif
#if ZF_ENABLE_FLOAT
	float f1, f2;
#endif

	trace(ctx, "(%s) ", op_name(ctx, op));

	switch(op) {

		case PRIM_COL:
			/* Start of word definition */
			if(input == NULL) {
				ctx->input_state = ZF_INPUT_PASS_WORD;
			} else {
				create(ctx, input, 0);
				COMPILING(ctx) = 1;
				locals_reset(ctx);
			}
			break;

		case PRIM_LTZ:
			/* Push true if less than zero, else false */
			zf_push(ctx, zf_pop(ctx) < 0 ? ZF_TRUE : ZF_FALSE);
			break;

		case PRIM_SEMICOL:
			/* End of word definition */
#if ZF_ENABLE_NAMED_LOCALS
			if(ctx->lframe) dict_add_op(ctx, PRIM_ENDLOCALS);
#endif
			dict_add_op(ctx, PRIM_EXIT);
			trace(ctx, "\n===");
			COMPILING(ctx) = 0;
			locals_reset(ctx);
			break;

		case PRIM_LITERAL:
			/* At compile time, compiles a value from the stack into the
			 * definition as a literal. At run time, the value will be pushed
			 * on the stack. */
			if(COMPILING(ctx)) dict_add_lit(ctx, zf_pop(ctx));
			/* FIXME: else abort "!compiling"? */
			break;

		case PRIM_CHECKPOINT:
			checkpoint_save(ctx, zf_pop(ctx));
			break;

		case PRIM_RESTORE:
			checkpoint_restore(ctx, zf_pop(ctx));
			break;

		case PRIM_DATA_HERE:
			if(ctx->data_compile) {
				zf_push(ctx, (zf_cell)((size_t)ZF_DATA_ADDR + (size_t)ctx->data_here));
			} else {
				zf_push(ctx, HERE(ctx));
			}
			break;

		case PRIM_DATA_ALLOT:
			d1 = zf_pop(ctx);
			if(ctx->data_compile) {
				zf_addr size = ctx->data_here;
				allot_addr(ctx, &size, d1);
				data_buf_reserve(ctx, size);
				ctx->data_here = size;
			} else {
				allot_addr(ctx, &HERE(ctx), d1);
			}
			break;

		case PRIM_LIT:
			/* At run time, push next value from dictionary on stack */
			ctx->ip += dict_get_cell(ctx, ctx->ip, &d1);
			zf_push(ctx, d1);
			break;

		case PRIM_EXIT:
			/* Return from word */
			ctx->ip = zf_popr(ctx);
			break;
		
		case PRIM_LEN:
			/* Get length of cell; consumes size encoding and address */
			size = zf_pop(ctx);
			addr = zf_pop(ctx);
			zf_push(ctx, peek(ctx, addr, &d1, size));
			break;

		case PRIM_PEEK:
			/* Peek at memory; consumes size encoding and address */
			size = zf_pop(ctx);
			addr = zf_pop(ctx);
			peek(ctx, addr, &d1, size);
			zf_push(ctx, d1);
			break;

		case PRIM_POKE:
			/* Poke memory; consumes size encoding, address, and value */
			size = zf_pop(ctx);
			addr = zf_pop(ctx);
			d1 = zf_pop(ctx);
			if(addr < ZF_USERVAR_COUNT) {
				USERVAR(ctx)[addr] = d1;
			} else {
				dict_put_cell_typed(ctx, addr, d1, size);
			}
			break;

		case PRIM_SWAP:
			/* Swap top two elements on stack */
			d1 = zf_pop(ctx); d2 = zf_pop(ctx);
			zf_push(ctx, d1); zf_push(ctx, d2);
			break;

		case PRIM_ROT:
			/* Rotate top three elements on stack */
			d1 = zf_pop(ctx); d2 = zf_pop(ctx); d3 = zf_pop(ctx);
			zf_push(ctx, d2); zf_push(ctx, d1); zf_push(ctx, d3);
			break;

		case PRIM_DROP:
			/* Drop top element from stack */
			zf_pop(ctx);
			break;

		case PRIM_DUP:
			/* Duplicate top element on stack */
			d1 = zf_pop(ctx);
			zf_push(ctx, d1); zf_push(ctx, d1);
			break;

		case PRIM_ADD:
			/* Pop and add top two elements on stack */
			d1 = zf_pop(ctx); d2 = zf_pop(ctx);
			zf_push(ctx, (zf_cell)((zf_ucell)d1 + (zf_ucell)d2));
			break;

		case PRIM_SYS:
			/* Perform host system call */
			d1 = zf_pop(ctx);
			ctx->input_state = zf_host_sys(ctx, (zf_syscall_id)d1, input);
			if(ctx->input_state != ZF_INPUT_INTERPRET) {
				zf_push(ctx, d1); /* re-push id to resume */
			}
			break;

		case PRIM_PICK:
			/* Pick n-th element from stack */
			addr = zf_pop(ctx);
			zf_push(ctx, zf_pick(ctx, addr));
			break;
		
		case PRIM_PICKR:
			/* Pick n-th element from return stack */
			addr = zf_pop(ctx);
			zf_push(ctx, zf_pickr(ctx, addr));
			break;

		case PRIM_SUB:
			/* Subtract top element on stack from next element */
			d1 = zf_pop(ctx); d2 = zf_pop(ctx);
			zf_push(ctx, (zf_cell)((zf_ucell)d2 - (zf_ucell)d1));
			break;

		case PRIM_MUL:
			/* Multiply top two elements on stack */
			d1 = zf_pop(ctx); d2 = zf_pop(ctx);
			zf_push(ctx, (zf_cell)((zf_ucell)d1 * (zf_ucell)d2));
			break;

		case PRIM_DIV:
			/* Divide next element on stack by top element */
			if((d2 = zf_pop(ctx)) == 0) {
				zf_abort(ctx, ZF_ABORT_DIVISION_BY_ZERO);
			}
			d1 = zf_pop(ctx);
			/* MIN / -1 overflows; wrap like the other operators */
			zf_push(ctx, d2 == -1 ? (zf_cell)(0 - (zf_ucell)d1) : d1 / d2);
			break;

		case PRIM_MOD:
			/* Modulo next element on stack by top element */
			if((d2 = zf_pop(ctx)) == 0) {
				zf_abort(ctx, ZF_ABORT_DIVISION_BY_ZERO);
			}
			d1 = zf_pop(ctx);
			zf_push(ctx, d2 == -1 ? 0 : d1 % d2);
			break;

		case PRIM_IMMEDIATE:
			/* Set immediate flag in next word */
			make_immediate(ctx);
			break;

		case PRIM_JMP:
			/* Jump to address */
			ctx->ip += dict_get_cell(ctx, ctx->ip, &d1);
			trace(ctx, "ip " ZF_ADDR_FMT "=>" ZF_ADDR_FMT, ctx->ip, (zf_addr)d1);
			ctx->ip = d1;
			break;

		case PRIM_JMP0:
			/* Jump to address if top of stack is zero */
			ctx->ip += dict_get_cell(ctx, ctx->ip, &d1);
			if(zf_pop(ctx) == 0) {
				trace(ctx, "ip " ZF_ADDR_FMT "=>" ZF_ADDR_FMT, ctx->ip, (zf_addr)d1);
				ctx->ip = d1;
			}
			break;

		case PRIM_TICK:
			/* Compile next word */
			if (COMPILING(ctx)) {
				ctx->ip += dict_get_cell(ctx, ctx->ip, &d1);
				trace(ctx, "%s/", op_name(ctx, d1));
				zf_push(ctx, d1);
			}
			else {
				if (input) {
					if (find_word(ctx, input,&addr,&code)) zf_push(ctx, code);
					else zf_abort(ctx, ZF_ABORT_NOT_A_WORD);
				}
				else ctx->input_state = ZF_INPUT_PASS_WORD;
			}
					
			break;

		case PRIM_COMMA:
			/* Compile literal value; consumes size encoding, value */
			size = zf_pop(ctx);
			d1 = zf_pop(ctx);
			dict_add_cell_typed(ctx, d1, size);
			break;

		case PRIM_COMMENT:
			/* Skip to matching ')' */
			if(!input || input[0] != ')') {
				ctx->input_state = ZF_INPUT_PASS_CHAR;
			}
			break;

		case PRIM_PUSHR:
			/* Push top of data stack to return stack */
			zf_pushr(ctx, zf_pop(ctx));
			break;

		case PRIM_POPR:
			/* Pop top of return stack to data stack */
			zf_push(ctx, zf_popr(ctx));
			break;

		case PRIM_EQUAL:
			/* Push true if top two elements on stack are equal, else false */
			zf_push(ctx, zf_pop(ctx) == zf_pop(ctx) ? ZF_TRUE : ZF_FALSE);
			break;

		case PRIM_KEY:
			/* Get next character from input stream */
			if(input == NULL) {
				ctx->input_state = ZF_INPUT_PASS_CHAR;
			} else {
				zf_push(ctx, input[0]);
			}
			break;

		case PRIM_LITS:
			/* Literal string */
			ctx->ip += dict_get_cell(ctx, ctx->ip, &d1);
			zf_push(ctx, ctx->ip);
			zf_push(ctx, d1);
			ctx->ip += d1;
			break;
		
		case PRIM_AND:
			/* Bitwise AND of top two elements on stack */
			zf_push(ctx, (zf_int)zf_pop(ctx) & (zf_int)zf_pop(ctx));
			break;

		case PRIM_OR:
			/* Bitwise OR of top two elements on stack */
			zf_push(ctx, (zf_int)zf_pop(ctx) | (zf_int)zf_pop(ctx));
			break;

		case PRIM_XOR:
			/* Bitwise XOR of top two elements on stack */
			zf_push(ctx, (zf_int)zf_pop(ctx) ^ (zf_int)zf_pop(ctx));
			break;

		case PRIM_SHL:
			/* Shift left of next element by top element */
			d1 = zf_pop(ctx); d2 = zf_pop(ctx);
			zf_push(ctx, d1 >= 0 && d1 < ZF_CELL_BITS ? (zf_cell)((zf_ucell)d2 << d1) : 0);
			break;

		case PRIM_SHR:
			/* Arithmetic shift right of next element by top element */
			d1 = zf_pop(ctx); d2 = zf_pop(ctx);
			if(d1 >= 0 && d1 < ZF_CELL_BITS) {
				zf_push(ctx, d2 >> d1);
			} else {
				zf_push(ctx, d2 < 0 ? -1 : 0);
			}
			break;

		case PRIM_RSHIFT:
			/* Logical shift right of next element by top element */
			d1 = zf_pop(ctx); d2 = zf_pop(ctx);
			zf_push(ctx, d1 >= 0 && d1 < ZF_CELL_BITS ? (zf_cell)((zf_ucell)d2 >> d1) : 0);
			break;

		/* Local variable frames on the return stack:
		 *
		 *   rstack[fp-2]  caller's fp
		 *   rstack[fp-1]  number of locals n
		 *   rstack[fp+k]  local k, 0 <= k < n
		 *
		 * fp is 0 when no frame is open. Addressing through fp keeps locals
		 * reachable however much >r, loops and nested calls push above them. */

		case PRIM_LOCALS:
			/* ( x0 ... xn-1 n -- ) open a frame, local k = xk */
			d1 = zf_pop(ctx);
			CHECK(ctx, d1 >= 0, ZF_ABORT_INVALID_SIZE);
			CHECK(ctx, (zf_addr)d1 <= DSP(ctx), ZF_ABORT_DSTACK_UNDERRUN);
			zf_pushr(ctx, (zf_cell)ctx->fp);
			zf_pushr(ctx, d1);
			addr = RSP(ctx);
			for(code = 0; code < (zf_addr)d1; code++) {
				zf_pushr(ctx, ctx->dstack[DSP(ctx) - (zf_addr)d1 + code]);
			}
			DSP(ctx) -= (zf_addr)d1;
			ctx->fp = addr;
			break;

		case PRIM_LGET:
			/* ( k -- x ) */
			d1 = zf_pop(ctx);
			addr = local_slot(ctx, d1);
			zf_push(ctx, ctx->rstack[addr]);
			break;

		case PRIM_LSET:
			/* ( x k -- ) */
			d1 = zf_pop(ctx);
			addr = local_slot(ctx, d1);
			ctx->rstack[addr] = zf_pop(ctx);
			break;

		case PRIM_2LGET:
			/* ( k -- lo hi ) two-cell local in slots k and k+1 */
			d1 = zf_pop(ctx);
			addr = local_slot(ctx, d1);
			code = local_slot(ctx, d1 + 1);
			zf_push(ctx, ctx->rstack[addr]);
			zf_push(ctx, ctx->rstack[code]);
			break;

		case PRIM_2LSET:
			/* ( lo hi k -- ) */
			d1 = zf_pop(ctx);
			addr = local_slot(ctx, d1);
			code = local_slot(ctx, d1 + 1);
			ctx->rstack[code] = zf_pop(ctx);
			ctx->rstack[addr] = zf_pop(ctx);
			break;

#if ZF_ENABLE_NAMED_LOCALS
		case PRIM_LBRACE:
			/* {: args | vals -- comment :} declares named locals for the
			 * definition being compiled. Called once per word of the
			 * declaration, each time asking for the next word. At :} it
			 * compiles "0 ... n locals"; afterwards a local's name compiles
			 * "k l@" (or "k 2l@" for d: locals) and ; closes the frame. */
			if(!COMPILING(ctx)) zf_abort(ctx, ZF_ABORT_COMPILE_ONLY_WORD);
			if(input == NULL) {
				CHECK(ctx, !ctx->lframe && ctx->lmode == LMODE_NONE, ZF_ABORT_BAD_LOCALS);
				ctx->lmode = LMODE_ARGS;
				ctx->input_state = ZF_INPUT_PASS_WORD;
				break;
			}
			if(strcmp(input, ":}") == 0) {
				CHECK(ctx, !ctx->ldouble, ZF_ABORT_BAD_LOCALS);
				for(code = ctx->largs; code < ctx->lslots; code++) dict_add_lit(ctx, 0);
				dict_add_lit(ctx, ctx->lslots);
				dict_add_op(ctx, PRIM_LOCALS);
				ctx->lframe = 1;
				ctx->lmode = LMODE_NONE;
				break;
			}
			if(ctx->lmode != LMODE_COMMENT) {
				if(strcmp(input, "|") == 0) {
					CHECK(ctx, ctx->lmode == LMODE_ARGS && !ctx->ldouble, ZF_ABORT_BAD_LOCALS);
					ctx->lmode = LMODE_VALS;
				} else if(strcmp(input, "--") == 0) {
					CHECK(ctx, !ctx->ldouble, ZF_ABORT_BAD_LOCALS);
					ctx->lmode = LMODE_COMMENT;
				} else if(strcmp(input, "d:") == 0) {
					CHECK(ctx, !ctx->ldouble, ZF_ABORT_BAD_LOCALS);
					ctx->ldouble = 1;
				} else {
					size_t len = strlen(input);
					int width = ctx->ldouble ? 2 : 1;
					/* A ; here almost certainly means a missing :} */
					CHECK(ctx, strcmp(input, ";") != 0, ZF_ABORT_BAD_LOCALS);
					CHECK(ctx, len <= 31 && ctx->lnames_len + 3 + len <= ZF_LOCAL_NAMES_SIZE, ZF_ABORT_BAD_LOCALS);
					CHECK(ctx, ctx->lslots + width <= 255, ZF_ABORT_BAD_LOCALS);
					ctx->lnames[ctx->lnames_len] = (char)len;
					ctx->lnames[ctx->lnames_len + 1] = (char)ctx->lslots;
					ctx->lnames[ctx->lnames_len + 2] = (char)width;
					memcpy(&ctx->lnames[ctx->lnames_len + 3], input, len);
					ctx->lnames_len += 3 + len;
					ctx->lslots += width;
					if(ctx->lmode == LMODE_ARGS) ctx->largs += width;
					ctx->ldouble = 0;
				}
			}
			ctx->input_state = ZF_INPUT_PASS_WORD;
			break;

		case PRIM_TO:
			/* x to name: compile a store into a named local */
			if(!COMPILING(ctx)) zf_abort(ctx, ZF_ABORT_COMPILE_ONLY_WORD);
			if(input == NULL) {
				ctx->input_state = ZF_INPUT_PASS_WORD;
			} else {
				int width = 1;
				d1 = 0;
				if(!ctx->lframe || !local_find(ctx, input, &d1, &width)) {
					zf_abort(ctx, ZF_ABORT_NOT_A_WORD);
				}
				dict_add_lit(ctx, d1);
				dict_add_op(ctx, width == 2 ? PRIM_2LSET : PRIM_LSET);
			}
			break;
#endif

		case PRIM_ENDLOCALS:
			/* Close the frame, dropping anything pushed above it */
			CHECK(ctx, ctx->fp >= 2 && ctx->fp <= RSP(ctx), ZF_ABORT_RSTACK_UNDERRUN);
			addr = ctx->fp;
			ctx->fp = (zf_addr)ctx->rstack[addr - 2];
			RSP(ctx) = addr - 2;
			break;

		case PRIM_LT:
			/* Signed less-than of next element and top element. A primitive
			 * rather than "- <0", which gives the wrong answer when the
			 * subtraction overflows */
			d1 = zf_pop(ctx); d2 = zf_pop(ctx);
			zf_push(ctx, d2 < d1 ? ZF_TRUE : ZF_FALSE);
			break;

		case PRIM_ULT:
			/* Unsigned less-than of next element and top element */
			d1 = zf_pop(ctx); d2 = zf_pop(ctx);
			zf_push(ctx, (zf_ucell)d2 < (zf_ucell)d1 ? ZF_TRUE : ZF_FALSE);
			break;

		case PRIM_UDIV:
			/* Unsigned divide next element by top element */
			if((d2 = zf_pop(ctx)) == 0) {
				zf_abort(ctx, ZF_ABORT_DIVISION_BY_ZERO);
			}
			d1 = zf_pop(ctx);
			zf_push(ctx, (zf_cell)((zf_ucell)d1 / (zf_ucell)d2));
			break;

		case PRIM_UMOD:
			/* Unsigned modulo next element by top element */
			if((d2 = zf_pop(ctx)) == 0) {
				zf_abort(ctx, ZF_ABORT_DIVISION_BY_ZERO);
			}
			d1 = zf_pop(ctx);
			zf_push(ctx, (zf_cell)((zf_ucell)d1 % (zf_ucell)d2));
			break;

#if ZF_ENABLE_DOUBLE_CELL
		/* 64-bit double cells, ( lo hi ). d+ d- and ud* give the same bits
		 * for signed and unsigned doubles. */

		case PRIM_DADD:
			u2 = zf_popud(ctx); u1 = zf_popud(ctx);
			zf_pushud(ctx, u1 + u2);
			break;

		case PRIM_DSUB:
			u2 = zf_popud(ctx); u1 = zf_popud(ctx);
			zf_pushud(ctx, u1 - u2);
			break;

		case PRIM_DULT:
			u2 = zf_popud(ctx); u1 = zf_popud(ctx);
			zf_push(ctx, u1 < u2 ? ZF_TRUE : ZF_FALSE);
			break;

		case PRIM_UMSTAR:
			/* Multiply two unsigned cells into a double */
			d1 = zf_pop(ctx); d2 = zf_pop(ctx);
			zf_pushud(ctx, (uint64_t)(zf_ucell)d1 * (zf_ucell)d2);
			break;

		case PRIM_UDSTAR:
			u2 = zf_popud(ctx); u1 = zf_popud(ctx);
			zf_pushud(ctx, u1 * u2);
			break;

		case PRIM_UDDIV:
			if((u2 = zf_popud(ctx)) == 0) {
				zf_abort(ctx, ZF_ABORT_DIVISION_BY_ZERO);
			}
			u1 = zf_popud(ctx);
			zf_pushud(ctx, u1 / u2);
			break;

		case PRIM_UDMOD:
			if((u2 = zf_popud(ctx)) == 0) {
				zf_abort(ctx, ZF_ABORT_DIVISION_BY_ZERO);
			}
			u1 = zf_popud(ctx);
			zf_pushud(ctx, u1 % u2);
			break;

		case PRIM_DLT:
			/* Signed double less-than */
			n2 = (int64_t)zf_popud(ctx); n1 = (int64_t)zf_popud(ctx);
			zf_push(ctx, n1 < n2 ? ZF_TRUE : ZF_FALSE);
			break;

		case PRIM_MSTAR:
			/* Multiply two signed cells into a double */
			d1 = zf_pop(ctx); d2 = zf_pop(ctx);
			zf_pushud(ctx, (uint64_t)((int64_t)d1 * (int64_t)d2));
			break;

		case PRIM_DDIV:
			/* Signed double divide, truncating toward zero */
			if((n2 = (int64_t)zf_popud(ctx)) == 0) {
				zf_abort(ctx, ZF_ABORT_DIVISION_BY_ZERO);
			}
			n1 = (int64_t)zf_popud(ctx);
			/* MIN / -1 overflows; wrap like the other operators */
			zf_pushud(ctx, n2 == -1 ? 0 - (uint64_t)n1 : (uint64_t)(n1 / n2));
			break;

		case PRIM_DMOD:
			if((n2 = (int64_t)zf_popud(ctx)) == 0) {
				zf_abort(ctx, ZF_ABORT_DIVISION_BY_ZERO);
			}
			n1 = (int64_t)zf_popud(ctx);
			zf_pushud(ctx, n2 == -1 ? 0 : (uint64_t)(n1 % n2));
			break;

#endif

#if ZF_ENABLE_FLOAT
		/* Floats are IEEE single-precision bit patterns in cells. f/ follows
		 * IEEE rules: dividing by zero gives an infinity, not an abort. */

		case PRIM_FADD:
			f2 = zf_popf(ctx); f1 = zf_popf(ctx);
			zf_pushf(ctx, f1 + f2);
			break;

		case PRIM_FSUB:
			f2 = zf_popf(ctx); f1 = zf_popf(ctx);
			zf_pushf(ctx, f1 - f2);
			break;

		case PRIM_FMUL:
			f2 = zf_popf(ctx); f1 = zf_popf(ctx);
			zf_pushf(ctx, f1 * f2);
			break;

		case PRIM_FDIV:
			f2 = zf_popf(ctx); f1 = zf_popf(ctx);
			zf_pushf(ctx, f1 / f2);
			break;

		case PRIM_FLT:
			f2 = zf_popf(ctx); f1 = zf_popf(ctx);
			zf_push(ctx, f1 < f2 ? ZF_TRUE : ZF_FALSE);
			break;

		case PRIM_FEQ:
			f2 = zf_popf(ctx); f1 = zf_popf(ctx);
			zf_push(ctx, f1 == f2 ? ZF_TRUE : ZF_FALSE);
			break;

		case PRIM_STOF:
			zf_pushf(ctx, (float)zf_pop(ctx));
			break;

		case PRIM_UTOF:
			zf_pushf(ctx, (float)(zf_ucell)zf_pop(ctx));
			break;

		case PRIM_FTOS:
			zf_push(ctx, float_to_cell_int(zf_popf(ctx)));
			break;

		case PRIM_FSQRT:
			zf_pushf(ctx, sqrtf(zf_popf(ctx)));
			break;

		case PRIM_FFLOOR:
			zf_pushf(ctx, floorf(zf_popf(ctx)));
			break;

		case PRIM_FCEIL:
			zf_pushf(ctx, ceilf(zf_popf(ctx)));
			break;

		case PRIM_FROUND:
			zf_pushf(ctx, roundf(zf_popf(ctx)));
			break;

		case PRIM_FTRUNC:
			zf_pushf(ctx, truncf(zf_popf(ctx)));
			break;
#endif

#if ZF_ENABLE_DFLOAT
		/* Double-precision floats: IEEE bit patterns in two cells, ( lo hi ) */

		case PRIM_DFADD:
			g2 = zf_popdf(ctx); g1 = zf_popdf(ctx);
			zf_pushdf(ctx, g1 + g2);
			break;

		case PRIM_DFSUB:
			g2 = zf_popdf(ctx); g1 = zf_popdf(ctx);
			zf_pushdf(ctx, g1 - g2);
			break;

		case PRIM_DFMUL:
			g2 = zf_popdf(ctx); g1 = zf_popdf(ctx);
			zf_pushdf(ctx, g1 * g2);
			break;

		case PRIM_DFDIV:
			g2 = zf_popdf(ctx); g1 = zf_popdf(ctx);
			zf_pushdf(ctx, g1 / g2);
			break;

		case PRIM_DFLT:
			g2 = zf_popdf(ctx); g1 = zf_popdf(ctx);
			zf_push(ctx, g1 < g2 ? ZF_TRUE : ZF_FALSE);
			break;

		case PRIM_DFEQ:
			g2 = zf_popdf(ctx); g1 = zf_popdf(ctx);
			zf_push(ctx, g1 == g2 ? ZF_TRUE : ZF_FALSE);
			break;

		case PRIM_STODF:
			zf_pushdf(ctx, (double)zf_pop(ctx));
			break;

		case PRIM_DFTOS:
			zf_push(ctx, dfloat_to_cell_int(zf_popdf(ctx)));
			break;

		case PRIM_DTODF:
			zf_pushdf(ctx, (double)(int64_t)zf_popud(ctx));
			break;

		case PRIM_DFTOD:
			zf_pushud(ctx, (uint64_t)dfloat_to_int64(zf_popdf(ctx)));
			break;

		case PRIM_DFSQRT:
			zf_pushdf(ctx, sqrt(zf_popdf(ctx)));
			break;

		case PRIM_DFFLOOR:
			zf_pushdf(ctx, floor(zf_popdf(ctx)));
			break;

		case PRIM_DFCEIL:
			zf_pushdf(ctx, ceil(zf_popdf(ctx)));
			break;

		case PRIM_DFROUND:
			zf_pushdf(ctx, round(zf_popdf(ctx)));
			break;

		case PRIM_DFTRUNC:
			zf_pushdf(ctx, trunc(zf_popdf(ctx)));
			break;

#if ZF_ENABLE_FLOAT
		case PRIM_FTODF:
			zf_pushdf(ctx, (double)zf_popf(ctx));
			break;

		case PRIM_DFTOF:
			zf_pushf(ctx, (float)zf_popdf(ctx));
			break;
#endif
#endif

		default:
			zf_abort(ctx, ZF_ABORT_INTERNAL_ERROR);
			break;
	}
}


/*
 * Handle incoming word. Compile or interpreted the word, or pass it to a
 * deferred primitive if it requested a word from the input stream.
 */

static void handle_word(zf_ctx *ctx, const char *buf)
{
	zf_addr w, c = 0;
	int found;

	/* If a word was requested by an earlier operation, resume with the new
	 * word */

	if(ctx->input_state == ZF_INPUT_PASS_WORD) {
		ctx->input_state = ZF_INPUT_INTERPRET;
		run(ctx, buf);
		return;
	}

#if ZF_ENABLE_NAMED_LOCALS
	/* Named locals shadow dictionary words in the definition */
	if(COMPILING(ctx) && ctx->lframe) {
		zf_cell slot;
		int width;
		if(local_find(ctx, buf, &slot, &width)) {
			dict_add_lit(ctx, slot);
			dict_add_op(ctx, width == 2 ? PRIM_2LGET : PRIM_LGET);
			return;
		}
	}
#endif

	/* Look up the word in the dictionary */

	found = find_word(ctx, buf, &w, &c);

	if(found) {

		/* Word found: compile or execute, depending on flags and state */

		zf_cell d;
		int flags;
		dict_get_cell(ctx, w, &d);
		flags = d;

		if(COMPILING(ctx) && (POSTPONE(ctx) || !(flags & ZF_FLAG_IMMEDIATE))) {
			if(flags & ZF_FLAG_PRIM) {
				dict_get_cell(ctx, c, &d);
#if ZF_ENABLE_NAMED_LOCALS
				/* exit leaves the definition, so close its named frame */
				if(d == PRIM_EXIT && ctx->lframe) dict_add_op(ctx, PRIM_ENDLOCALS);
#endif
				dict_add_op(ctx, d);
			} else {
				dict_add_op(ctx, c);
			}
			POSTPONE(ctx) = 0;
		} else {
			execute(ctx, c);
		}
	} else {

		/* Word not found: try to convert to a number and compile or push, depending
		 * on state */

		zf_cell v;
#if ZF_ENABLE_DOUBLE_CELL
		uint64_t ud;
		if(parse_double_literal(buf, &ud)) {
			push_or_compile_ud(ctx, ud);
			return;
		}
#endif
#if ZF_ENABLE_DFLOAT
		double df;
		if(parse_dfloat_literal(buf, &df)) {
			uint64_t bits;
			memcpy(&bits, &df, sizeof(bits));
			push_or_compile_ud(ctx, bits);
			return;
		}
#endif
		v = zf_host_parse_num(ctx, buf);

		if(COMPILING(ctx)) {
			dict_add_lit(ctx, v);
		} else {
			zf_push(ctx, v);
		}
	}
}


/*
 * Handle one character. Split into words to pass to handle_word(), or pass the
 * char to a deferred prim if it requested a character from the input stream
 */

static void handle_char(zf_ctx *ctx, char c)
{
	if(ctx->input_state == ZF_INPUT_PASS_CHAR) {

		ctx->input_state = ZF_INPUT_INTERPRET;
		run(ctx, &c);

	} else if(c != '\0' && !isspace(c)) {

		if(ctx->read_len < sizeof(ctx->read_buf)-1) {
			ctx->read_buf[ctx->read_len++] = c;
			ctx->read_buf[ctx->read_len] = '\0';
		}

	} else {

		if(ctx->read_len > 0) {
			ctx->read_len = 0;
			handle_word(ctx, ctx->read_buf);
		}
	}
}


/*
 * Initialisation
 */

zf_result zf_init_checked(zf_ctx *ctx, int enable_trace)
{
	if(ctx == NULL) {
		return ZF_ABORT_INTERNAL_ERROR;
	}

	ctx->input_state = ZF_INPUT_INTERPRET;
	ctx->ip = 0;
	ctx->abort_jmp_valid = 0;
	ctx->abort_reason = ZF_OK;
	ctx->read_len = 0;
	ctx->data_base = 0;
	ctx->data_len = 0;
	ctx->data_here = 0;
	ctx->data_compile = 0;
#if ZF_ENABLE_ROM_DICT
	ctx->rom_dict = NULL;
	ctx->rom_len = 0;
	ctx->dict_base = 0;
#endif

	#if ZF_ENABLE_DYNAMIC_DICT
	ctx->data_buf = NULL;
	ctx->data_buf_cap = 0;
	ctx->dict_max = ZF_DICT_MAX_SIZE;
	ctx->dict_cap = ZF_DICT_INITIAL_SIZE;
	if(ctx->dict_max != 0 && ctx->dict_cap > ctx->dict_max) {
		ctx->dict_cap = ctx->dict_max;
	}
	ctx->dict = (uint8_t *)ZF_REALLOC(NULL, ctx->dict_cap);
	if(ctx->dict == NULL) {
		ctx->dict_cap = 0;
		return ZF_ABORT_OUTSIDE_MEM;
	}
	memset(ctx->dict, 0, ctx->dict_cap);
	#else
	memset(ctx->dict, 0, sizeof(ctx->dict));
	#endif

	HERE(ctx) = ZF_USERVAR_COUNT * sizeof(zf_addr);
	LATEST(ctx) = 0;
	TRACE(ctx) = enable_trace;
	COMPILING(ctx) = 0;
	locals_reset(ctx);
	POSTPONE(ctx) = 0;
	DSP(ctx) = 0;
	RSP(ctx) = 0;
	ctx->fp = 0;
	uservars_to_image(ctx);

	return ZF_OK;
}

void zf_init(zf_ctx *ctx, int enable_trace)
{
	zf_result r = zf_init_checked(ctx, enable_trace);
	if(r != ZF_OK) {
		zf_abort(ctx, r);
	}
}

void zf_free(zf_ctx *ctx)
{
	#if ZF_ENABLE_DYNAMIC_DICT
	ZF_FREE(ctx->dict);
	ctx->dict = NULL;
	ctx->dict_cap = 0;
	ZF_FREE(ctx->data_buf);
	ctx->data_buf = NULL;
	ctx->data_buf_cap = 0;
	#else
	(void)ctx;
	#endif
}

zf_result zf_dict_set_limit(zf_ctx *ctx, size_t max)
{
	if(ctx == NULL) return ZF_ABORT_INTERNAL_ERROR;
	#if ZF_ENABLE_DYNAMIC_DICT
	ctx->dict_max = max;
	#else
	(void)max;
	#endif
	return ZF_OK;
}


#if ZF_ENABLE_BOOTSTRAP

/*
 * Functions for bootstrapping the dictionary by adding all primitive ops and the
 * user variables.
 */

static void add_prim(zf_ctx *ctx, const char *name, zf_prim op)
{
	int imm = 0;

	if(name[0] == '_') {
		name ++;
		imm = 1;
	}

	create(ctx, name, ZF_FLAG_PRIM);
	dict_add_op(ctx, op);
	dict_add_op(ctx, PRIM_EXIT);
	if(imm) make_immediate(ctx);
}

static void add_uservar(zf_ctx *ctx, const char *name, zf_addr addr)
{
	create(ctx, name, 0);
	dict_add_lit(ctx, addr);
	dict_add_op(ctx, PRIM_EXIT);
}

static void add_const(zf_ctx *ctx, const char *name, zf_cell value)
{
	create(ctx, name, 0);
	dict_add_lit(ctx, value);
	dict_add_op(ctx, PRIM_EXIT);
}


void zf_bootstrap(zf_ctx *ctx)
{

	/* Add primitives and user variables to dictionary */

	zf_addr i = 0;
	const char *p;
	for(p=prim_names; *p; p+=strlen(p)+1) {
		add_prim(ctx, p, (zf_prim)i++);
	} 

	i = 0;
	for(p=uservar_names; *p; p+=strlen(p)+1) {
		add_uservar(ctx, p, i++);
	}

	add_const(ctx, "var-max-size", 1 + sizeof(zf_cell));
	add_const(ctx, "chkpt-size", sizeof(zf_checkpoint));
	add_const(ctx, "cell", sizeof(zf_cell));
}

#else 
void zf_bootstrap(zf_ctx *ctx) { (void)ctx; }
#endif


/*
 * Eval forth string
 */

zf_result zf_eval(zf_ctx *ctx, const char *buf)
{
	zf_result r;

	#if ZF_ENABLE_DYNAMIC_DICT
	if(ctx == NULL || ctx->dict == NULL) {
		return ZF_ABORT_OUTSIDE_MEM;
	}
	#else
	if(ctx == NULL) {
		return ZF_ABORT_INTERNAL_ERROR;
	}
	#endif
	if(buf == NULL) {
		return ZF_ABORT_EXTERNAL;
	}

	ctx->abort_jmp_valid++;
	r = (zf_result)setjmp(ctx->jmpbuf);

	if(r == ZF_OK) {
		for(;;) {
			handle_char(ctx, *buf);
			if(*buf == '\0') {
				ctx->abort_jmp_valid--;
				ctx->abort_reason = ZF_OK;
				return ZF_OK;
			}
			buf ++;
		}
	} else {
		ctx->abort_jmp_valid--;
		COMPILING(ctx) = 0;
		locals_reset(ctx);
		RSP(ctx) = 0;
		ctx->fp = 0;
		DSP(ctx) = 0;
		return r;
	}
}


void *zf_dump(zf_ctx *ctx, size_t *len)
{
	if(ctx == NULL) {
		if(len) *len = 0;
		return NULL;
	}
	/* A mounted ROM, or an image imported with a data window, can't be dumped
	 * as a plain image: its variables live outside the dictionary. */
	if(DICT_BASE(ctx) != 0 || ctx->data_len != 0) {
		if(len) *len = 0;
		return NULL;
	}
	uservars_to_image(ctx);
	if(len) *len = dict_capacity(ctx);
	return ctx->dict;
}

void zf_dict_set_data_compile(zf_ctx *ctx, int enable)
{
	if(ctx != NULL) {
		ctx->data_compile = enable ? 1 : 0;
		if(enable) ctx->data_here = 0;
	}
}

size_t zf_dict_data_size(zf_ctx *ctx)
{
	if(ctx == NULL) return 0;
	return (size_t)(ctx->data_compile ? ctx->data_here : ctx->data_len);
}

const void *zf_dict_data(zf_ctx *ctx)
{
	if(ctx == NULL || zf_dict_data_size(ctx) == 0) return NULL;
	if(ctx->data_compile) {
#if ZF_ENABLE_DYNAMIC_DICT
		return ctx->data_buf;
#else
		return NULL;
#endif
	}
	return ctx->dict + ((size_t)ctx->data_base - (size_t)DICT_BASE(ctx));
}

size_t zf_dict_size(zf_ctx *ctx)
{
	return (size_t)HERE(ctx);
}

size_t zf_dict_capacity(zf_ctx *ctx)
{
	return dict_capacity(ctx);
}

static zf_result dict_import_prepare(zf_ctx *ctx, const void *buf, size_t len, zf_addr trace)
{
	#if ZF_ENABLE_DYNAMIC_DICT
	if(ctx == NULL || ctx->dict == NULL) {
		return ZF_ABORT_OUTSIDE_MEM;
	}
	#else
	if(ctx == NULL) {
		return ZF_ABORT_INTERNAL_ERROR;
	}
	#endif
	if(buf == NULL || len < ZF_USERVAR_COUNT * sizeof(zf_addr)) {
		return ZF_ABORT_OUTSIDE_MEM;
	}

	#if ZF_ENABLE_DYNAMIC_DICT
	if(dict_grow(ctx, 0, len) != ZF_OK) {
		return ZF_ABORT_OUTSIDE_MEM;
	}
	#else
	if(len > dict_writable_capacity(ctx)) {
		return ZF_ABORT_OUTSIDE_MEM;
	}
	#endif

#if ZF_ENABLE_ROM_DICT
	ctx->rom_dict = NULL;
	ctx->rom_len = 0;
	ctx->dict_base = 0;
#endif
	ctx->data_base = 0;
	ctx->data_len = 0;
	ctx->data_here = 0;
	ctx->data_compile = 0;

	memcpy(ctx->dict, buf, len);
	uservars_from_image(ctx, buf);
	TRACE(ctx) = trace;
	COMPILING(ctx) = 0;
	locals_reset(ctx);
	POSTPONE(ctx) = 0;
	DSP(ctx) = 0;
	RSP(ctx) = 0;
	ctx->fp = 0;

	if((size_t)HERE(ctx) > dict_capacity(ctx)) {
		return ZF_ABORT_OUTSIDE_MEM;
	}

	return ZF_OK;
}

zf_result zf_dict_import(zf_ctx *ctx, const void *buf, size_t len)
{
	zf_addr trace;
	if(ctx == NULL) return ZF_ABORT_INTERNAL_ERROR;
	trace = TRACE(ctx);
	return dict_import_prepare(ctx, buf, len, trace);
}

zf_result zf_dict_import_with_data(zf_ctx *ctx, const void *buf, size_t len, size_t data_len)
{
	zf_result r;
	zf_addr trace;
	zf_addr data_base;
	size_t image_len;

	if(ctx == NULL) return ZF_ABORT_INTERNAL_ERROR;
	if(buf == NULL || data_len > len) return ZF_ABORT_OUTSIDE_MEM;
	image_len = len - data_len;
	trace = TRACE(ctx);
	r = dict_import_prepare(ctx, buf, image_len, trace);
	if(r != ZF_OK) return r;
	if(data_len > (size_t)((zf_addr)-1)) return ZF_ABORT_OUTSIDE_MEM;

	data_base = HERE(ctx);
	if(data_base > (zf_addr)-1 - (zf_addr)data_len) return ZF_ABORT_OUTSIDE_MEM;
	#if ZF_ENABLE_DYNAMIC_DICT
	if(dict_grow(ctx, 0, (size_t)data_base + data_len) != ZF_OK) return ZF_ABORT_OUTSIDE_MEM;
	#else
	if(!dict_has_writable_range(ctx, data_base, data_len)) return ZF_ABORT_OUTSIDE_MEM;
	#endif
	memcpy(ctx->dict + ((size_t)data_base - (size_t)DICT_BASE(ctx)), (const uint8_t *)buf + image_len, data_len);
	ctx->data_base = data_base;
	ctx->data_len = (zf_addr)data_len;
	HERE(ctx) = data_base + (zf_addr)data_len;
	uservars_to_image(ctx);
	return ZF_OK;
}

zf_result zf_dict_mount_rom(zf_ctx *ctx, const void *buf, size_t len, size_t data_len)
{
#if ZF_ENABLE_ROM_DICT
	zf_addr trace;
	size_t rom_len;

	#if ZF_ENABLE_DYNAMIC_DICT
	if(ctx == NULL || ctx->dict == NULL) {
		return ZF_ABORT_OUTSIDE_MEM;
	}
	#else
	if(ctx == NULL) {
		return ZF_ABORT_INTERNAL_ERROR;
	}
	#endif
	if(buf == NULL || data_len > len || len > (size_t)((zf_addr)-1) ||
	   len - data_len < ZF_USERVAR_COUNT * sizeof(zf_addr)) {
		return ZF_ABORT_OUTSIDE_MEM;
	}
	rom_len = len - data_len;

	#if ZF_ENABLE_DYNAMIC_DICT
	if(dict_grow(ctx, rom_len, data_len) != ZF_OK) {
		return ZF_ABORT_OUTSIDE_MEM;
	}
	#else
	if(data_len > dict_writable_capacity(ctx)) {
		return ZF_ABORT_OUTSIDE_MEM;
	}
	#endif

	trace = TRACE(ctx);
	ctx->rom_dict = (const uint8_t *)buf;
	ctx->rom_len = (zf_addr)rom_len;
	ctx->dict_base = (zf_addr)rom_len;
	ctx->data_base = (zf_addr)rom_len;
	ctx->data_len = (zf_addr)data_len;
	ctx->data_here = 0;
	ctx->data_compile = 0;

	memset(ctx->dict, 0, dict_writable_capacity(ctx));
	memcpy(ctx->dict, (const uint8_t *)buf + rom_len, data_len);
	uservars_from_image(ctx, buf);
	TRACE(ctx) = trace;
	COMPILING(ctx) = 0;
	locals_reset(ctx);
	POSTPONE(ctx) = 0;
	DSP(ctx) = 0;
	RSP(ctx) = 0;
	ctx->fp = 0;
	HERE(ctx) = (zf_addr)len;

	if((size_t)HERE(ctx) > dict_capacity(ctx)) {
		return ZF_ABORT_OUTSIDE_MEM;
	}

	return ZF_OK;
#else
	return zf_dict_import_with_data(ctx, buf, len, data_len);
#endif
}

#if ZF_ENABLE_FLOAT
zf_cell zf_float_to_cell(float f)
{
	zf_cell v;
	memcpy(&v, &f, sizeof(v));
	return v;
}

float zf_cell_to_float(zf_cell v)
{
	float f;
	memcpy(&f, &v, sizeof(f));
	return f;
}
#endif

#if ZF_ENABLE_DFLOAT
double zf_cells_to_dfloat(zf_cell lo, zf_cell hi)
{
	uint64_t u = ((uint64_t)(zf_ucell)hi << 32) | (zf_ucell)lo;
	double v;
	memcpy(&v, &u, sizeof(v));
	return v;
}

void zf_dfloat_to_cells(double v, zf_cell *lo, zf_cell *hi)
{
	uint64_t u;
	memcpy(&u, &v, sizeof(u));
	*lo = (zf_cell)(zf_ucell)u;
	*hi = (zf_cell)(zf_ucell)(u >> 32);
}
#endif

int zf_parse_num(const char *buf, zf_cell *v)
{
	const char *p = buf;
	char *end;
	long long n;
	int hex;

	if(*p == '-' || *p == '+') p++;
	if(*p == '\0') return 0;
	hex = p[0] == '0' && (p[1] == 'x' || p[1] == 'X');

#if ZF_ENABLE_FLOAT
	if(!hex && strpbrk(p, ".eE") != NULL) {
		float f;
		if(buf[strlen(buf) - 1] == '.') return 0;
		f = strtof(buf, &end);
		if(end == buf || *end != '\0') return 0;
		*v = zf_float_to_cell(f);
		return 1;
	}
#endif

	n = strtoll(buf, &end, hex ? 16 : 10);
	if(end == buf || *end != '\0') return 0;
	if(n < -2147483647LL - 1 || n > 4294967295LL) return 0;
	*v = (zf_cell)(zf_ucell)n;
	return 1;
}

zf_result zf_uservar_set(zf_ctx *ctx, zf_uservar_id uv, zf_cell v)
{
	zf_result result = ZF_ABORT_INVALID_USERVAR;

	if (uv < ZF_USERVAR_COUNT) {
		USERVAR(ctx)[uv] = v;
		result = ZF_OK;
	}

	return result;
}

zf_result zf_uservar_get(zf_ctx *ctx, zf_uservar_id uv, zf_cell *v)
{
	zf_result result = ZF_ABORT_INVALID_USERVAR;

	if (uv < ZF_USERVAR_COUNT) {
		if (v != NULL) {
			*v = USERVAR(ctx)[uv];
		}
		result = ZF_OK;
	}

	return result;
}

/*
 * End
 */
