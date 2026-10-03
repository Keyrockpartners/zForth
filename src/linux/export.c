/*
 * The ROM exporter: write the loaded dictionary as a C header (zforth -H)
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "host.h"


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

void zfl_export_header(zf_ctx *ctx, const char *name)
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
