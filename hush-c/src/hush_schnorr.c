/* hush_schnorr.c: owns BIP-340 schnorr verification over OpenSSL BIGNUM/EC. */

#include <assert.h>
#include <string.h>

#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/obj_mac.h>

#include "hush_schnorr.h"

enum {
    HUSH_SCHNORR_HASH_BYTES = 32,
    HUSH_SCHNORR_WORD_BYTES = 32,
    HUSH_SCHNORR_TAG_BYTES = 17 /* "BIP0340/challenge" */
};

#define HUSH_SCHNORR_TAG "BIP0340/challenge"

/* Every OpenSSL object one verification needs. */
typedef struct {
    EC_GROUP *group;
    EC_POINT *public_point;
    EC_POINT *left;
    EC_POINT *right;
    BIGNUM *field;
    BIGNUM *order;
    BIGNUM *x;
    BIGNUM *s;
    BIGNUM *e;
    BIGNUM *r;
    BIGNUM *rx;
    BIGNUM *ry;
    BN_CTX *ctx;
    unsigned char r_bytes[HUSH_SCHNORR_WORD_BYTES];
} hush_schnorr_ctx_t;

/* Allocates every member. Fails HUSH_ERR_CRYPTO. */
static hush_status_t hush_schnorr_ctx_create(hush_schnorr_ctx_t *out);
/* Releases every member. NULL-safe. */
static void hush_schnorr_ctx_destroy(hush_schnorr_ctx_t *ctx);
/* One-shot SHA-256 over a bounded buffer. */
static hush_status_t hush_schnorr_sha256(
    unsigned char out[HUSH_SCHNORR_HASH_BYTES], const unsigned char *data,
    size_t len);
/* True when value is below the group field prime. */
static int hush_schnorr_in_field(const hush_schnorr_ctx_t *ctx,
                                 const BIGNUM *value);
/* lift_x: sets public_point from the x-only key. 0 when the key is invalid. */
static int hush_schnorr_lift(const hush_schnorr_ctx_t *ctx,
                             const unsigned char *pubkey);
/* Challenge scalar e = tagged_hash("BIP0340/challenge", R.x || P || m) mod n. */
static hush_status_t hush_schnorr_challenge(
    hush_schnorr_ctx_t *ctx, const unsigned char *pubkey,
    const unsigned char *message, size_t message_len);
/* Computes the challenge and the point R = s*G - e*P. */
static hush_status_t hush_schnorr_equation(
    hush_schnorr_ctx_t *ctx, const hush_schnorr_request_t *request);
/* True when R is a non-infinity point with even y and x == r. */
static int hush_schnorr_result_matches(const hush_schnorr_ctx_t *ctx);

hush_status_t hush_schnorr_verify(const hush_schnorr_request_t *request)
{
    hush_schnorr_ctx_t ctx;
    hush_status_t status;

    if (request == NULL || request->pubkey == NULL || request->signature == NULL)
        return HUSH_ERR_ARG;
    if (request->message_len > 0 && request->message == NULL)
        return HUSH_ERR_ARG;
    status = hush_schnorr_ctx_create(&ctx);
    if (status != HUSH_OK)
        return status;
    if (!hush_schnorr_lift(&ctx, request->pubkey)) {
        hush_schnorr_ctx_destroy(&ctx);
        return HUSH_ERR_DENIED;
    }
    status = hush_schnorr_equation(&ctx, request);
    if (status == HUSH_OK && !hush_schnorr_result_matches(&ctx))
        status = HUSH_ERR_DENIED;
    hush_schnorr_ctx_destroy(&ctx);
    return status;
}

static hush_status_t hush_schnorr_ctx_create(hush_schnorr_ctx_t *out)
{
    assert(out != NULL);
    memset(out, 0, sizeof(*out));
    out->group = EC_GROUP_new_by_curve_name(NID_secp256k1);
    out->public_point = EC_POINT_new(out->group);
    out->left = EC_POINT_new(out->group);
    out->right = EC_POINT_new(out->group);
    out->field = BN_new();
    out->order = BN_new();
    out->x = BN_new();
    out->s = BN_new();
    out->e = BN_new();
    out->r = BN_new();
    out->rx = BN_new();
    out->ry = BN_new();
    out->ctx = BN_CTX_new();
    if (out->group == NULL || out->public_point == NULL || out->left == NULL ||
        out->right == NULL || out->field == NULL || out->order == NULL ||
        out->x == NULL || out->s == NULL || out->e == NULL || out->r == NULL ||
        out->rx == NULL || out->ry == NULL || out->ctx == NULL) {
        hush_schnorr_ctx_destroy(out);
        return HUSH_ERR_CRYPTO;
    }
    if (EC_GROUP_get_curve(out->group, out->field, NULL, NULL, out->ctx) != 1 ||
        EC_GROUP_get_order(out->group, out->order, out->ctx) != 1) {
        hush_schnorr_ctx_destroy(out);
        return HUSH_ERR_CRYPTO;
    }
    return HUSH_OK;
}

static void hush_schnorr_ctx_destroy(hush_schnorr_ctx_t *ctx)
{
    assert(ctx != NULL);
    EC_POINT_free(ctx->public_point);
    EC_POINT_free(ctx->left);
    EC_POINT_free(ctx->right);
    EC_GROUP_free(ctx->group);
    BN_free(ctx->field);
    BN_free(ctx->order);
    BN_free(ctx->x);
    BN_free(ctx->s);
    BN_free(ctx->e);
    BN_free(ctx->r);
    BN_free(ctx->rx);
    BN_free(ctx->ry);
    BN_CTX_free(ctx->ctx);
    memset(ctx, 0, sizeof(*ctx));
}

static hush_status_t hush_schnorr_sha256(
    unsigned char out[HUSH_SCHNORR_HASH_BYTES], const unsigned char *data,
    size_t len)
{
    EVP_MD_CTX *hash;
    unsigned int written = 0;
    int ok;

    assert(out != NULL);
    assert(data != NULL || len == 0);
    hash = EVP_MD_CTX_new();
    if (hash == NULL)
        return HUSH_ERR_CRYPTO;
    ok = EVP_DigestInit_ex(hash, EVP_sha256(), NULL) == 1 &&
         EVP_DigestUpdate(hash, data, len) == 1 &&
         EVP_DigestFinal_ex(hash, out, &written) == 1;
    EVP_MD_CTX_free(hash);
    if (!ok || written != (unsigned int)HUSH_SCHNORR_HASH_BYTES)
        return HUSH_ERR_CRYPTO;
    return HUSH_OK;
}

static int hush_schnorr_in_field(const hush_schnorr_ctx_t *ctx,
                                 const BIGNUM *value)
{
    assert(ctx != NULL);
    assert(value != NULL);
    return BN_cmp(value, ctx->field) < 0;
}

static int hush_schnorr_lift(const hush_schnorr_ctx_t *ctx,
                             const unsigned char *pubkey)
{
    assert(ctx != NULL);
    assert(pubkey != NULL);
    if (BN_bin2bn(pubkey, HUSH_SCHNORR_PUBKEY_BYTES, ctx->x) == NULL)
        return 0;
    if (!hush_schnorr_in_field(ctx, ctx->x))
        return 0;
    /* y_bit 0 selects the even-y root, which is exactly lift_x. */
    return EC_POINT_set_compressed_coordinates(ctx->group, ctx->public_point,
                                               ctx->x, 0, ctx->ctx) == 1;
}

static hush_status_t hush_schnorr_challenge(
    hush_schnorr_ctx_t *ctx, const unsigned char *pubkey,
    const unsigned char *message, size_t message_len)
{
    unsigned char tag_hash[HUSH_SCHNORR_HASH_BYTES];
    unsigned char hash[HUSH_SCHNORR_HASH_BYTES];
    EVP_MD_CTX *digest;
    unsigned int written = 0;
    int ok;

    assert(ctx != NULL);
    HUSH_TRY(hush_schnorr_sha256(tag_hash,
                                 (const unsigned char *)HUSH_SCHNORR_TAG,
                                 (size_t)HUSH_SCHNORR_TAG_BYTES));
    digest = EVP_MD_CTX_new();
    if (digest == NULL)
        return HUSH_ERR_CRYPTO;
    ok = EVP_DigestInit_ex(digest, EVP_sha256(), NULL) == 1 &&
         EVP_DigestUpdate(digest, tag_hash, sizeof(tag_hash)) == 1 &&
         EVP_DigestUpdate(digest, tag_hash, sizeof(tag_hash)) == 1 &&
         EVP_DigestUpdate(digest, ctx->r_bytes, sizeof(ctx->r_bytes)) == 1 &&
         EVP_DigestUpdate(digest, pubkey, HUSH_SCHNORR_PUBKEY_BYTES) == 1 &&
         EVP_DigestUpdate(digest, message, message_len) == 1 &&
         EVP_DigestFinal_ex(digest, hash, &written) == 1;
    EVP_MD_CTX_free(digest);
    if (!ok || written != (unsigned int)HUSH_SCHNORR_HASH_BYTES)
        return HUSH_ERR_CRYPTO;
    if (BN_bin2bn(hash, sizeof(hash), ctx->e) == NULL)
        return HUSH_ERR_CRYPTO;
    if (BN_mod(ctx->e, ctx->e, ctx->order, ctx->ctx) != 1)
        return HUSH_ERR_CRYPTO;
    return HUSH_OK;
}

static hush_status_t hush_schnorr_equation(
    hush_schnorr_ctx_t *ctx, const hush_schnorr_request_t *request)
{
    assert(ctx != NULL);
    assert(request != NULL);
    memcpy(ctx->r_bytes, request->signature, sizeof(ctx->r_bytes));
    if (BN_bin2bn(ctx->r_bytes, sizeof(ctx->r_bytes), ctx->r) == NULL ||
        BN_bin2bn(request->signature + HUSH_SCHNORR_WORD_BYTES,
                  HUSH_SCHNORR_WORD_BYTES, ctx->s) == NULL)
        return HUSH_ERR_CRYPTO;
    if (!hush_schnorr_in_field(ctx, ctx->r) ||
        BN_cmp(ctx->s, ctx->order) >= 0)
        return HUSH_ERR_DENIED;
    HUSH_TRY(hush_schnorr_challenge(ctx, request->pubkey, request->message,
                                    request->message_len));
    if (EC_POINT_mul(ctx->group, ctx->left, ctx->s, NULL, NULL, ctx->ctx) != 1 ||
        EC_POINT_mul(ctx->group, ctx->right, NULL, ctx->public_point, ctx->e,
                     ctx->ctx) != 1)
        return HUSH_ERR_CRYPTO;
    if (EC_POINT_invert(ctx->group, ctx->right, ctx->ctx) != 1 ||
        EC_POINT_add(ctx->group, ctx->left, ctx->left, ctx->right,
                     ctx->ctx) != 1)
        return HUSH_ERR_CRYPTO;
    return HUSH_OK;
}

static int hush_schnorr_result_matches(const hush_schnorr_ctx_t *ctx)
{
    assert(ctx != NULL);
    if (EC_POINT_is_at_infinity(ctx->group, ctx->left) == 1)
        return 0;
    if (EC_POINT_get_affine_coordinates(ctx->group, ctx->left, ctx->rx,
                                        ctx->ry, ctx->ctx) != 1)
        return 0;
    if (BN_is_odd(ctx->ry))
        return 0;
    return BN_cmp(ctx->rx, ctx->r) == 0;
}
