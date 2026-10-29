#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <proto/exec.h>
#include <proto/dos.h>

#include "app.h"
#include "passwords.h"

/* =========================================================================
 * Cryptographic Primitives: SHA-256, HMAC-SHA256, PBKDF2, ChaCha20
 * Fully self-contained, portable, constant-time & big/little-endian safe.
 * ========================================================================= */

#define ROTR32(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define ROTL32(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

static inline uint32 load32_be(const uint8 *p)
{
    return ((uint32)p[0] << 24) |
           ((uint32)p[1] << 16) |
           ((uint32)p[2] << 8)  |
           ((uint32)p[3]);
}

static inline void store32_be(uint8 *p, uint32 v)
{
    p[0] = (uint8)((v >> 24) & 0xFF);
    p[1] = (uint8)((v >> 16) & 0xFF);
    p[2] = (uint8)((v >> 8)  & 0xFF);
    p[3] = (uint8)(v & 0xFF);
}

static inline uint32 load32_le(const uint8 *p)
{
    return ((uint32)p[0])       |
           ((uint32)p[1] << 8)  |
           ((uint32)p[2] << 16) |
           ((uint32)p[3] << 24);
}

static inline void store32_le(uint8 *p, uint32 v)
{
    p[0] = (uint8)(v & 0xFF);
    p[1] = (uint8)((v >> 8)  & 0xFF);
    p[2] = (uint8)((v >> 16) & 0xFF);
    p[3] = (uint8)((v >> 24) & 0xFF);
}

/* SHA-256 context */
typedef struct {
    uint32 state[8];
    uint64 count;
    uint8  buffer[64];
} SHA256_CTX;

static const uint32 K256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static void sha256_transform(uint32 state[8], const uint8 block[64])
{
    uint32 a = state[0], b = state[1], c = state[2], d = state[3];
    uint32 e = state[4], f = state[5], g = state[6], h = state[7];
    uint32 W[64];

    for (int t = 0; t < 16; t++) {
        W[t] = load32_be(&block[t * 4]);
    }
    for (int t = 16; t < 64; t++) {
        uint32 s0 = ROTR32(W[t - 15], 7) ^ ROTR32(W[t - 15], 18) ^ (W[t - 15] >> 3);
        uint32 s1 = ROTR32(W[t - 2], 17) ^ ROTR32(W[t - 2], 19)  ^ (W[t - 2] >> 10);
        W[t] = W[t - 16] + s0 + W[t - 7] + s1;
    }

    for (int t = 0; t < 64; t++) {
        uint32 S1 = ROTR32(e, 6) ^ ROTR32(e, 11) ^ ROTR32(e, 25);
        uint32 ch = (e & f) ^ ((~e) & g);
        uint32 temp1 = h + S1 + ch + K256[t] + W[t];
        uint32 S0 = ROTR32(a, 2) ^ ROTR32(a, 13) ^ ROTR32(a, 22);
        uint32 maj = (a & b) ^ (a & c) ^ (b & c);
        uint32 temp2 = S0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

static void sha256_init(SHA256_CTX *ctx)
{
    ctx->state[0] = 0x6a09e667;
    ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372;
    ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f;
    ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab;
    ctx->state[7] = 0x5be0cd19;
    ctx->count = 0;
}

static void sha256_update(SHA256_CTX *ctx, const uint8 *data, size_t len)
{
    size_t i = 0;
    size_t buffer_idx = (size_t)(ctx->count & 63);
    ctx->count += len;

    if (buffer_idx > 0) {
        size_t to_copy = 64 - buffer_idx;
        if (len < to_copy) {
            memcpy(&ctx->buffer[buffer_idx], data, len);
            return;
        }
        memcpy(&ctx->buffer[buffer_idx], data, to_copy);
        sha256_transform(ctx->state, ctx->buffer);
        i += to_copy;
    }

    for (; i + 64 <= len; i += 64) {
        sha256_transform(ctx->state, &data[i]);
    }

    if (i < len) {
        memcpy(ctx->buffer, &data[i], len - i);
    }
}

static void sha256_final(SHA256_CTX *ctx, uint8 digest[32])
{
    uint8 pad[64] = { 0x80 };
    uint64 bits = ctx->count * 8;
    size_t buffer_idx = (size_t)(ctx->count & 63);
    size_t pad_len = (buffer_idx < 56) ? (56 - buffer_idx) : (120 - buffer_idx);

    uint8 len_bytes[8];
    for (int i = 0; i < 8; i++) {
        len_bytes[i] = (uint8)((bits >> ((7 - i) * 8)) & 0xFF);
    }

    sha256_update(ctx, pad, pad_len);
    sha256_update(ctx, len_bytes, 8);

    for (int i = 0; i < 8; i++) {
        store32_be(&digest[i * 4], ctx->state[i]);
    }
}

static void sha256_hash(const uint8 *data, size_t len, uint8 digest[32])
{
    SHA256_CTX ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, digest);
}

/* HMAC-SHA256 */
static void hmac_sha256(const uint8 *key, size_t key_len, const uint8 *data, size_t data_len, uint8 out[32])
{
    uint8 k_pad[64];
    memset(k_pad, 0, sizeof(k_pad));

    if (key_len > 64) {
        sha256_hash(key, key_len, k_pad);
    } else {
        memcpy(k_pad, key, key_len);
    }

    uint8 ipad[64], opad[64];
    for (int i = 0; i < 64; i++) {
        ipad[i] = k_pad[i] ^ 0x36;
        opad[i] = k_pad[i] ^ 0x5c;
    }

    uint8 inner[32];
    SHA256_CTX ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, ipad, 64);
    sha256_update(&ctx, data, data_len);
    sha256_final(&ctx, inner);

    sha256_init(&ctx);
    sha256_update(&ctx, opad, 64);
    sha256_update(&ctx, inner, 32);
    sha256_final(&ctx, out);
}

/* PBKDF2-HMAC-SHA256 (32 bytes output) */
static void pbkdf2_sha256(const char *pass, size_t pass_len, const uint8 *salt, size_t salt_len, uint32 iterations, uint8 out_key[32])
{
    uint8 salt_block[64];
    size_t s_len = (salt_len > 60) ? 60 : salt_len;
    memcpy(salt_block, salt, s_len);
    salt_block[s_len + 0] = 0;
    salt_block[s_len + 1] = 0;
    salt_block[s_len + 2] = 0;
    salt_block[s_len + 3] = 1; /* block 1 */

    uint8 U[32];
    hmac_sha256((const uint8 *)pass, pass_len, salt_block, s_len + 4, U);
    memcpy(out_key, U, 32);

    for (uint32 iter = 1; iter < iterations; iter++) {
        hmac_sha256((const uint8 *)pass, pass_len, U, 32, U);
        for (int i = 0; i < 32; i++) {
            out_key[i] ^= U[i];
        }
    }
}

/* ChaCha20 (RFC 7539 / 8439) */
#define CHACHA_QR(a, b, c, d) \
    a += b; d ^= a; d = ROTL32(d, 16); \
    c += d; b ^= c; b = ROTL32(b, 12); \
    a += b; d ^= a; d = ROTL32(d, 8);  \
    c += d; b ^= c; b = ROTL32(b, 7);

static void chacha20_block(const uint32 in_state[16], uint8 out[64])
{
    uint32 x[16];
    memcpy(x, in_state, sizeof(x));

    for (int i = 0; i < 10; i++) {
        /* Column round */
        CHACHA_QR(x[0], x[4], x[8],  x[12]);
        CHACHA_QR(x[1], x[5], x[9],  x[13]);
        CHACHA_QR(x[2], x[6], x[10], x[14]);
        CHACHA_QR(x[3], x[7], x[11], x[15]);
        /* Diagonal round */
        CHACHA_QR(x[0], x[5], x[10], x[15]);
        CHACHA_QR(x[1], x[6], x[11], x[12]);
        CHACHA_QR(x[2], x[7], x[8],  x[13]);
        CHACHA_QR(x[3], x[4], x[9],  x[14]);
    }

    for (int i = 0; i < 16; i++) {
        store32_le(&out[i * 4], x[i] + in_state[i]);
    }
}

static void chacha20_crypt(const uint8 key[32], const uint8 nonce[12], uint32 counter, uint8 *data, size_t len)
{
    uint32 state[16];
    state[0] = 0x61707865; /* "expa" */
    state[1] = 0x3320646e; /* "nd 3" */
    state[2] = 0x79622d32; /* "2-by" */
    state[3] = 0x6b206574; /* "te k" */

    for (int i = 0; i < 8; i++) {
        state[4 + i] = load32_le(&key[i * 4]);
    }
    state[12] = counter;
    for (int i = 0; i < 3; i++) {
        state[13 + i] = load32_le(&nonce[i * 4]);
    }

    uint8 block[64];
    for (size_t offset = 0; offset < len; offset += 64) {
        chacha20_block(state, block);
        state[12]++;
        size_t block_len = (len - offset < 64) ? (len - offset) : 64;
        for (size_t b = 0; b < block_len; b++) {
            data[offset + b] ^= block[b];
        }
    }
}

/* Default key used when no custom master password was configured */
static const char *s_default_internal_pass = "VortexBrowse-OS4-SecurePasswordStore-Key-2026";

/* File paths */
#define VB_PW_PATH_ENVARC "ENVARC:VortexBrowse/passwords.enc"
#define VB_PW_PATH_ENV    "ENV:VortexBrowse/passwords.enc"
#define VB_PW_PATH_LOCAL  "PROGDIR:passwords.enc"

/* File header: "VBENC01\0" */
#define VB_PW_MAGIC "VBENC01"
#define VB_PW_ROUNDS 2048

typedef struct {
    char   magic[8];       /* "VBENC01\0" */
    uint8  salt[16];       /* Random salt */
    uint8  nonce[12];      /* Random ChaCha20 nonce */
    uint32 rounds;         /* PBKDF2 iteration count (2048) */
    uint8  hmac[32];       /* HMAC-SHA256 over salt+nonce+ciphertext */
} EncryptedFileHeader;

/* Decrypted payload header */
#define VB_PAYLOAD_MAGIC "VBPASS01"

typedef struct {
    char   magic[8];
    uint32 count;
    uint32 has_custom_master;
    uint32 reserved;
} DecryptedPayloadHeader;

/* Random bytes generator */
static void generate_random_bytes(uint8 *buf, size_t len)
{
    static uint32 seed = 0;
    if (seed == 0) {
        seed = (uint32)time(NULL) ^ 0xA55A3C4D;
    }
    for (size_t i = 0; i < len; i++) {
        seed = seed * 1664525 + 1013904223;
        buf[i] = (uint8)(seed >> 16);
    }
}

/* =========================================================================
 * Password Store Lifecycle & Operations
 * ========================================================================= */

BOOL passwords_init(VortexApp *app)
{
    if (!app) return FALSE;

    if (!app->password_store) {
        app->password_store = (PasswordStore *)calloc(1, sizeof(PasswordStore));
        if (!app->password_store) {
            DBG_LOG("[Passwords] ERROR: Out of memory allocating password store!\n");
            return FALSE;
        }
    }

    /* Try to unlock with default key first (if user didn't set custom master pass) */
    if (passwords_file_exists()) {
        passwords_unlock(app, s_default_internal_pass);
    } else {
        /* New empty store */
        app->password_store->count = 0;
        app->password_store->unlocked = TRUE;
        app->password_store->has_custom_master = FALSE;
        strncpy(app->password_store->master_password, s_default_internal_pass, sizeof(app->password_store->master_password) - 1);
    }

    return TRUE;
}

void passwords_cleanup(VortexApp *app)
{
    if (!app || !app->password_store) return;

    if (app->password_store->modified) {
        passwords_save(app);
    }

    /* Zero memory securely before freeing */
    memset(app->password_store, 0, sizeof(PasswordStore));
    free(app->password_store);
    app->password_store = NULL;
}

BOOL passwords_file_exists(void)
{
    FILE *fp = fopen(VB_PW_PATH_ENV, "rb");
    if (fp) { fclose(fp); return TRUE; }

    fp = fopen(VB_PW_PATH_ENVARC, "rb");
    if (fp) { fclose(fp); return TRUE; }

    fp = fopen(VB_PW_PATH_LOCAL, "rb");
    if (fp) { fclose(fp); return TRUE; }

    return FALSE;
}

BOOL passwords_unlock(VortexApp *app, const char *master_pw)
{
    if (!app || !app->password_store) return FALSE;

    const char *try_pw = (master_pw && master_pw[0] != '\0') ? master_pw : s_default_internal_pass;

    FILE *fp = fopen(VB_PW_PATH_ENV, "rb");
    if (!fp) fp = fopen(VB_PW_PATH_ENVARC, "rb");
    if (!fp) fp = fopen(VB_PW_PATH_LOCAL, "rb");
    if (!fp) {
        DBG_LOG("[Passwords] No password file found to unlock.\n");
        return FALSE;
    }

    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (fsize < (long)sizeof(EncryptedFileHeader)) {
        DBG_LOG("[Passwords] File too small to be valid encrypted store!\n");
        fclose(fp);
        return FALSE;
    }

    EncryptedFileHeader hdr;
    if (fread(&hdr, 1, sizeof(hdr), fp) != sizeof(hdr)) {
        fclose(fp);
        return FALSE;
    }

    if (memcmp(hdr.magic, VB_PW_MAGIC, 7) != 0) {
        DBG_LOG("[Passwords] Invalid file magic!\n");
        fclose(fp);
        return FALSE;
    }

    size_t cipher_len = fsize - sizeof(EncryptedFileHeader);
    uint8 *cipher_buf = (uint8 *)malloc(cipher_len);
    if (!cipher_buf) {
        fclose(fp);
        return FALSE;
    }

    if (fread(cipher_buf, 1, cipher_len, fp) != cipher_len) {
        free(cipher_buf);
        fclose(fp);
        return FALSE;
    }
    fclose(fp);

    /* Derive key via PBKDF2 */
    uint32 rounds = load32_be((const uint8 *)&hdr.rounds);
    if (rounds == 0 || rounds > 100000) rounds = VB_PW_ROUNDS;

    uint8 key[32];
    pbkdf2_sha256(try_pw, strlen(try_pw), hdr.salt, 16, rounds, key);

    /* Verify HMAC-SHA256 */
    uint8 verify_hmac[32];
    uint8 *hmac_data = (uint8 *)malloc(16 + 12 + cipher_len);
    if (!hmac_data) {
        free(cipher_buf);
        return FALSE;
    }
    memcpy(hmac_data, hdr.salt, 16);
    memcpy(hmac_data + 16, hdr.nonce, 12);
    memcpy(hmac_data + 28, cipher_buf, cipher_len);

    hmac_sha256(key, 32, hmac_data, 16 + 12 + cipher_len, verify_hmac);
    free(hmac_data);

    if (memcmp(verify_hmac, hdr.hmac, 32) != 0) {
        /* Wrong password or modified/corrupted file */
        free(cipher_buf);
        return FALSE;
    }

    /* Decrypt ciphertext with ChaCha20 */
    chacha20_crypt(key, hdr.nonce, 1, cipher_buf, cipher_len);

    /* Parse decrypted payload */
    if (cipher_len < sizeof(DecryptedPayloadHeader)) {
        free(cipher_buf);
        return FALSE;
    }

    DecryptedPayloadHeader *p_hdr = (DecryptedPayloadHeader *)cipher_buf;
    if (memcmp(p_hdr->magic, VB_PAYLOAD_MAGIC, 8) != 0) {
        DBG_LOG("[Passwords] Decrypted payload header mismatch!\n");
        free(cipher_buf);
        return FALSE;
    }

    uint32 count = load32_be((const uint8 *)&p_hdr->count);
    if (count > VB_MAX_PASSWORDS) count = VB_MAX_PASSWORDS;

    app->password_store->count = (int)count;
    app->password_store->has_custom_master = (load32_be((const uint8 *)&p_hdr->has_custom_master) != 0);
    app->password_store->unlocked = TRUE;
    strncpy(app->password_store->master_password, try_pw, sizeof(app->password_store->master_password) - 1);

    const uint8 *entry_ptr = cipher_buf + sizeof(DecryptedPayloadHeader);
    for (uint32 i = 0; i < count; i++) {
        if ((size_t)(entry_ptr - cipher_buf + sizeof(PasswordEntry)) > cipher_len) break;
        memcpy(&app->password_store->entries[i], entry_ptr, sizeof(PasswordEntry));
        entry_ptr += sizeof(PasswordEntry);
    }

    free(cipher_buf);
    DBG_LOG("[Passwords] Successfully unlocked %d encrypted website credentials.\n", app->password_store->count);
    return TRUE;
}

BOOL passwords_save(VortexApp *app)
{
    if (!app || !app->password_store || !app->password_store->unlocked) return FALSE;

    /* Build decrypted payload buffer */
    size_t payload_len = sizeof(DecryptedPayloadHeader) + app->password_store->count * sizeof(PasswordEntry);
    uint8 *payload = (uint8 *)malloc(payload_len);
    if (!payload) return FALSE;

    DecryptedPayloadHeader *p_hdr = (DecryptedPayloadHeader *)payload;
    memcpy(p_hdr->magic, VB_PAYLOAD_MAGIC, 8);
    store32_be((uint8 *)&p_hdr->count, (uint32)app->password_store->count);
    store32_be((uint8 *)&p_hdr->has_custom_master, app->password_store->has_custom_master ? 1 : 0);
    store32_be((uint8 *)&p_hdr->reserved, 0);

    uint8 *entry_dest = payload + sizeof(DecryptedPayloadHeader);
    for (int i = 0; i < app->password_store->count; i++) {
        memcpy(entry_dest, &app->password_store->entries[i], sizeof(PasswordEntry));
        entry_dest += sizeof(PasswordEntry);
    }

    /* Setup encrypted file header */
    EncryptedFileHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    memcpy(hdr.magic, VB_PW_MAGIC, 8);
    generate_random_bytes(hdr.salt, 16);
    generate_random_bytes(hdr.nonce, 12);
    store32_be((uint8 *)&hdr.rounds, VB_PW_ROUNDS);

    /* Derive encryption key */
    const char *pw = app->password_store->master_password;
    if (pw[0] == '\0') pw = s_default_internal_pass;

    uint8 key[32];
    pbkdf2_sha256(pw, strlen(pw), hdr.salt, 16, VB_PW_ROUNDS, key);

    /* Encrypt payload using ChaCha20 */
    chacha20_crypt(key, hdr.nonce, 1, payload, payload_len);

    /* Calculate HMAC-SHA256 */
    uint8 *hmac_data = (uint8 *)malloc(16 + 12 + payload_len);
    if (!hmac_data) {
        free(payload);
        return FALSE;
    }
    memcpy(hmac_data, hdr.salt, 16);
    memcpy(hmac_data + 16, hdr.nonce, 12);
    memcpy(hmac_data + 28, payload, payload_len);
    hmac_sha256(key, 32, hmac_data, 16 + 12 + payload_len, hdr.hmac);
    free(hmac_data);

    /* Write to disk */
    const char *paths[] = { VB_PW_PATH_ENVARC, VB_PW_PATH_ENV, VB_PW_PATH_LOCAL };
    BOOL any_saved = FALSE;

    for (int p = 0; p < 3; p++) {
        FILE *fp = fopen(paths[p], "wb");
        if (!fp) {
            /* Try to create directory if ENVARC:VortexBrowse does not exist */
            if (p == 0) {
                IDOS->CreateDir("ENVARC:VortexBrowse");
                fp = fopen(paths[p], "wb");
            } else if (p == 1) {
                IDOS->CreateDir("ENV:VortexBrowse");
                fp = fopen(paths[p], "wb");
            }
        }
        if (fp) {
            fwrite(&hdr, 1, sizeof(hdr), fp);
            fwrite(payload, 1, payload_len, fp);
            fclose(fp);
            any_saved = TRUE;
        }
    }

    free(payload);
    app->password_store->modified = FALSE;
    DBG_LOG("[Passwords] Saved %d encrypted passwords to storage.\n", app->password_store->count);
    return any_saved;
}

BOOL passwords_set_master(VortexApp *app, const char *new_master_pw)
{
    if (!app || !app->password_store) return FALSE;

    if (!new_master_pw || new_master_pw[0] == '\0') {
        /* Reset to default internal pass */
        app->password_store->has_custom_master = FALSE;
        strncpy(app->password_store->master_password, s_default_internal_pass, sizeof(app->password_store->master_password) - 1);
    } else {
        app->password_store->has_custom_master = TRUE;
        strncpy(app->password_store->master_password, new_master_pw, sizeof(app->password_store->master_password) - 1);
    }

    app->password_store->modified = TRUE;
    return passwords_save(app);
}

BOOL passwords_add(VortexApp *app, const char *domain, const char *user, const char *pass, const char *notes)
{
    if (!app || !app->password_store || !app->password_store->unlocked) return FALSE;
    if (app->password_store->count >= VB_MAX_PASSWORDS) return FALSE;

    int idx = app->password_store->count;
    memset(&app->password_store->entries[idx], 0, sizeof(PasswordEntry));

    if (domain) strncpy(app->password_store->entries[idx].domain, domain, VB_PW_MAX_DOMAIN - 1);
    if (user)   strncpy(app->password_store->entries[idx].username, user, VB_PW_MAX_USER - 1);
    if (pass)   strncpy(app->password_store->entries[idx].password, pass, VB_PW_MAX_PASS - 1);
    if (notes)  strncpy(app->password_store->entries[idx].notes, notes, VB_PW_MAX_NOTES - 1);

    app->password_store->count++;
    app->password_store->modified = TRUE;
    passwords_save(app);
    return TRUE;
}

BOOL passwords_update(VortexApp *app, int index, const char *domain, const char *user, const char *pass, const char *notes)
{
    if (!app || !app->password_store || index < 0 || index >= app->password_store->count) return FALSE;

    if (domain) strncpy(app->password_store->entries[index].domain, domain, VB_PW_MAX_DOMAIN - 1);
    if (user)   strncpy(app->password_store->entries[index].username, user, VB_PW_MAX_USER - 1);
    if (pass)   strncpy(app->password_store->entries[index].password, pass, VB_PW_MAX_PASS - 1);
    if (notes)  strncpy(app->password_store->entries[index].notes, notes, VB_PW_MAX_NOTES - 1);

    app->password_store->modified = TRUE;
    passwords_save(app);
    return TRUE;
}

BOOL passwords_remove(VortexApp *app, int index)
{
    if (!app || !app->password_store || index < 0 || index >= app->password_store->count) return FALSE;

    for (int i = index; i < app->password_store->count - 1; i++) {
        app->password_store->entries[i] = app->password_store->entries[i + 1];
    }
    app->password_store->count--;
    app->password_store->modified = TRUE;
    passwords_save(app);
    return TRUE;
}

static const char *vb_strcasestr(const char *haystack, const char *needle)
{
    if (!haystack || !needle) return NULL;
    if (!*needle) return haystack;
    size_t nlen = strlen(needle);
    for (; *haystack; haystack++) {
        if (strncasecmp(haystack, needle, nlen) == 0) {
            return haystack;
        }
    }
    return NULL;
}

const PasswordEntry *passwords_find_by_domain(VortexApp *app, const char *domain)
{
    if (!app || !app->password_store || !domain || domain[0] == '\0') return NULL;

    /* Extract clean host from URL (strip http://, https://, www., path, port) */
    char clean_host[256];
    const char *p = domain;
    if (strncmp(p, "http://", 7) == 0) p += 7;
    else if (strncmp(p, "https://", 8) == 0) p += 8;
    if (strncmp(p, "www.", 4) == 0) p += 4;
    strncpy(clean_host, p, sizeof(clean_host) - 1);
    clean_host[sizeof(clean_host) - 1] = '\0';
    char *slash = strchr(clean_host, '/');
    if (slash) *slash = '\0';
    char *colon = strchr(clean_host, ':');
    if (colon) *colon = '\0';

    for (int i = 0; i < app->password_store->count; i++) {
        const char *entry_dom = app->password_store->entries[i].domain;
        if (!entry_dom || entry_dom[0] == '\0') continue;

        if (vb_strcasestr(domain, entry_dom) != NULL ||
            vb_strcasestr(entry_dom, domain) != NULL ||
            (clean_host[0] != '\0' && (vb_strcasestr(clean_host, entry_dom) != NULL ||
                                      vb_strcasestr(entry_dom, clean_host) != NULL))) {
            return &app->password_store->entries[i];
        }
    }
    return NULL;
}
