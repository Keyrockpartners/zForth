#ifndef ZFCONF_COMMON_H
#define ZFCONF_COMMON_H

#include <inttypes.h>

/* Set to 1 to add tracing support for debugging and inspection. Requires the
 * zf_host_trace() function to be implemented. Adds about one kB to .text and
 * .rodata, dramatically reduces speed, but is very useful. Make sure to enable
 * tracing at run time when calling zf_init() or by setting the 'trace' user
 * variable to 1 */
#ifndef ZF_ENABLE_TRACE
#define ZF_ENABLE_TRACE 0
#endif

/* Set to 1 to allocate the writable dictionary from the heap and grow it on
 * demand, 0 for a fixed ZF_DICT_SIZE array inside zf_ctx. */
#ifndef ZF_ENABLE_DYNAMIC_DICT
#define ZF_ENABLE_DYNAMIC_DICT 1
#endif

/* Dynamic dictionary sizing, in bytes. These count only the writable RAM part
 * of the dictionary: with a mounted ROM image that is the data window plus
 * anything defined at runtime, not the image itself (an image loaded with
 * zf_dict_import_with_data() on a build without ROM support is in RAM, and
 * counts). Growth past ZF_DICT_MAX_SIZE aborts the running code with
 * ZF_ABORT_OUTSIDE_MEM instead of exhausting the heap; 0 means no limit. Set
 * ZF_DICT_INITIAL_SIZE equal to ZF_DICT_MAX_SIZE to allocate once at startup,
 * before the heap fragments, and never realloc. zf_dict_set_limit() changes
 * the maximum at runtime. */
#ifndef ZF_DICT_INITIAL_SIZE
#define ZF_DICT_INITIAL_SIZE 4096
#endif

#ifndef ZF_DICT_GROW_SIZE
#define ZF_DICT_GROW_SIZE 4096
#endif

#ifndef ZF_DICT_MAX_SIZE
#define ZF_DICT_MAX_SIZE 32768
#endif

/* The dynamic dictionary uses ZF_REALLOC(p, n) and ZF_FREE(p), defaulting to
 * realloc() and free(). ZF_REALLOC(NULL, n) must behave like malloc(n). To
 * pick the heap on ESP32, e.g. keep the dictionary in internal RAM:
 *
 *   #include <esp_heap_caps.h>
 *   #define ZF_REALLOC(p, n) heap_caps_realloc((p), (n), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
 *   #define ZF_FREE(p) heap_caps_free(p)
 *
 * or MALLOC_CAP_SPIRAM to put it in PSRAM. */

/* Set to 1 to allow mounting a read-only base dictionary and appending new
 * definitions to a writable RAM tail. This is useful when a generated
 * zforth_dict.h can live in memory-mapped flash/ROM. */
#ifndef ZF_ENABLE_ROM_DICT
#define ZF_ENABLE_ROM_DICT 0
#endif

/* Base address of the data window. Variables defined while exporting a
 * prebuilt dictionary (zforth -H) get their storage here instead of inline in
 * the image, so they stay writable when the image is mounted from ROM. The
 * window is backed by RAM at runtime; the dictionary itself may never grow up
 * to this address. */
#ifndef ZF_DATA_ADDR
#define ZF_DATA_ADDR 0x40000000u
#endif


/* Set to 1 to add boundary checks to stack operations. Increases .text size
 * by approx 100 bytes */
#ifndef ZF_ENABLE_BOUNDARY_CHECKS
#define ZF_ENABLE_BOUNDARY_CHECKS 1
#endif


/* Set to 1 to enable bootstrapping of the forth dictionary by adding the
 * primitives and user variables. On small embedded systems you may choose to
 * leave this out and start by loading a cross-compiled dictionary instead.
 * Enabling adds a few hundred bytes to the .text and .rodata segments */
#ifndef ZF_ENABLE_BOOTSTRAP
#define ZF_ENABLE_BOOTSTRAP 0
#endif


/* Set to 1 to enable typed access to memory. This allows memory read and write
 * of signed and unsigned memory of 8, 16 and 32 bits width, as well as the zf_cell
 * type. This adds a few hundred bytes of .text. Check the memaccess.zf file for
 * examples how to use these operations */
#ifndef ZF_ENABLE_TYPED_MEM_ACCESS
#define ZF_ENABLE_TYPED_MEM_ACCESS 1
#endif


/* Type to use for the basic cell, data stack and return stack. Must be a
 * signed integer type, with zf_ucell the unsigned type of the same width.
 * Arithmetic wraps around on overflow. */
#ifndef ZF_CELL_TYPE
#define ZF_CELL_TYPE int32_t
#endif
typedef ZF_CELL_TYPE zf_cell;

#ifndef ZF_UCELL_TYPE
#define ZF_UCELL_TYPE uint32_t
#endif
typedef ZF_UCELL_TYPE zf_ucell;

#ifndef ZF_CELL_FMT
#define ZF_CELL_FMT "%" PRId32
#endif

/* Set to 1 to add 64-bit double-cell numbers, signed and unsigned: two stack
 * entries, the low cell below the high cell (d+ d- d< du< m* um* ud* d/ dmod
 * ud/ udmod). A number with a final '.', e.g. 123. -5. or 0xFF., is a
 * double-cell literal. Requires 32-bit cells. */
#ifndef ZF_ENABLE_DOUBLE_CELL
#define ZF_ENABLE_DOUBLE_CELL 1
#endif

/* Set to 1 to add single-precision floating point. A float is stored in one
 * cell as its IEEE-754 bit pattern, on the same stack as integers, and the
 * f+ f- f* f/ ... primitives interpret those bits. Requires 32-bit cells. */
#ifndef ZF_ENABLE_FLOAT
#define ZF_ENABLE_FLOAT 1
#endif

/* Set to 1 to add double-precision floating point: an IEEE-754 double in two
 * cells, ( lo hi ), with df+ df- df* df/ ... and literals written with a d
 * exponent (1.5d0). Done in software on chips without a double FPU. Requires
 * 32-bit cells. */
#ifndef ZF_ENABLE_DFLOAT
#define ZF_ENABLE_DFLOAT 1
#endif

/* zf_int use for bitops, some arch int type width is less than register width,
   it will cause sign fill, so we need manual specify it */
#ifndef ZF_INT_TYPE
#define ZF_INT_TYPE int
#endif
typedef ZF_INT_TYPE zf_int;

/* The type to use for pointers and addresses. 'unsigned int' is usually a good
 * choice for best performance and smallest code size */
#ifndef ZF_ADDR_TYPE
#define ZF_ADDR_TYPE unsigned int
#endif
typedef ZF_ADDR_TYPE zf_addr;

#ifndef ZF_ADDR_FMT
#define ZF_ADDR_FMT "%04x"
#endif


/* Memory region sizes: dictionary size is given in bytes, stack sizes are
 * number of elements of type zf_cell. ZF_DICT_SIZE is the fixed dictionary
 * size when ZF_ENABLE_DYNAMIC_DICT is 0. */
#ifndef ZF_DICT_SIZE
#define ZF_DICT_SIZE 4096
#endif

#ifndef ZF_DSTACK_SIZE
#define ZF_DSTACK_SIZE 128
#endif

#ifndef ZF_RSTACK_SIZE
#define ZF_RSTACK_SIZE 128
#endif

#endif
