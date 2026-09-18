#ifndef HOST_STEAM_AUTH_H
#define HOST_STEAM_AUTH_H

#ifdef __cplusplus
extern "C" {
#endif

void steam_auth_init(const char *project_root);
void steam_auth_shutdown(void);
int steam_auth_begin(void);
void steam_auth_cancel(void);
void steam_auth_sign_out(void);
int steam_auth_signed_in(void);
int steam_auth_busy(void);
const char *steam_auth_persona(void);
const char *steam_auth_id(void);
const char *steam_auth_avatar_path(void);
const char *steam_auth_status(void);
const char *steam_auth_dd_user(void);
void steam_auth_set_dd_user(const char *username);
int steam_auth_owns_d2(void);
int steam_auth_owns_forsaken(void);
int steam_auth_owns_red_war(void);

#ifdef __cplusplus
}
#endif

#endif
