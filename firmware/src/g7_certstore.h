/*
 * The app certificates and signing key a 4-digit G7 asks for on its first
 * pairing (xDrip+'s keks_p1..p3 from the setup QR code).
 *
 * They are loaded at run time -- written by the setup page over Bluetooth and
 * kept in flash -- so a published firmware image carries no key. A private
 * build may still compile them in (src/g7_certs.h from tools/make_g7_certs.py);
 * stored ones take precedence.
 */
#ifndef G7_CERTSTORE_H_
#define G7_CERTSTORE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define G7_CERT_MAX_LEN     700 /* certificate A / B (seen: 494 / 465 bytes) */
#define G7_CERT_KEY_MAX_LEN 200 /* keks_p3: PKCS#8 DER, or the raw 32-byte scalar */

enum g7_cert_part {
	G7_CERT_PART_A = 1,   /* keks_p1 */
	G7_CERT_PART_B = 2,   /* keks_p2, the leaf certificate of the key */
	G7_CERT_PART_KEY = 3, /* keks_p3 */
};

enum g7_cert_source {
	G7_CERTS_NONE,
	G7_CERTS_STORED,  /* loaded over Bluetooth, in flash */
	G7_CERTS_BUILTIN, /* compiled in from g7_certs.h */
};

enum g7_cert_source g7_certs_source(void);

/* Valid only while g7_certs_source() != G7_CERTS_NONE. */
const uint8_t *g7_certs_a(size_t *len);
const uint8_t *g7_certs_b(size_t *len);
const uint8_t *g7_certs_key(void); /* 32-byte P-256 scalar */

/* Upload: write the parts in chunks, then commit (validates and saves). */
int g7_certs_stage(enum g7_cert_part part, size_t offset, const uint8_t *data, size_t len);
/* -EINVAL: incomplete or malformed; -EBADMSG: key does not match certificate B. */
int g7_certs_commit(void);
int g7_certs_clear(void);

/* Key matches certificate B and a test signature verifies. */
int g7_certs_selftest(void);

/* SHA256withECDSA of `chal` (16 bytes) with the key, r || s. */
int g7_certs_sign(const uint8_t chal[16], uint8_t sig[64]);

#endif /* G7_CERTSTORE_H_ */
