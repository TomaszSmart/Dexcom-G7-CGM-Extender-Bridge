/*
 * Dexcom G7 application-layer EC-JPAKE (secp256r1) keyed by the 4-digit
 * applicator pairing code, as implemented by xDrip+'s libkeks. See
 * ../PROTOCOL_RESEARCH.md section 2a for the wire sequence, and
 * ../tools/g7_jpake_ref.py for the reference implementation the test vectors
 * come from.
 *
 * We are always the "client" party, the sensor is the "server". Round packets
 * are 160 bytes: X || Y || Vx || Vy || r (big-endian, 32 bytes each), sent and
 * received in 20-byte chunks over the ExtraData characteristic.
 */
#ifndef G7_JPAKE_H_
#define G7_JPAKE_H_

#include <stdint.h>

#include <tinycrypt/ecc.h>

#define G7_JPAKE_PACKET_LEN 160
#define G7_JPAKE_SCALAR_LEN 32
#define G7_JPAKE_KEY_LEN    16
#define G7_CHALLENGE_LEN    8

struct g7_jpake {
	uECC_word_t s[NUM_ECC_WORDS];        /* pairing code as a scalar */
	uECC_word_t x1[NUM_ECC_WORDS];       /* our round 1 private key */
	uECC_word_t x2[NUM_ECC_WORDS];       /* our round 2 private key */
	uECC_word_t X1[2 * NUM_ECC_WORDS];   /* G * x1 */
	uECC_word_t X2[2 * NUM_ECC_WORDS];   /* G * x2 */
	uECC_word_t X3[2 * NUM_ECC_WORDS];   /* sensor's round 1 public key */
	uECC_word_t X4[2 * NUM_ECC_WORDS];   /* sensor's round 2 public key */
	uECC_word_t B[2 * NUM_ECC_WORDS];    /* sensor's round 3 value */
	uint8_t have_peer;                   /* bit n set: sensor's round n+1 accepted */
};

/*
 * Starts a new exchange for `code` (4 digits for G7, 6 for Dexcom ONE+), with
 * fresh random private keys. Costs two scalar multiplications, so it is worth
 * calling before connecting. Returns 0 or a negative errno.
 */
int g7_jpake_init(struct g7_jpake *ctx, const char *code);

/* Same as g7_jpake_init, but with fixed big-endian private keys (self-test). */
int g7_jpake_init_fixed(struct g7_jpake *ctx, const char *code,
			const uint8_t x1[G7_JPAKE_SCALAR_LEN],
			const uint8_t x2[G7_JPAKE_SCALAR_LEN]);

/*
 * Validates the sensor's packet for `round` (1..3) and stores its public
 * value. Round 3 requires rounds 1 and 2 first. Returns 0, -EBADMSG if the
 * zero-knowledge proof does not verify, or another negative errno.
 */
int g7_jpake_read_round(struct g7_jpake *ctx, int round,
			const uint8_t pkt[G7_JPAKE_PACKET_LEN]);

/*
 * Produces our packet for `round` (1..3). Round 3 requires the sensor's
 * rounds 1 and 2. `v` is the proof nonce (big-endian); pass NULL for a random
 * one, which is the only sane choice outside the self-test.
 */
int g7_jpake_write_round(struct g7_jpake *ctx, int round,
			 const uint8_t v[G7_JPAKE_SCALAR_LEN],
			 uint8_t out[G7_JPAKE_PACKET_LEN]);

/* Derives the AES-128 session key once the sensor's round 3 was accepted. */
int g7_jpake_derive_key(const struct g7_jpake *ctx, uint8_t key[G7_JPAKE_KEY_LEN]);

/*
 * First 8 bytes of AES-128-ECB(key, chal || chal). Used both ways: to check the
 * sensor's answer to our AuthRequest token, and to answer its challenge.
 */
int g7_challenge_hash(const uint8_t key[G7_JPAKE_KEY_LEN],
		      const uint8_t chal[G7_CHALLENGE_LEN],
		      uint8_t out[G7_CHALLENGE_LEN]);

/*
 * Runs the known-good transcript from g7_jpake_testvec.h. Returns 0 if every
 * byte matches; logs the first mismatch otherwise.
 */
int g7_jpake_selftest(void);

#endif /* G7_JPAKE_H_ */
