/*
 * Dexcom G7 EC-JPAKE, ported from xDrip+ libkeks (jamorham.keks.Calc) onto
 * TinyCrypt's P-256 primitives. Points are TinyCrypt native (little-endian
 * words, X then Y); everything on the wire is big-endian.
 */
#include "g7_jpake.h"

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>

#include <tinycrypt/aes.h>
#include <tinycrypt/constants.h>
#include <tinycrypt/sha256.h>

#include "g7_jpake_testvec.h"

LOG_MODULE_REGISTER(g7_jpake, LOG_LEVEL_INF);

#define W  NUM_ECC_WORDS
#define FB NUM_ECC_BYTES

/* Party ids bound into every proof. libkeks stores them nibble-swapped as
 * "36C69656E647" / "375627675627", which decode to these ASCII strings.
 */
static const uint8_t party_client[] = {'c', 'l', 'i', 'e', 'n', 't'};
static const uint8_t party_server[] = {'s', 'e', 'r', 'v', 'e', 'r'};

#define HAVE_ROUND(n) BIT((n) - 1)

static uECC_Curve curve(void)
{
	return uECC_secp256r1();
}

/* ---- scalars (mod n) ---------------------------------------------------- */

static void scalar_reduce(uECC_word_t *k)
{
	/* Any 256-bit value is < 2n, so one conditional subtraction suffices. */
	if (uECC_vli_cmp_unsafe(curve()->n, k, W) != 1) {
		uECC_vli_sub(k, k, curve()->n, W);
	}
}

static void scalar_from_be(uECC_word_t *k, const uint8_t be[FB])
{
	uECC_vli_bytesToNative(k, be, FB);
	scalar_reduce(k);
}

static int scalar_random(uECC_word_t *k)
{
	uint8_t buf[FB];

	for (int tries = 0; tries < 16; tries++) {
		int err = sys_csrand_get(buf, sizeof(buf));

		if (err) {
			return err;
		}
		uECC_vli_bytesToNative(k, buf, FB);
		if (!uECC_vli_isZero(k, W) && uECC_vli_cmp_unsafe(curve()->n, k, W) == 1) {
			return 0;
		}
	}
	return -EIO;
}

/* ---- points ------------------------------------------------------------- */

static void point_to_be(uint8_t out[2 * FB], const uECC_word_t *pt)
{
	uECC_vli_nativeToBytes(out, FB, pt);
	uECC_vli_nativeToBytes(out + FB, FB, pt + W);
}

static bool point_equal(const uECC_word_t *a, const uECC_word_t *b)
{
	return memcmp(a, b, 2 * W * sizeof(uECC_word_t)) == 0;
}

/* R = P * k, for 0 < k < n. R may alias P. */
static int point_mul(uECC_word_t *R, const uECC_word_t *P, const uECC_word_t *k)
{
	uECC_word_t k0[W], k1[W];
	uECC_word_t *regular[2] = {k0, k1};
	uECC_word_t res[2 * W];

	if (uECC_vli_isZero(k, W)) {
		return -EDOM;
	}

	/* Same fixed-length ladder setup as EccPoint_compute_public_key. */
	uECC_word_t carry = regularize_k(k, k0, k1, curve());

	EccPoint_mult(res, P, regular[!carry], NULL, curve()->num_n_bits + 1, curve());
	if (EccPoint_isZero(res, curve())) {
		return -EDOM;
	}
	memcpy(R, res, sizeof(res));
	return 0;
}

/* R = P + Q in affine coordinates. Fails for P == -Q (point at infinity). */
static int point_add(uECC_word_t *R, const uECC_word_t *P, const uECC_word_t *Q)
{
	const uECC_word_t *p = curve()->p;
	uECC_word_t num[W], den[W], lam[W], t[W], x3[W], y3[W];

	if (uECC_vli_cmp_unsafe(P, Q, W) == 0) {
		if (uECC_vli_cmp_unsafe(P + W, Q + W, W) != 0 || uECC_vli_isZero(P + W, W)) {
			return -EDOM;
		}
		/* Doubling: lambda = (3x^2 + a) / 2y, with a = -3. */
		uECC_word_t three[W] = {3};

		uECC_vli_modMult_fast(t, P, P, curve());
		uECC_vli_modAdd(num, t, t, p, W);
		uECC_vli_modAdd(num, num, t, p, W);
		uECC_vli_modSub(num, num, three, p, W);
		uECC_vli_modAdd(den, P + W, P + W, p, W);
	} else {
		uECC_vli_modSub(num, Q + W, P + W, p, W);
		uECC_vli_modSub(den, Q, P, p, W);
	}

	uECC_vli_modInv(t, den, p, W);
	uECC_vli_modMult_fast(lam, num, t, curve());

	uECC_vli_modMult_fast(x3, lam, lam, curve());
	uECC_vli_modSub(x3, x3, P, p, W);
	uECC_vli_modSub(x3, x3, Q, p, W);

	uECC_vli_modSub(t, P, x3, p, W);
	uECC_vli_modMult_fast(y3, lam, t, curve());
	uECC_vli_modSub(y3, y3, P + W, p, W);

	memcpy(R, x3, sizeof(x3));
	memcpy(R + W, y3, sizeof(y3));
	return 0;
}

static void point_negate(uECC_word_t *R, const uECC_word_t *P)
{
	if (R != P) {
		memcpy(R, P, W * sizeof(uECC_word_t));
	}
	uECC_vli_sub(R + W, curve()->p, P + W, W);
}

/* ---- zero-knowledge proofs ---------------------------------------------- */

static void sha_item(struct tc_sha256_state_struct *st, const uint8_t *data, size_t len)
{
	const uint8_t len_be[4] = {len >> 24, len >> 16, len >> 8, len};

	tc_sha256_update(st, len_be, sizeof(len_be));
	tc_sha256_update(st, data, len);
}

static void sha_point(struct tc_sha256_state_struct *st, const uECC_word_t *pt)
{
	uint8_t enc[1 + 2 * FB] = {0x04};

	point_to_be(enc + 1, pt);
	sha_item(st, enc, sizeof(enc));
}

/* h = SHA-256(g, V, X, party) mod n, as in Calc.getZeroKnowledgeHash. */
static void zkp_hash(uECC_word_t *h, const uECC_word_t *g, const uECC_word_t *V,
		     const uECC_word_t *X, const uint8_t *party, size_t party_len)
{
	struct tc_sha256_state_struct st;
	uint8_t digest[TC_SHA256_DIGEST_SIZE];

	tc_sha256_init(&st);
	sha_point(&st, g);
	sha_point(&st, V);
	sha_point(&st, X);
	sha_item(&st, party, party_len);
	tc_sha256_final(digest, &st);
	scalar_from_be(h, digest);
}

/* Proof of knowledge of x with X = g * x, nonce v, from us ("client"). */
static int zkp_make(uint8_t out[G7_JPAKE_PACKET_LEN], const uECC_word_t *g,
		    const uECC_word_t *x, const uECC_word_t *X, const uECC_word_t *v)
{
	uECC_word_t V[2 * W], h[W], hx[W], r[W];
	int err = point_mul(V, g, v);

	if (err) {
		return err;
	}
	zkp_hash(h, g, V, X, party_client, sizeof(party_client));
	uECC_vli_modMult(hx, h, x, curve()->n, W);
	uECC_vli_modSub(r, v, hx, curve()->n, W);

	point_to_be(out, X);
	point_to_be(out + 2 * FB, V);
	uECC_vli_nativeToBytes(out + 4 * FB, FB, r);
	return 0;
}

/* Checks the sensor's ("server") proof over generator g; returns X on success. */
static int zkp_check(uECC_word_t *X_out, const uECC_word_t *g,
		     const uint8_t pkt[G7_JPAKE_PACKET_LEN])
{
	uECC_word_t X[2 * W], V[2 * W], r[W], h[W], L[2 * W], T[2 * W];

	uECC_vli_bytesToNative(X, pkt, FB);
	uECC_vli_bytesToNative(X + W, pkt + FB, FB);
	uECC_vli_bytesToNative(V, pkt + 2 * FB, FB);
	uECC_vli_bytesToNative(V + W, pkt + 3 * FB, FB);
	uECC_vli_bytesToNative(r, pkt + 4 * FB, FB);

	if (uECC_valid_point(X, curve()) != 0 || uECC_valid_point(V, curve()) != 0 ||
	    uECC_vli_cmp_unsafe(curve()->n, r, W) != 1) {
		return -EBADMSG;
	}

	/* Valid iff g*r + X*h == V. */
	zkp_hash(h, g, V, X, party_server, sizeof(party_server));
	if (point_mul(L, g, r) || point_mul(T, X, h) || point_add(L, L, T) ||
	    !point_equal(L, V)) {
		return -EBADMSG;
	}

	memcpy(X_out, X, sizeof(X));
	return 0;
}

/* ---- public API ------------------------------------------------------------ */

static int password_scalar(uECC_word_t *s, const char *code)
{
	uint8_t be[FB] = {0};
	size_t len = strlen(code);

	if (len != 4 && len != 6) {
		return -EINVAL;
	}
	for (size_t i = 0; i < len; i++) {
		if (code[i] < '0' || code[i] > '9') {
			return -EINVAL;
		}
	}

	/* ASCII bytes as a big-endian integer; 6-digit codes get an "00" prefix. */
	memcpy(be + FB - len, code, len);
	if (len == 6) {
		be[FB - 8] = '0';
		be[FB - 7] = '0';
	}
	uECC_vli_bytesToNative(s, be, FB);
	return 0;
}

static int init_common(struct g7_jpake *ctx, const char *code)
{
	int err = password_scalar(ctx->s, code);

	if (!err) {
		err = point_mul(ctx->X1, curve()->G, ctx->x1);
	}
	if (!err) {
		err = point_mul(ctx->X2, curve()->G, ctx->x2);
	}
	ctx->have_peer = 0;
	return err;
}

int g7_jpake_init(struct g7_jpake *ctx, const char *code)
{
	memset(ctx, 0, sizeof(*ctx));

	int err = scalar_random(ctx->x1);

	if (!err) {
		err = scalar_random(ctx->x2);
	}
	return err ? err : init_common(ctx, code);
}

int g7_jpake_init_fixed(struct g7_jpake *ctx, const char *code,
			const uint8_t x1[G7_JPAKE_SCALAR_LEN],
			const uint8_t x2[G7_JPAKE_SCALAR_LEN])
{
	memset(ctx, 0, sizeof(*ctx));
	scalar_from_be(ctx->x1, x1);
	scalar_from_be(ctx->x2, x2);
	return init_common(ctx, code);
}

int g7_jpake_read_round(struct g7_jpake *ctx, int round,
			const uint8_t pkt[G7_JPAKE_PACKET_LEN])
{
	uECC_word_t g[2 * W];
	int err;

	switch (round) {
	case 1:
		err = zkp_check(ctx->X3, curve()->G, pkt);
		break;
	case 2:
		err = zkp_check(ctx->X4, curve()->G, pkt);
		break;
	case 3:
		if ((ctx->have_peer & (HAVE_ROUND(1) | HAVE_ROUND(2))) !=
		    (HAVE_ROUND(1) | HAVE_ROUND(2))) {
			return -EALREADY;
		}
		/* The sensor's generator is X1 + X2 + X3. */
		err = point_add(g, ctx->X1, ctx->X2);
		err = err ? err : point_add(g, g, ctx->X3);
		err = err ? err : zkp_check(ctx->B, g, pkt);
		break;
	default:
		return -EINVAL;
	}

	if (!err) {
		ctx->have_peer |= HAVE_ROUND(round);
	}
	return err;
}

int g7_jpake_write_round(struct g7_jpake *ctx, int round,
			 const uint8_t v_be[G7_JPAKE_SCALAR_LEN],
			 uint8_t out[G7_JPAKE_PACKET_LEN])
{
	uECC_word_t v[W], g[2 * W], xs[W], A[2 * W];
	int err;

	if (v_be) {
		scalar_from_be(v, v_be);
	} else {
		err = scalar_random(v);
		if (err) {
			return err;
		}
	}

	switch (round) {
	case 1:
		return zkp_make(out, curve()->G, ctx->x1, ctx->X1, v);
	case 2:
		return zkp_make(out, curve()->G, ctx->x2, ctx->X2, v);
	case 3:
		if ((ctx->have_peer & (HAVE_ROUND(1) | HAVE_ROUND(2))) !=
		    (HAVE_ROUND(1) | HAVE_ROUND(2))) {
			return -EALREADY;
		}
		/* A = (X1 + X3 + X4) * (x2 * s). */
		err = point_add(g, ctx->X1, ctx->X3);
		err = err ? err : point_add(g, g, ctx->X4);
		if (err) {
			return err;
		}
		uECC_vli_modMult(xs, ctx->x2, ctx->s, curve()->n, W);
		err = point_mul(A, g, xs);
		return err ? err : zkp_make(out, g, xs, A, v);
	default:
		return -EINVAL;
	}
}

int g7_jpake_derive_key(const struct g7_jpake *ctx, uint8_t key[G7_JPAKE_KEY_LEN])
{
	uECC_word_t xs[W], T[2 * W], K[2 * W];
	uint8_t kx[FB], digest[TC_SHA256_DIGEST_SIZE];
	struct tc_sha256_state_struct st;

	if (!(ctx->have_peer & HAVE_ROUND(3))) {
		return -EALREADY;
	}

	/* K = (B - X4 * (x2 * s)) * x2 */
	uECC_vli_modMult(xs, ctx->x2, ctx->s, curve()->n, W);
	int err = point_mul(T, ctx->X4, xs);

	if (!err) {
		point_negate(T, T);
		err = point_add(K, ctx->B, T);
	}
	if (!err) {
		err = point_mul(K, K, ctx->x2);
	}
	if (err) {
		return err;
	}

	uECC_vli_nativeToBytes(kx, FB, K);
	tc_sha256_init(&st);
	tc_sha256_update(&st, kx, sizeof(kx));
	tc_sha256_final(digest, &st);
	memcpy(key, digest, G7_JPAKE_KEY_LEN);
	return 0;
}

int g7_challenge_hash(const uint8_t key[G7_JPAKE_KEY_LEN],
		      const uint8_t chal[G7_CHALLENGE_LEN],
		      uint8_t out[G7_CHALLENGE_LEN])
{
	struct tc_aes_key_sched_struct sched;
	uint8_t block[TC_AES_BLOCK_SIZE], enc[TC_AES_BLOCK_SIZE];

	memcpy(block, chal, G7_CHALLENGE_LEN);
	memcpy(block + G7_CHALLENGE_LEN, chal, G7_CHALLENGE_LEN);
	if (tc_aes128_set_encrypt_key(&sched, key) != TC_CRYPTO_SUCCESS ||
	    tc_aes_encrypt(enc, block, &sched) != TC_CRYPTO_SUCCESS) {
		return -EIO;
	}
	memcpy(out, enc, G7_CHALLENGE_LEN);
	return 0;
}

/* ---- self-test ------------------------------------------------------------- */

static bool expect(const char *what, const uint8_t *got, const uint8_t *want, size_t len)
{
	if (memcmp(got, want, len) == 0) {
		LOG_INF("  %-12s OK", what);
		return true;
	}
	LOG_ERR("  %-12s MISMATCH", what);
	LOG_HEXDUMP_ERR(got, len, "got");
	LOG_HEXDUMP_ERR(want, len, "want");
	return false;
}

static bool expect_rc(const char *what, int rc, int want)
{
	if (rc == want) {
		LOG_INF("  %-12s OK", what);
		return true;
	}
	LOG_ERR("  %-12s returned %d, expected %d", what, rc, want);
	return false;
}

int g7_jpake_selftest(void)
{
	static struct g7_jpake ctx; /* ~450 bytes, keep it off the shell stack */
	static uint8_t pkt[G7_JPAKE_PACKET_LEN];
	uint8_t key[G7_JPAKE_KEY_LEN], hash[G7_CHALLENGE_LEN];
	bool ok = true;
	int64_t t0 = k_uptime_get();

	LOG_INF("EC-JPAKE self-test (known-good transcript, code %s)", TV_CODE);

	ok &= expect_rc("init", g7_jpake_init_fixed(&ctx, TV_CODE, tv_x1, tv_x2), 0);
	ok &= expect_rc("read s1", g7_jpake_read_round(&ctx, 1, tv_s1), 0);
	ok &= expect_rc("write a1", g7_jpake_write_round(&ctx, 1, tv_v1, pkt), 0);
	ok &= expect("a1", pkt, tv_a1, sizeof(pkt));
	ok &= expect_rc("read s2", g7_jpake_read_round(&ctx, 2, tv_s2), 0);
	ok &= expect_rc("write a2", g7_jpake_write_round(&ctx, 2, tv_v2, pkt), 0);
	ok &= expect("a2", pkt, tv_a2, sizeof(pkt));
	ok &= expect_rc("read s3", g7_jpake_read_round(&ctx, 3, tv_s3), 0);
	ok &= expect_rc("write a3", g7_jpake_write_round(&ctx, 3, tv_v3, pkt), 0);
	ok &= expect("a3", pkt, tv_a3, sizeof(pkt));
	ok &= expect_rc("derive key", g7_jpake_derive_key(&ctx, key), 0);
	ok &= expect("key", key, tv_key, sizeof(key));
	ok &= expect_rc("chal hash", g7_challenge_hash(key, tv_chal, hash), 0);
	ok &= expect("chal hash", hash, tv_chal_hash, sizeof(hash));

	int64_t t_ok = k_uptime_get() - t0;

	/* A corrupted sensor packet must be rejected. */
	memcpy(pkt, tv_s1, sizeof(pkt));
	pkt[G7_JPAKE_PACKET_LEN - 1] ^= 0x01;
	ok &= expect_rc("bad proof", g7_jpake_read_round(&ctx, 1, pkt), -EBADMSG);

	/* A wrong pairing code still "completes" but must give a different key. */
	g7_jpake_init_fixed(&ctx, "7346", tv_x1, tv_x2);
	g7_jpake_read_round(&ctx, 1, tv_s1);
	g7_jpake_read_round(&ctx, 2, tv_s2);
	g7_jpake_read_round(&ctx, 3, tv_s3);
	g7_jpake_derive_key(&ctx, key);
	if (memcmp(key, tv_key, sizeof(key)) == 0) {
		LOG_ERR("  wrong code   produced the right key");
		ok = false;
	} else {
		LOG_INF("  wrong code   OK (different key)");
	}

	/* Random-key init, as used for real connections. */
	t0 = k_uptime_get();
	ok &= expect_rc("random init", g7_jpake_init(&ctx, TV_CODE), 0);
	LOG_INF("  random init took %lld ms", k_uptime_get() - t0);

	memset(&ctx, 0, sizeof(ctx));
	LOG_INF("EC-JPAKE self-test %s (full transcript %lld ms)", ok ? "PASSED" : "FAILED",
		t_ok);
	return ok ? 0 : -EFAULT;
}
