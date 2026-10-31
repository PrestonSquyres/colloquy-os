/* Host-side check of the kernel's SHA-256 against FIPS 180-4 / NIST vectors. */
#include <stdio.h>
#include <string.h>
#include "sha256.h"
static int check(const char *msg, usize n, const char *hex) {
    u8 out[32]; char got[65];
    sha256(msg, n, out);
    for (int i = 0; i < 32; i++) sprintf(got + 2 * i, "%02x", out[i]);
    int ok = !strcmp(got, hex);
    printf("  %s sha256(%zu bytes)\n", ok ? "PASS" : "FAIL", (size_t)n);
    return ok;
}
int main(void) {
    static char million[1000000]; memset(million, 'a', sizeof million);
    int ok = 1;
    ok &= check("", 0, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    ok &= check("abc", 3, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    ok &= check("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56,
                "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    ok &= check(million, sizeof million, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    printf("sha256 vectors: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
