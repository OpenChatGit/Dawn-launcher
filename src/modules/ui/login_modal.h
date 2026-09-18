#ifndef MODULES_UI_LOGIN_MODAL_H
#define MODULES_UI_LOGIN_MODAL_H

#include "shared/api.h"
#include "state/state.h"

void login_modal_open(void);
void login_modal_close(void);
void login_modal_hide(void);
void login_modal_sign_out(void);
void login_modal_sync(AppState *state, Platform *platform);
int login_modal_visible(void);
int login_modal_signed_in(void);
const char *login_modal_username(void);
const char *login_modal_avatar_path(void);
int login_modal_wants_mouse(Platform *platform);
void login_modal_tick(Platform *platform, float dt);

#endif
