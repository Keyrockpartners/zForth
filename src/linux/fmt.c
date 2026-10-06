/*
 * The fmt and fmt-buf syscalls: printf-style formatting of stack values
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <inttypes.h>

#include "host.h"


#define ZF_FMT_MAX_ARG_CELLS 16

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
	char width[12] = "", prec[12] = "";
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
				const uint8_t *str = zfl_dict_range(ctx, addr, len);
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
void zfl_fmt(zf_ctx *ctx)
{
	zf_cell fmt_len = zf_pop(ctx);
	zf_cell fmt_addr = zf_pop(ctx);
	const uint8_t *fmt = zfl_dict_range(ctx, fmt_addr, fmt_len);
	fmt_sink out = { NULL, 0, 0 };
	fmt_format(ctx, fmt, (size_t)fmt_len, &out);
	fflush(stdout);
}

/* fmt-buf ( buf-addr buf-len args... fmt-addr fmt-len -- n ): format into a
 * dictionary buffer, truncating to buf-len bytes; n is the number of bytes
 * written. The output is built in C memory and copied in one write, since a
 * dictionary write may move the dictionary. */
void zfl_fmt_buf(zf_ctx *ctx)
{
	zf_cell fmt_len = zf_pop(ctx);
	zf_cell fmt_addr = zf_pop(ctx);
	const uint8_t *fmt = zfl_dict_range(ctx, fmt_addr, fmt_len);
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
		(void)zfl_dict_range(ctx, buf_addr, (zf_cell)n);
		zf_dict_write_bytes(ctx, (zf_addr)buf_addr, out.buf, n);
	}
	zf_push(ctx, (zf_cell)n);
}
