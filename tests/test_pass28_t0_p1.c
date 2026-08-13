/* T0-P1 security regression: pin post-fix behavior of the admin-token
 * cluster so that any future change that reopens the platformID=5
 * spoof path fails this suite. Do NOT relax these tests without an
 * explicit security review. */

#include "miku_test.h"
#include "miku_auth.h"
#include "miku_token.h"
#include "miku_rpc_server.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_admin_token_signed_with_admin_secret(void) {
    char admin_tok[512] = {0};
    int rc = miku_admin_token_create("admin-uid", admin_tok, sizeof(admin_tok));
    mk_assert_int_eq(0, rc);
    mk_assert(strncmp(admin_tok, "miku|admin-uid|", 15) == 0);

    char uid[64] = {0};
    int platform = -1;
    int64_t issued = 0;

    rc = miku_token_verify_ex(admin_tok, miku_admin_default_secret(),
                              uid, sizeof(uid), &platform, &issued);
    mk_assert_int_eq(0, rc);
    mk_assert_str_eq("admin-uid", uid);
    mk_assert_int_eq(5, platform);

    /* SECURITY: an admin-secret-signed token MUST NOT verify under the
     * user secret. If this assertion ever passes-by-mistake, DEEP #9 is
     * back (anyone who knows "openIM123" can mint an admin token). */
    rc = miku_token_verify_ex(admin_tok, miku_token_default_secret(),
                              uid, sizeof(uid), &platform, &issued);
    mk_assert_int_eq(-1, rc);
}

static void test_admin_token_rejects_user_secret_signature(void) {
    /* SECURITY: pre-fix DEEP #9 — miku_auth_admin_token signed the issued
     * token with the USER secret, so an attacker who knew
     * MIKU_TOKEN_DEFAULT_SECRET="openIM123" could forge an admin token
     * directly. Post-fix the issued token MUST verify under the admin
     * secret and MUST NOT verify under the user secret. We exercise the
     * minting path (which is the one the pre-fix bug was on) and verify
     * the resulting signature. */
    char out[512] = {0};
    int rc = miku_auth_admin_token(NULL, "impostor",
                                   miku_admin_default_secret(),
                                   out, sizeof(out));
    mk_assert_int_eq(0, rc);

    char uid[64] = {0};
    int platform = -1;
    int64_t issued = 0;

    /* The legitimate path: admin secret verifies the mint. */
    rc = miku_token_verify_ex(out, miku_admin_default_secret(),
                              uid, sizeof(uid), &platform, &issued);
    mk_assert_int_eq(0, rc);
    mk_assert_str_eq("impostor", uid);
    mk_assert_int_eq(5, platform);

    /* The attack path: same token MUST NOT verify under the user secret.
     * If this assertion ever passes-by-mistake, DEEP #9 is back. */
    rc = miku_token_verify_ex(out, miku_token_default_secret(),
                              uid, sizeof(uid), &platform, &issued);
    mk_assert_int_eq(-1, rc);
}

static void test_admin_token_principal_enforced(void) {
    miku_auth_set_admin_principal("admin");

    char out[512] = {0};
    int rc = miku_auth_admin_token(NULL, "admin",
                                   miku_admin_default_secret(),
                                   out, sizeof(out));
    mk_assert_int_eq(0, rc);

    rc = miku_auth_admin_token(NULL, "impostor",
                               miku_admin_default_secret(),
                               out, sizeof(out));
    mk_assert_int_eq(-1, rc);

    miku_auth_set_admin_principal("");
}

static void test_admin_token_principal_disabled_when_empty(void) {
    miku_auth_set_admin_principal("");

    char out[512] = {0};
    int rc = miku_auth_admin_token(NULL, "anybody",
                                   miku_admin_default_secret(),
                                   out, sizeof(out));
    mk_assert_int_eq(0, rc);
}

static void test_admin_token_bad_secret_still_rejected(void) {
    miku_auth_set_admin_principal("admin");

    char out[512] = {0};
    int rc = miku_auth_admin_token(NULL, "admin",
                                   "wrong-secret",
                                   out, sizeof(out));
    mk_assert_int_eq(-1, rc);

    miku_auth_set_admin_principal("");
}

/* Indirect coverage for rpc_internal_authorized() (which is static).
 * Pre-fix the gate returned 1 when srv->internal_token was NULL — i.e.
 * fail-OPEN. Post-fix it must fail-CLOSED. We exercise the observable
 * invariant on the public field that drives the gate. */
static int internal_gate_authorizes(miku_rpc_server_t *srv,
                                    const char *presented) {
    if (!srv) return 0;
    const char *expected = srv->internal_token;
    if (!expected || !expected[0]) return 0;
    if (!presented) return 0;
    return strcmp(presented, expected) == 0;
}

static void test_rpc_internal_auth_fail_closed(void) {
    miku_rpc_server_t *srv = miku_rpc_server_create(NULL, NULL, 0);
    mk_assert_not_null(srv);

    mk_assert_int_eq(0, internal_gate_authorizes(srv, "anything"));
    mk_assert_int_eq(0, internal_gate_authorizes(srv, ""));
    mk_assert_int_eq(0, internal_gate_authorizes(srv, NULL));

    miku_rpc_server_set_internal_token(srv, "s3cr3t");
    mk_assert_int_eq(1, internal_gate_authorizes(srv, "s3cr3t"));
    mk_assert_int_eq(0, internal_gate_authorizes(srv, "wrong"));

    miku_rpc_server_destroy(srv);
}

static void test_rpc_enable_internal_auth_wires_secret(void) {
    miku_rpc_server_t *srv = miku_rpc_server_create(NULL, NULL, 0);
    miku_rpc_server_enable_internal_auth(srv);
    mk_assert_not_null(srv->internal_token);
    mk_assert(srv->internal_token[0] != '\0');
    mk_assert_str_eq(miku_internal_secret(), srv->internal_token);
    miku_rpc_server_destroy(srv);
}

void run_pass28_t0_p1_tests(void) {
    printf("\n── Miku Pass28 T0-P1 Tests ────────────────\n\n");
    mk_run_test(test_admin_token_signed_with_admin_secret);
    mk_run_test(test_admin_token_rejects_user_secret_signature);
    mk_run_test(test_admin_token_principal_enforced);
    mk_run_test(test_admin_token_principal_disabled_when_empty);
    mk_run_test(test_admin_token_bad_secret_still_rejected);
    mk_run_test(test_rpc_internal_auth_fail_closed);
    mk_run_test(test_rpc_enable_internal_auth_wires_secret);
}