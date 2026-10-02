
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <ctype.h>
#include <inttypes.h>
#include <time.h>
#include <unistd.h>

#ifdef USE_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

#include "zforth.h"

#if ZF_LINUX_ROM_DICT
/* -DZF_DICT_HEADER='"path/image.h"' selects another prebuilt image; the
 * default "zforth_dict.h" is found next to this file first */
#ifdef ZF_DICT_HEADER
#include ZF_DICT_HEADER
#else
#include "zforth_dict.h"
#endif
#endif


/*
 * Evaluate buffer with code, check return value and report errors
 */

/* Set when evaluating a file or -e word fails, for the exit status */
static int had_error = 0;

zf_result do_eval(zf_ctx *ctx, const char *src, int line, const char *buf)
{
	const char *msg = NULL;

	zf_result rv = zf_eval(ctx, buf);

	switch(rv)
	{
		case ZF_OK: break;
		case ZF_ABORT_INTERNAL_ERROR: msg = "internal error"; break;
		case ZF_ABORT_OUTSIDE_MEM: msg = "outside memory"; break;
		case ZF_ABORT_DSTACK_OVERRUN: msg = "dstack overrun"; break;
		case ZF_ABORT_DSTACK_UNDERRUN: msg = "dstack underrun"; break;
		case ZF_ABORT_RSTACK_OVERRUN: msg = "rstack overrun"; break;
		case ZF_ABORT_RSTACK_UNDERRUN: msg = "rstack underrun"; break;
		case ZF_ABORT_NOT_A_WORD: msg = "not a word"; break;
		case ZF_ABORT_COMPILE_ONLY_WORD: msg = "compile-only word"; break;
		case ZF_ABORT_INVALID_SIZE: msg = "invalid size"; break;
		case ZF_ABORT_DIVISION_BY_ZERO: msg = "division by zero"; break;
		case ZF_ABORT_BAD_LOCALS: msg = "bad locals declaration"; break;
		case ZF_ABORT_BOUNDS: msg = "index out of range"; break;
		case ZF_ABORT_USER: msg = "aborted"; break;
		default: msg = "unknown error";
	}

	if(msg) {
		fprintf(stderr, "\033[31m");
		if(src) fprintf(stderr, "%s:%d: ", src, line);
		fprintf(stderr, "%s\033[0m\n", msg);
	}

	return rv;
}


/*
 * Load given forth file
 */

void include(zf_ctx *ctx, const char *fname)
{
	char buf[4096];

	FILE *f = fopen(fname, "rb");
	int line = 1;
	if(f) {
		while(fgets(buf, sizeof(buf), f)) {
			if(do_eval(ctx, fname, line++, buf) != ZF_OK) had_error = 1;
		}
		fclose(f);
	} else {
		fprintf(stderr, "error opening file '%s': %s\n", fname, strerror(errno));
		had_error = 1;
	}
}


/*
 * Save dictionary
 */

static void save(zf_ctx *ctx, const char *fname)
{
	size_t len = zf_dict_size(ctx);
	void *p = zf_dump(ctx, NULL);
	FILE *f;
	if(p == NULL) {
		fprintf(stderr, "dictionary dump unavailable for prebuilt dictionary\n");
		return;
	}
	f = fopen(fname, "wb");
	if(f) {
		fwrite(p, 1, len, f);
		fclose(f);
	}
}


/*
 * Load dictionary
 */

static void load(zf_ctx *ctx, const char *fname)
{
	FILE *f = fopen(fname, "rb");
	if(f) {
		long file_size;
		void *buf;
		zf_result rv;

		if(fseek(f, 0, SEEK_END) != 0) {
			perror("read");
			fclose(f);
			return;
		}

		file_size = ftell(f);
		if(file_size < 0) {
			perror("read");
			fclose(f);
			return;
		}

		if(fseek(f, 0, SEEK_SET) != 0) {
			perror("read");
			fclose(f);
			return;
		}

		buf = malloc((size_t)file_size);
		if(buf == NULL) {
			fprintf(stderr, "out of memory while loading '%s'\n", fname);
			fclose(f);
			return;
		}

		if(fread(buf, 1, (size_t)file_size, f) != (size_t)file_size) {
			perror("read");
			free(buf);
			fclose(f);
			return;
		}

		rv = zf_dict_import(ctx, buf, (size_t)file_size);
		free(buf);
		fclose(f);

		if(rv != ZF_OK) {
			fprintf(stderr, "error loading dictionary '%s'\n", fname);
		}
	} else {
		perror("read");
	}
}


/*
 * Emit dictionary as C header
 */

static int is_ident_start(int c)
{
	return isalpha((unsigned char)c) || c == '_';
}

static int is_ident_char(int c)
{
	return isalnum((unsigned char)c) || c == '_';
}

static void make_symbol_name(const char *input, char *output, size_t output_size)
{
	size_t i = 0;

	if(output_size == 0) {
		return;
	}

	if(input == NULL || input[0] == '\0') {
		input = "zforth_dict";
	}

	if(!is_ident_start((unsigned char)input[0])) {
		output[i++] = '_';
	}

	for(; *input != '\0' && i + 1 < output_size; input++) {
		output[i++] = is_ident_char((unsigned char)*input) ? *input : '_';
	}

	output[i] = '\0';

	if(output[0] == '\0') {
		strncpy(output, "zforth_dict", output_size - 1);
		output[output_size - 1] = '\0';
	}
}

static void make_include_guard(const char *symbol, char *guard, size_t guard_size)
{
	size_t i;

	if(guard_size == 0) {
		return;
	}

	for(i = 0; symbol[i] != '\0' && i + 1 < guard_size; i++) {
		char c = symbol[i];
		guard[i] = isalnum((unsigned char)c) ? (char)toupper((unsigned char)c) : '_';
	}

	if(i + sizeof("_H") <= guard_size) {
		guard[i++] = '_';
		guard[i++] = 'H';
	}

	guard[i] = '\0';

	if(guard[0] == '\0') {
		strncpy(guard, "ZFORTH_DICT_H", guard_size - 1);
		guard[guard_size - 1] = '\0';
	}
}

static void emit_header(zf_ctx *ctx, const char *name)
{
	char symbol[128];
	char guard[132];
	const unsigned char *dict = (const unsigned char *)zf_dump(ctx, NULL);
	const unsigned char *data = (const unsigned char *)zf_dict_data(ctx);
	size_t dict_len = zf_dict_size(ctx);
	size_t data_len = zf_dict_data_size(ctx);
	size_t len = dict_len + data_len;
	size_t i;
	if(dict == NULL) {
		fprintf(stderr, "dictionary dump unavailable for prebuilt dictionary\n");
		return;
	}

	make_symbol_name(name, symbol, sizeof(symbol));
	make_include_guard(symbol, guard, sizeof(guard));

	printf("#ifndef %s\n", guard);
	printf("#define %s\n\n", guard);
	printf("#include <stddef.h>\n\n");
	printf("/* Dictionary image followed by the initial contents of the data window\n");
	printf(" * (the last %s_data_len bytes) */\n", symbol);
	printf("static const unsigned char %s[] = {\n", symbol);

	for(i = 0; i < len; i++) {
		if((i % 12) == 0) {
			printf("    ");
		}

		printf("0x%02x", i < dict_len ? dict[i] : data[i - dict_len]);
		if(i + 1 < len) {
			printf(", ");
		}

		if((i % 12) == 11 || i + 1 == len) {
			printf("\n");
		}
	}

	if(len == 0) {
		printf("\n");
	}

	printf("};\n");
	printf("static const size_t %s_len = sizeof(%s);\n", symbol, symbol);
	printf("static const size_t %s_data_len = %zu;\n\n", symbol, data_len);
	printf("#endif\n");
}


#define ZF_FMT_MAX_ARG_CELLS 16

static const uint8_t *checked_dict_range(zf_ctx *ctx, zf_cell addr, zf_cell len)
{
	if(addr < 0 || len < 0) {
		zf_abort(ctx, ZF_ABORT_OUTSIDE_MEM);
	}
	return (const uint8_t *)zf_dict_addr(ctx, (zf_addr)addr, (size_t)len);
}

/*
 * fmt: printf-style output with C conventions. A placeholder is
 * %[flags][width][.precision][l]verb, flags from "-0+ #". Verbs d (signed),
 * u (unsigned), x X (hex) and c take one cell, f e E g G one float cell; with
 * the l prefix d u x X take a 64-bit double cell and f e E g G a double float,
 * both
 * ( lo hi ). s takes ( addr len ). %% is a literal %. Anything else is
 * printed as is and consumes no argument.
 */

typedef struct {
	char flags[6];
	int width;    /* -1 if absent */
	int prec;     /* -1 if absent */
	int lng;      /* l prefix: two-cell argument */
	char verb;
	size_t len;   /* characters after the % */
} fmt_spec;

static int fmt_digits(const uint8_t *fmt, size_t n, size_t *j)
{
	int v = 0;
	while(*j < n && isdigit(fmt[*j])) {
		if(v < 1000) v = v * 10 + (fmt[*j] - '0');
		(*j)++;
	}
	return v > 255 ? 255 : v;
}

/* Parse the placeholder after the % at fmt[i]; returns 0 if it isn't one */
static int fmt_parse(const uint8_t *fmt, size_t n, size_t i, fmt_spec *sp)
{
	size_t j = i;
	int k = 0;

	memset(sp, 0, sizeof(*sp));
	while(j < n && fmt[j] != '\0' && strchr("-0+ #", fmt[j])) {
		if(k < 5) sp->flags[k++] = (char)fmt[j];
		j++;
	}
	sp->width = (j < n && isdigit(fmt[j])) ? fmt_digits(fmt, n, &j) : -1;
	sp->prec = -1;
	if(j < n && fmt[j] == '.') {
		j++;
		sp->prec = fmt_digits(fmt, n, &j);
	}
	if(j < n && fmt[j] == 'l') {
		sp->lng = 1;
		j++;
	}
	if(j >= n || fmt[j] == '\0' || !strchr("duxXcfeEgGs%", fmt[j])) return 0;
	sp->verb = (char)fmt[j];
	if(sp->lng && strchr("cs%", sp->verb)) return 0;
	sp->len = j - i + 1;
	return 1;
}

static int fmt_cells(const fmt_spec *sp)
{
	if(sp->verb == '%') return 0;
	if(sp->verb == 's') return 2;
	return sp->lng ? 2 : 1;
}

/* Build the C format for a placeholder: % flags width .prec, then conv */
static void fmt_cspec(char *out, size_t size, const fmt_spec *sp, const char *conv)
{
	char width[8] = "", prec[8] = "";
	if(sp->width >= 0) snprintf(width, sizeof(width), "%d", sp->width);
	if(sp->prec >= 0) snprintf(prec, sizeof(prec), ".%d", sp->prec);
	snprintf(out, size, "%%%s%s%s%s", sp->flags, width, prec, conv);
}

/* Where fmt output goes: stdout, or a C buffer of cap bytes that keeps the
 * first cap bytes and drops the rest */
typedef struct {
	char *buf;    /* NULL for stdout */
	size_t cap;
	size_t len;
} fmt_sink;

static void sink_write(fmt_sink *out, const char *p, size_t n)
{
	if(out->buf == NULL) {
		(void)fwrite(p, 1, n, stdout);
		return;
	}
	if(out->len < out->cap) {
		size_t room = out->cap - out->len;
		memcpy(out->buf + out->len, p, n < room ? n : room);
	}
	out->len += n;
}

static void sink_printf(fmt_sink *out, const char *cfmt, ...)
{
	char tmp[600];
	int n;
	va_list va;
	va_start(va, cfmt);
	n = vsnprintf(tmp, sizeof(tmp), cfmt, va);
	va_end(va);
	if(n < 0) return;
	if((size_t)n >= sizeof(tmp)) n = sizeof(tmp) - 1;
	sink_write(out, tmp, (size_t)n);
}

static void sink_pad(fmt_sink *out, int n)
{
	while(n-- > 0) sink_write(out, " ", 1);
}

/* Number of argument cells the placeholders in fmt take */
static int fmt_arg_cells(const uint8_t *fmt, size_t fmt_len)
{
	fmt_spec sp;
	int cells = 0;
	size_t i;
	for(i = 0; i < fmt_len; i++) {
		if(fmt[i] == '%' && fmt_parse(fmt, fmt_len, i + 1, &sp)) {
			cells += fmt_cells(&sp);
			i += sp.len;
		}
	}
	return cells;
}

/* Format fmt[0..fmt_len) with arguments popped from the stack into out */
static void fmt_format(zf_ctx *ctx, const uint8_t *fmt, size_t fmt_len, fmt_sink *out)
{
	zf_cell args[ZF_FMT_MAX_ARG_CELLS];
	fmt_spec sp;
	char cspec[32];
	int cells = fmt_arg_cells(fmt, fmt_len);
	int ai;
	size_t i;

	if(cells > ZF_FMT_MAX_ARG_CELLS) {
		zf_abort(ctx, ZF_ABORT_EXTERNAL);
	}

	/* args[0] is the top of the stack, i.e. the last argument */
	for(ai = 0; ai < cells; ai++) {
		args[ai] = zf_pop(ctx);
	}

	ai = cells - 1;
	for(i = 0; i < fmt_len; i++) {
		size_t start = i;
		if(fmt[i] != '%' || !fmt_parse(fmt, fmt_len, i + 1, &sp)) {
			/* Copy the run of plain text up to the next % in one go */
			while(i + 1 < fmt_len && fmt[i + 1] != '%') i++;
			sink_write(out, (const char *)fmt + start, i - start + 1);
			continue;
		}
		i += sp.len;

		switch(sp.verb) {
			case '%':
				sink_write(out, "%", 1);
				break;

			case 'd': case 'u': case 'x': case 'X': {
				const char *conv;
				if(sp.lng) {
					uint64_t lo = (zf_ucell)args[ai--];
					uint64_t hi = (zf_ucell)args[ai--];
					uint64_t v = (hi << 32) | lo;
					conv = sp.verb == 'd' ? PRId64 : sp.verb == 'u' ? PRIu64 : sp.verb == 'x' ? PRIx64 : PRIX64;
					fmt_cspec(cspec, sizeof(cspec), &sp, conv);
					if(sp.verb == 'd') sink_printf(out, cspec, (int64_t)v);
					else sink_printf(out, cspec, v);
				} else {
					zf_cell v = args[ai--];
					conv = sp.verb == 'd' ? PRId32 : sp.verb == 'u' ? PRIu32 : sp.verb == 'x' ? PRIx32 : PRIX32;
					fmt_cspec(cspec, sizeof(cspec), &sp, conv);
					if(sp.verb == 'd') sink_printf(out, cspec, (int32_t)v);
					else sink_printf(out, cspec, (uint32_t)v);
				}
				break;
			}

			case 'c':
				fmt_cspec(cspec, sizeof(cspec), &sp, "c");
				sink_printf(out, cspec, (int)(unsigned char)args[ai--]);
				break;

			case 'f': case 'e': case 'E': case 'g': case 'G': {
				char conv[2] = { sp.verb, '\0' };
				double v;
				if(sp.lng) {
					zf_cell lo = args[ai--];
					zf_cell hi = args[ai--];
					v = zf_cells_to_dfloat(lo, hi);
				} else {
					v = zf_cell_to_float(args[ai--]);
				}
				fmt_cspec(cspec, sizeof(cspec), &sp, conv);
				sink_printf(out, cspec, v);
				break;
			}

			case 's': {
				zf_cell addr = args[ai--];
				zf_cell len = args[ai--];
				const uint8_t *str = checked_dict_range(ctx, addr, len);
				int n = (int)len;
				int pad;
				if(sp.prec >= 0 && sp.prec < n) n = sp.prec;
				pad = sp.width > n ? sp.width - n : 0;
				if(!strchr(sp.flags, '-')) sink_pad(out, pad);
				sink_write(out, (const char *)str, (size_t)n);
				if(strchr(sp.flags, '-')) sink_pad(out, pad);
				break;
			}
		}
	}
}

/* fmt ( args... fmt-addr fmt-len -- ): format to stdout */
static void fmt_syscall(zf_ctx *ctx)
{
	zf_cell fmt_len = zf_pop(ctx);
	zf_cell fmt_addr = zf_pop(ctx);
	const uint8_t *fmt = checked_dict_range(ctx, fmt_addr, fmt_len);
	fmt_sink out = { NULL, 0, 0 };
	fmt_format(ctx, fmt, (size_t)fmt_len, &out);
	fflush(stdout);
}

/* fmt-buf ( buf-addr buf-len args... fmt-addr fmt-len -- n ): format into a
 * dictionary buffer, truncating to buf-len bytes; n is the number of bytes
 * written. The output is built in C memory and copied in one write, since a
 * dictionary write may move the dictionary. */
static void fmt_buf_syscall(zf_ctx *ctx)
{
	zf_cell fmt_len = zf_pop(ctx);
	zf_cell fmt_addr = zf_pop(ctx);
	const uint8_t *fmt = checked_dict_range(ctx, fmt_addr, fmt_len);
	static char *scratch = NULL;
	static size_t scratch_cap = 0;
	fmt_sink out = { NULL, 0, 0 };
	zf_cell buf_len, buf_addr;
	size_t n;

	/* buf-len sits just below the arguments */
	buf_len = zf_pick(ctx, (zf_addr)fmt_arg_cells(fmt, (size_t)fmt_len));
	out.cap = buf_len > 0 ? (size_t)buf_len : 0;
	/* A static scratch buffer, grown as needed, so an abort while
	 * formatting can't leak it */
	if(out.cap + 1 > scratch_cap) {
		char *p = realloc(scratch, out.cap + 1);
		if(p == NULL) zf_abort(ctx, ZF_ABORT_OUTSIDE_MEM);
		scratch = p;
		scratch_cap = out.cap + 1;
	}
	out.buf = scratch;
	fmt_format(ctx, fmt, (size_t)fmt_len, &out);
	buf_len = zf_pop(ctx);
	buf_addr = zf_pop(ctx);
	n = out.len < out.cap ? out.len : out.cap;
	if(n > 0) {
		(void)checked_dict_range(ctx, buf_addr, (zf_cell)n);
		zf_dict_write_bytes(ctx, (zf_addr)buf_addr, out.buf, n);
	}
	zf_push(ctx, (zf_cell)n);
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
			const void *buf = checked_dict_range(ctx, addr, len);
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
			include(ctx, input);
			break;
		
		case ZF_SYSCALL_USER + 3:
			save(ctx, "zforth.save");
			break;

		case ZF_SYSCALL_USER + 4:
			fmt_syscall(ctx);
			break;

		case ZF_SYSCALL_USER + 5:
			fmt_buf_syscall(ctx);
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
			printf("unhandled syscall %d\n", id);
			break;
	}

	return ZF_INPUT_INTERPRET;
}


/*
 * Tracing output
 */

void zf_host_trace(zf_ctx *ctx, const char *fmt, va_list va)
{
	fprintf(stderr, "\033[1;30m");
	vfprintf(stderr, fmt, va);
	fprintf(stderr, "\033[0m");
}


/*
 * Parse number
 */

zf_cell zf_host_parse_num(zf_ctx *ctx, const char *buf)
{
	zf_cell v;
	if(!zf_parse_num(buf, &v)) {
		zf_abort(ctx, ZF_ABORT_NOT_A_WORD);
	}
	return v;
}


void usage(void)
{
	fprintf(stderr, 
		"usage: zforth [options] [src | -e WORD ...]\n"
		"\n"
		"Source files and -e words are run in command-line order.\n"
		"\n"
		"Options:\n"
		"   -h         show help\n"
		"   -H NAME    write loaded dictionary as C header to stdout\n"
		"   -t         enable tracing\n"
		"   -l FILE    load dictionary from FILE\n"
		"   -q         quiet\n"
		"   -e WORD    evaluate WORD (any Forth text) after the files before it\n"
		"   -x         exit after the files and -e words instead of reading stdin\n"
		"\n"
		"The exit status is 1 if a file or -e word failed.\n"
	);
}


/*
 * Main
 */

int main(int argc, char **argv)
{
	int i;
	int trace = 0;
	int line = 0;
	int quiet = 0;
	int no_repl = 0;
	const char *header_name = NULL;
	const char *fname_load = NULL;
	/* Files and -e words in command-line order; is_word marks the -e ones */
	const char **run = calloc((size_t)argc + 1, sizeof(*run));
	char *is_word = calloc((size_t)argc + 1, 1);
	int nrun = 0;

	if(run == NULL || is_word == NULL) {
		fprintf(stderr, "out of memory\n");
		return 1;
	}

	/* Parse command line options. Options may appear anywhere; -e words
	 * and files keep their relative order. */

	for(i = 1; i < argc; i++) {
		const char *a = argv[i];
		if(strcmp(a, "--") == 0) {
			for(i++; i < argc; i++) run[nrun++] = argv[i];
			break;
		}
		if(a[0] != '-' || a[1] == '\0' || a[2] != '\0') {
			run[nrun++] = a;
			continue;
		}
		switch(a[1]) {
			case 'H': case 'l': case 'e':
				if(i + 1 >= argc) {
					usage();
					return 1;
				}
				if(a[1] == 'H') header_name = argv[++i];
				else if(a[1] == 'l') fname_load = argv[++i];
				else { is_word[nrun] = 1; run[nrun++] = argv[++i]; }
				break;
			case 't':
				trace = 1;
				break;
			case 'q':
				quiet = 1;
				break;
			case 'x':
				no_repl = 1;
				break;
			case 'h':
				usage();
				exit(0);
			default:
				usage();
				return 1;
		}
	}

	zf_ctx *ctx = malloc(sizeof(zf_ctx));

	/* Initialize zforth */

	zf_init(ctx, trace);


	/* Load dict from disk if requested, otherwise bootstrap fort
	 * dictionary */

	if(fname_load) {
		load(ctx, fname_load);
	} else {
#if ZF_LINUX_ROM_DICT
		zf_result rv = zf_dict_mount_rom(ctx, zforth_dict, zforth_dict_len, zforth_dict_data_len);
		if(rv != ZF_OK) {
			fprintf(stderr, "error mounting built-in ROM dictionary\n");
			zf_free(ctx);
			free(ctx);
			return 1;
		}
#else
		zf_bootstrap(ctx);
#endif
	}

	if(header_name) {
		zf_dict_set_data_compile(ctx, 1);
	}


	/* Include files from command line */

	for(i = 0; i < nrun; i++) {
		if(is_word[i]) {
			if(do_eval(ctx, "-e", 1, run[i]) != ZF_OK) had_error = 1;
		} else {
			include(ctx, run[i]);
		}
	}
	free(run);
	free(is_word);

	if(header_name) {
		emit_header(ctx, header_name);
		zf_free(ctx);
		free(ctx);
		return had_error;
	}

	if(no_repl) {
		zf_free(ctx);
		free(ctx);
		return had_error;
	}

	if(!quiet) {
		zf_cell here;
		zf_uservar_get(ctx, ZF_USERVAR_HERE, &here);
		printf("Welcome to zForth, %d bytes used\n", (int)here);
	}

	/* Interactive interpreter: read a line using readline library,
	 * and pass to zf_eval() for evaluation*/

#ifdef USE_READLINE

	read_history(".zforth.hist");

	for(;;) {

		char *buf = readline("");
		if(buf == NULL) break;

		if(strlen(buf) > 0) {

			do_eval(ctx, "stdin", ++line, buf);
			printf("\n");

			add_history(buf);
			write_history(".zforth.hist");

		}

		free(buf);
	}
#else
	for(;;) {
		char buf[4096];
		if(fgets(buf, sizeof(buf), stdin)) {
			do_eval(ctx, "stdin", ++line, buf);
			printf("\n");
		} else {
			break;
		}
	}
#endif

	zf_free(ctx);
	free(ctx);
	return had_error;
}


/*
 * End
 */
