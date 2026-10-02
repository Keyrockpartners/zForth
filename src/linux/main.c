
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include <math.h>
#include <ctype.h>
#include <inttypes.h>

#ifdef USE_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

#include "zforth.h"

#if ZF_LINUX_ROM_DICT
#include "zforth_dict.h"
#endif


/*
 * Evaluate buffer with code, check return value and report errors
 */

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
	char buf[256];

	FILE *f = fopen(fname, "rb");
	int line = 1;
	if(f) {
		while(fgets(buf, sizeof(buf), f)) {
			do_eval(ctx, fname, line++, buf);
		}
		fclose(f);
	} else {
		fprintf(stderr, "error opening file '%s': %s\n", fname, strerror(errno));
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
 * u (unsigned), x X (hex) and c take one cell, f e g one float cell; with the
 * l prefix d u x X take a 64-bit double cell and f e g a double float, both
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
	if(j >= n || fmt[j] == '\0' || !strchr("duxXcfegs%", fmt[j])) return 0;
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

static void fmt_syscall(zf_ctx *ctx)
{
	zf_cell fmt_len_cell = zf_pop(ctx);
	zf_cell fmt_addr_cell = zf_pop(ctx);
	const uint8_t *fmt = checked_dict_range(ctx, fmt_addr_cell, fmt_len_cell);
	size_t fmt_len = (size_t)fmt_len_cell;
	zf_cell args[ZF_FMT_MAX_ARG_CELLS];
	fmt_spec sp;
	char cspec[32];
	int cells = 0;
	int ai;
	size_t i;

	for(i = 0; i < fmt_len; i++) {
		if(fmt[i] == '%' && fmt_parse(fmt, fmt_len, i + 1, &sp)) {
			cells += fmt_cells(&sp);
			i += sp.len;
		}
	}
	if(cells > ZF_FMT_MAX_ARG_CELLS) {
		zf_abort(ctx, ZF_ABORT_EXTERNAL);
	}

	/* args[0] is the top of the stack, i.e. the last argument */
	for(ai = 0; ai < cells; ai++) {
		args[ai] = zf_pop(ctx);
	}

	ai = cells - 1;
	for(i = 0; i < fmt_len; i++) {
		if(fmt[i] != '%' || !fmt_parse(fmt, fmt_len, i + 1, &sp)) {
			putchar((char)fmt[i]);
			continue;
		}
		i += sp.len;

		switch(sp.verb) {
			case '%':
				putchar('%');
				break;

			case 'd': case 'u': case 'x': case 'X': {
				const char *conv;
				if(sp.lng) {
					uint64_t lo = (zf_ucell)args[ai--];
					uint64_t hi = (zf_ucell)args[ai--];
					uint64_t v = (hi << 32) | lo;
					conv = sp.verb == 'd' ? PRId64 : sp.verb == 'u' ? PRIu64 : sp.verb == 'x' ? PRIx64 : PRIX64;
					fmt_cspec(cspec, sizeof(cspec), &sp, conv);
					if(sp.verb == 'd') printf(cspec, (int64_t)v);
					else printf(cspec, v);
				} else {
					zf_cell v = args[ai--];
					conv = sp.verb == 'd' ? PRId32 : sp.verb == 'u' ? PRIu32 : sp.verb == 'x' ? PRIx32 : PRIX32;
					fmt_cspec(cspec, sizeof(cspec), &sp, conv);
					if(sp.verb == 'd') printf(cspec, (int32_t)v);
					else printf(cspec, (uint32_t)v);
				}
				break;
			}

			case 'c':
				fmt_cspec(cspec, sizeof(cspec), &sp, "c");
				printf(cspec, (int)(unsigned char)args[ai--]);
				break;

			case 'f': case 'e': case 'g': {
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
				printf(cspec, v);
				break;
			}

			case 's': {
				zf_cell addr = args[ai--];
				zf_cell len = args[ai--];
				const uint8_t *str = checked_dict_range(ctx, addr, len);
				int n = (int)len;
				fmt_spec ws = sp;
				if(sp.prec >= 0 && sp.prec < n) n = sp.prec;
				ws.prec = -1;
				fmt_cspec(cspec, sizeof(cspec), &ws, ".*s");
				printf(cspec, n, (const char *)str);
				break;
			}
		}
	}

	fflush(stdout);
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
		"usage: zfort [options] [src ...]\n"
		"\n"
		"Options:\n"
		"   -h         show help\n"
		"   -H NAME    write loaded dictionary as C header to stdout\n"
		"   -t         enable tracing\n"
		"   -l FILE    load dictionary from FILE\n"
		"   -q         quiet\n"
	);
}


/*
 * Main
 */

int main(int argc, char **argv)
{
	int i;
	int c;
	int trace = 0;
	int line = 0;
	int quiet = 0;
	const char *header_name = NULL;
	const char *fname_load = NULL;

	/* Parse command line options */

	while((c = getopt(argc, argv, "hH:l:tq")) != -1) {
		switch(c) {
			case 'H':
				header_name = optarg;
				break;
			case 't':
				trace = 1;
				break;
			case 'l':
				fname_load = optarg;
				break;
			case 'h':
				usage();
				exit(0);
			case 'q':
				quiet = 1;
				break;
		}
	}
	
	argc -= optind;
	argv += optind;

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

	for(i=0; i<argc; i++) {
		include(ctx, argv[i]);
	}

	if(header_name) {
		emit_header(ctx, header_name);
		zf_free(ctx);
		free(ctx);
		return 0;
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
	return 0;
}


/*
 * End
 */
