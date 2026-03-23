#include "auth/auth_store.h"

#include "lib/string.h"

/* ── Session state ───────────────────────────────────────────────── */

static char g_session_username[16];
static int  g_session_active = 0;

/* ── Credential verification ─────────────────────────────────────── */

int auth_verify_devacc(const char *pin) {
    if (!pin) return 0;
    return str_eq(pin, AUTH_DEVACC_PIN);
}

/* ── Session API ─────────────────────────────────────────────────── */

void auth_session_begin(const char *username) {
    str_copy(g_session_username, username ? username : "", sizeof(g_session_username));
    g_session_active = (g_session_username[0] != '\0');
}

void auth_session_end(void) {
    g_session_username[0] = '\0';
    g_session_active = 0;
}

const char *auth_session_username(void) {
    return g_session_username;
}

int auth_session_active(void) {
    return g_session_active;
}
