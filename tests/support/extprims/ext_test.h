/* A sample extension-primitive header for the build-config tests:
 * sqr ( n -- n*n ), and an abort reason. */
#define ZF_EXT_PRIM_ENUM PRIM_EXT_SQR, PRIM_EXT_FAIL,
#define ZF_EXT_PRIM_NAMES _("sqr") _("ext-fail")
#define ZF_EXT_PRIM_CASES \
	case PRIM_EXT_SQR: d1 = zf_pop(ctx); zf_push(ctx, d1 * d1); break; \
	case PRIM_EXT_FAIL: zf_abort(ctx, ZF_ABORT_EXT_TEST); break;
#define ZF_EXT_RESULTS ZF_ABORT_EXT_TEST
#define ZF_EXT_RESULT_MESSAGES case ZF_ABORT_EXT_TEST: msg = "extension test"; break;
#define ZF_EXT_PRIMS_ID 1
