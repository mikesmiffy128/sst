// *SUPER* low effort x25519 keygen tool, for lack of better tool on hand.
// To compile:
// Unix: $CC -O2 -Dbool=_Bool -o.build/genkeypair tools/genkeypair.c
// Windows: clang-cl -fuse-ld=lld -O2 -Dbool=_Bool -Fe.build/genkeypair.exe tools/genkeypair.c /link advapi32.lib

#include <stdio.h>
#include <stdlib.h>

#include "../src/3p/monocypher/monocypher.c" // yeah, we're doing this.
#include "../src/os.h"

int main(void) {
	unsigned char prv[32], pub[32];
	os_randombytes(prv, sizeof(prv));
	crypto_x25519_public_key(pub, prv);
	// and now the worst string formatting code you've ever seen!!!
	fputs("Private key: ", stdout);
	for (int i = 0; i < sizeof(prv); i += 4) {
		fprintf(stdout, "%.2X%.2X%.2X%.2X",
				prv[i], prv[i + 1], prv[i + 2], prv[i + 3]);
	}
	fputs("\n{", stdout);
	for (int i = 0; i < sizeof(prv) - 4; i += 4) {
		fprintf(stdout, "0x%.2X, 0x%.2X, 0x%.2X, 0x%.2X, ",
				prv[i], prv[i + 1], prv[i + 2], prv[i + 3]);
	}
	fprintf(stdout, "0x%.2X, 0x%.2X, 0x%.2X, 0x%.2X}\n\nPublic key:  ",
			prv[sizeof(prv) - 4], prv[sizeof(prv) - 3],
			prv[sizeof(prv) - 2], prv[sizeof(prv) - 1]);
	for (int i = 0; i < sizeof(pub); i += 4) {
		fprintf(stdout, "%.2X%.2X%.2X%.2X",
				pub[i], pub[i + 1], pub[i + 2], pub[i + 3]);
	}
	fputs("\n{", stdout);
	for (int i = 0; i < sizeof(pub) - 4; i += 4) {
		fprintf(stdout, "0x%.2X, 0x%.2X, 0x%.2X, 0x%.2X, ",
				pub[i], pub[i + 1], pub[i + 2], pub[i + 3]);
	}
	fprintf(stdout, "0x%.2X, 0x%.2X, 0x%.2X, 0x%.2X}\n",
			pub[sizeof(pub) - 4], pub[sizeof(pub) - 3],
			pub[sizeof(pub) - 2], pub[sizeof(pub) - 1]);
}
