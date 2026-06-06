#ifndef ZFCONF_COMMON_H
#define ZFCONF_COMMON_H

/* Set to 1 to add tracing support for debugging and inspection. Requires the
 * zf_host_trace() function to be implemented. Adds about one kB to .text and
 * .rodata, dramatically reduces speed, but is very useful. Make sure to enable
 * tracing at run time when calling zf_init() or by setting the 'trace' user
 * variable to 1 */
#ifndef ZF_ENABLE_TRACE
#define ZF_ENABLE_TRACE 0
#endif

#ifndef ZF_ENABLE_DYNAMIC_DICT
#define ZF_ENABLE_DYNAMIC_DICT 1
#endif

/* Set to 1 to allow mounting a read-only base dictionary and appending new
 * definitions to a writable RAM tail. This is useful when a generated
 * zforth_dict.h can live in memory-mapped flash/ROM. */
#ifndef ZF_ENABLE_ROM_DICT
#define ZF_ENABLE_ROM_DICT 0
#endif

/* Set to 1 to enable opaque host/OS object cells. Disabled by default so
 * numeric-only builds do not require host object hooks or extra storage. */
#ifndef ZFORTH_EXT_OS_OBJECTS
#define ZFORTH_EXT_OS_OBJECTS 0
#endif

#ifndef ZF_EXT_DICT_OWNERS_MAX
#define ZF_EXT_DICT_OWNERS_MAX 32
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


/* Type to use for the basic cell, data stack and return stack. Choose a signed
 * integer type that suits your needs, or 'float' or 'double' if you need
 * floating point numbers */
#ifndef ZF_CELL_TYPE
#define ZF_CELL_TYPE double
#endif
typedef ZF_CELL_TYPE zf_cell;

#ifndef ZF_CELL_FMT
#define ZF_CELL_FMT "%.17g"
#endif

#ifndef ZF_SCAN_FMT
#define ZF_SCAN_FMT "%lf"
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
 * number of elements of type zf_cell */
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
