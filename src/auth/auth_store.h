#pragma once

/* ------------------------------------------------------------------ *
 * Hardcoded developer account credentials                              *
 * ------------------------------------------------------------------ */

#define AUTH_DEVACC_NAME "devacc"
#define AUTH_DEVACC_PIN  "9898"

/* Verify a PIN against the hardcoded devacc credential.
   Returns 1 if pin matches, 0 otherwise. */
int auth_verify_devacc(const char *pin);

/* ------------------------------------------------------------------ *
 * Runtime session state (not persisted)                                *
 * ------------------------------------------------------------------ */

void        auth_session_begin(const char *username);
void        auth_session_end(void);
const char *auth_session_username(void);
int         auth_session_active(void);
