/*
 * The command line, file and dictionary loading, and the interactive loop
 */

#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <poll.h>
#include <unistd.h>

#ifdef USE_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

#include "host.h"



/*
 * Evaluate buffer with code, check return value and report errors
 */

/* Set when evaluating a file or -e word fails, for the exit status */
static int had_error = 0;

/* Print why an evaluation aborted, if it did */
static zf_result report(const char *src, int line, zf_result rv)
{
	const char *msg = NULL;

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
#ifdef ZF_EXT_RESULT_MESSAGES
		ZF_EXT_RESULT_MESSAGES
#endif
		case ZF_ABORT_IMAGE_MISMATCH: msg = "dictionary image built with a different configuration"; break;
		default: msg = "unknown error";
	}

	if(msg) {
		fprintf(stderr, "\033[31m");
		if(src) fprintf(stderr, "%s:%d: ", src, line);
		fprintf(stderr, "%s\033[0m\n", msg);
	}

	return rv;
}

static zf_result do_eval(zf_ctx *ctx, const char *src, int line, const char *buf)
{
	return report(src, line, zf_eval(ctx, buf));
}


zf_result zfl_eval(zf_ctx *ctx, const char *src, int line, const char *text)
{
	return do_eval(ctx, src, line, text);
}

zf_result zfl_execute(zf_ctx *ctx, const char *src, zf_addr xt)
{
	return report(src, 0, zf_execute(ctx, xt));
}


/*
 * The REPL with a poll function: wait for input at most poll_ms at a time
 * and call poll in between; it ends with its input, like the plain REPL
 */

static zf_ctx *poll_ctx;
static int poll_line;

static void eval_line(char *buf)
{
	if(strlen(buf) > 0) {
		do_eval(poll_ctx, "stdin", ++poll_line, buf);
		printf("\n");
		fflush(stdout);
#ifdef USE_READLINE
		add_history(buf);
		write_history(".zforth.hist");
#endif
	}
}

#ifdef USE_READLINE
static int stdin_open = 1;

static void readline_line(char *buf)
{
	if(buf == NULL) {
		rl_callback_handler_remove();
		stdin_open = 0;
		return;
	}
	eval_line(buf);
	free(buf);
}
#endif

static void repl_poll(zf_ctx *ctx, const zfl_config *cfg)
{
	/* returns at the end of input */
	char buf[4096];
	size_t len = 0;
	int open = 1;
#ifdef USE_READLINE
	int tty = isatty(0);
	if(tty) {
		read_history(".zforth.hist");
		rl_callback_handler_install("", readline_line);
	}
#endif
	poll_ctx = ctx;
	for(;;) {
		struct pollfd pfd = { 0, POLLIN, 0 };
		int n;
#ifdef USE_READLINE
		if(tty) open = stdin_open;
#endif
		if(!open) {
#ifdef USE_READLINE
			if(tty) printf("\n");
#endif
			return;
		}
		n = poll(&pfd, 1, cfg->poll_ms);
		if(n > 0 && open) {
#ifdef USE_READLINE
			if(tty) {
				rl_callback_read_char();
				cfg->poll(ctx);
				continue;
			}
#endif
			ssize_t r = read(0, buf + len, sizeof(buf) - 1 - len);
			if(r <= 0) {
				/* end of input: evaluate a last unterminated line */
				open = 0;
				buf[len] = '\0';
				if(len > 0) eval_line(buf);
				len = 0;
			} else {
				char *p, *start = buf;
				len += (size_t)r;
				buf[len] = '\0';
				while((p = memchr(start, '\n', len - (size_t)(start - buf))) != NULL) {
					*p = '\0';
					eval_line(start);
					start = p + 1;
				}
				len -= (size_t)(start - buf);
				memmove(buf, start, len);
				if(len == sizeof(buf) - 1) {
					/* a line too long for the buffer: evaluate what we have */
					buf[len] = '\0';
					eval_line(buf);
					len = 0;
				}
			}
		}
		cfg->poll(ctx);
	}
}


/*
 * Load given forth file
 */

/* Reads a whole line of any length into *buf (grown as needed); returns 0
 * at the end of the file. A line is never split, so a token can't be cut
 * in two. */
static int read_line(FILE *f, char **buf, size_t *cap)
{
	size_t len = 0;
	if(*buf == NULL) {
		*cap = 4096;
		*buf = malloc(*cap);
		if(*buf == NULL) return 0;
	}
	for(;;) {
		if(fgets(*buf + len, (int)(*cap - len), f) == NULL) return len > 0;
		len += strlen(*buf + len);
		if(len > 0 && (*buf)[len - 1] == '\n') return 1;
		if(len + 1 >= *cap) {
			char *p = realloc(*buf, *cap * 2);
			if(p == NULL) return 1;
			*buf = p;
			*cap *= 2;
		}
	}
}

void zfl_include(zf_ctx *ctx, const char *fname)
{
	char *buf = NULL;
	size_t cap = 0;

	FILE *f = fopen(fname, "rb");
	int line = 1;
	if(f) {
		while(read_line(f, &buf, &cap)) {
			if(do_eval(ctx, fname, line++, buf) != ZF_OK) had_error = 1;
		}
		free(buf);
		fclose(f);
	} else {
		fprintf(stderr, "error opening file '%s': %s\n", fname, strerror(errno));
		had_error = 1;
	}
}


/*
 * Save dictionary
 */

void zfl_save(zf_ctx *ctx, const char *fname)
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

		if(rv == ZF_ABORT_IMAGE_MISMATCH) {
			fprintf(stderr, "error loading dictionary '%s': built with different feature flags\n", fname);
		} else if(rv != ZF_OK) {
			fprintf(stderr, "error loading dictionary '%s'\n", fname);
		}
	} else {
		perror("read");
	}
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


static void usage(void)
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
 * Main: run the command line, then the REPL
 */

int zfl_main(int argc, char **argv, const zfl_config *cfg)
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

	zfl_set_sys(cfg->sys);

	/* Initialize zforth */

	zf_init(ctx, trace);


	/* Load dict from disk if requested, otherwise bootstrap fort
	 * dictionary */

	if(fname_load) {
		load(ctx, fname_load);
	} else {
		if(cfg->rom) {
			zf_result rv = zf_dict_mount_rom(ctx, cfg->rom, cfg->rom_len, cfg->rom_data_len);
			if(rv == ZF_ABORT_IMAGE_MISMATCH) {
				fprintf(stderr, "error mounting built-in ROM dictionary: built with different feature flags\n");
				zf_free(ctx);
				free(ctx);
				return 1;
			}
			if(rv != ZF_OK) {
				fprintf(stderr, "error mounting built-in ROM dictionary\n");
				zf_free(ctx);
				free(ctx);
				return 1;
			}
		} else {
			zf_bootstrap(ctx);
		}
	}

	if(header_name) {
		zf_dict_set_data_compile(ctx, 1);
	}


	/* Include files from command line */

	for(i = 0; i < nrun; i++) {
		if(is_word[i]) {
			if(do_eval(ctx, "-e", 1, run[i]) != ZF_OK) had_error = 1;
		} else {
			zfl_include(ctx, run[i]);
		}
	}
	free(run);
	free(is_word);

	if(header_name) {
		zfl_export_header(ctx, header_name);
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

	if(cfg->poll != NULL) {
		repl_poll(ctx, cfg);
		zf_free(ctx);
		free(ctx);
		return had_error;
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
