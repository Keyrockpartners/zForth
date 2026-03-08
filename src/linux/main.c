
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include <math.h>
#include <ctype.h>

#ifdef USE_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

#include "zforth.h"



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
	FILE *f = fopen(fname, "wb");
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
	size_t len = zf_dict_size(ctx);
	size_t i;

	make_symbol_name(name, symbol, sizeof(symbol));
	make_include_guard(symbol, guard, sizeof(guard));

	printf("#ifndef %s\n", guard);
	printf("#define %s\n\n", guard);
	printf("#include <stddef.h>\n\n");
	printf("static const unsigned char %s[] = {\n", symbol);

	for(i = 0; i < len; i++) {
		if((i % 12) == 0) {
			printf("    ");
		}

		printf("0x%02x", dict[i]);
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
	printf("static const size_t %s_len = sizeof(%s);\n\n", symbol, symbol);
	printf("#endif\n");
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
			size_t cap = zf_dict_capacity(ctx);
			if(addr < 0 || len < 0 || (size_t)addr > cap || (size_t)len > cap - (size_t)addr) {
				zf_abort(ctx, ZF_ABORT_OUTSIDE_MEM);
			}
			void *buf = (uint8_t *)zf_dump(ctx, NULL) + (int)addr;
			(void)fwrite(buf, 1, len, stdout);
			fflush(stdout); }
			break;


		/* Application specific callbacks */

		case ZF_SYSCALL_USER + 0:
			printf("\n");
			exit(0);
			break;

		case ZF_SYSCALL_USER + 1:
			zf_push(ctx, sin(zf_pop(ctx)));
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
	int n = 0;
	int r = sscanf(buf, ZF_SCAN_FMT"%n", &v, &n);
	if(r != 1 || buf[n] != '\0') {
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
		zf_bootstrap(ctx);
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
