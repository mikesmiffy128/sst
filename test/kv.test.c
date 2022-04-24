/* This file is dedicated to the public domain. */

{.desc = "the KeyValues parser"};

// undef conflicting macros
#undef ERROR // windows.h
#undef OUT // "
#undef EOF // stdio.h
#include "../src/kv.c"

#include "../src/intdefs.h"
#include "../src/noreturn.h"

static noreturn die(const struct kv_parser *kvp) {
	fprintf(stderr, "parse error: %d:%d: %s\n", kvp->line, kvp->col,
			kvp->errmsg);
	exit(1);
}

static void tokcb(enum kv_token type, const char *p, uint len,
		void *ctxt) {
	// nop - we're just testing the tokeniser
}

static const char data[] =
"KeyValues {\n\
	Key/1	Val1![conditional]\n\
	Key2\n\
Val2// comment\n\
	\"String Key\"  // also comment\n\
	Val3  Key4 [conditional!]{ Key5 \"Value Five\" } // one more\n\
} \n\
";
static const int sz = sizeof(data) - 1;

TEST("parsing should work with any buffer size") {
	for (int chunksz = 3; chunksz <= sz; ++chunksz) {
		struct kv_parser kvp = {0};
		// sending data in chunks to test reentrancy
		for (int chunk = 0; chunk * chunksz < sz; ++chunk) {
			int thischunk = chunksz;
			if (chunk * chunksz + thischunk > sz) {
				thischunk = sz - chunk * chunksz;
			}
			if (!kv_parser_feed(&kvp, data + chunk * chunksz, thischunk,
					tokcb, 0)) {
				die(&kvp);
			}
		}
		if (!kv_parser_done(&kvp)) die(&kvp);
	}
	return true;
}

// vi: sw=4 ts=4 noet tw=80 cc=80
