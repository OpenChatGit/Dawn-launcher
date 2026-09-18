#ifndef MODULES_UI_MODAL_SKIN_H
#define MODULES_UI_MODAL_SKIN_H

#include "shared/api.h"
#include "shared/icons.h"

#define MODAL_INSET 18.0f
#define MODAL_RADIUS 14.0f
#define MODAL_CLOSE 32.0f
#define MODAL_BTN_H 40.0f
#define MODAL_FIELD_H 40.0f
#define MODAL_TITLE_PX 15.0f
#define MODAL_SUB_PX 11.0f
#define MODAL_GAP 8.0f

int modal_px(float value, float scale);
int modal_hit(int mx, int my, float x, float y, float w, float h);
float modal_approach(float current, float target, float dt);
uint32_t modal_mix(uint32_t a, uint32_t b, int amount);
uint32_t modal_panel_color(void);
uint32_t modal_field_color(void);
uint32_t modal_button_fill(float hover);

float modal_lift(float anim);
void modal_draw_overlay(void *hdc, float w, float h, float anim);
void modal_draw_card(void *hdc, float x, float y, float w, float h, float anim, float scale);
void modal_place_close(
    float card_x,
    float card_y,
    float card_w,
    float scale,
    float *x,
    float *y,
    float *s
);
void modal_draw_close(void *hdc, float x, float y, float s, float hover, float anim);
void modal_draw_title(
    void *hdc,
    float x,
    float y,
    float w,
    float h,
    const wchar_t *text,
    float scale,
    float anim
);
void modal_draw_subtitle(
    void *hdc,
    float x,
    float y,
    float w,
    float h,
    const wchar_t *text,
    float scale,
    float anim
);
void modal_draw_field(
    void *hdc,
    float x,
    float y,
    float w,
    float h,
    const wchar_t *text,
    float anim,
    float scale
);
void modal_draw_button(
    void *hdc,
    float x,
    float y,
    float w,
    float h,
    const wchar_t *text,
    IconId icon,
    float hover,
    float anim,
    int disabled,
    float scale
);

#endif
