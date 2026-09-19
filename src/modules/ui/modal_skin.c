#include "modal_skin.h"
#include "shared/chrome.h"
#include "shared/theme.h"
#include "theme/theme_desc.h"

#include <math.h>

int
modal_px(float value, float scale)
{
    return (int)(value * scale + 0.5f);
}

int
modal_hit(int mx, int my, float x, float y, float w, float h)
{
    return (float)mx >= x && (float)my >= y && (float)mx < x + w && (float)my < y + h;
}

float
modal_approach(float current, float target, float dt)
{
    float step;

    if (dt < 0.0f) {
        dt = 0.0f;
    }
    if (dt > 0.05f) {
        dt = 0.05f;
    }
    step = dt / 0.16f;
    if (current < target) {
        current += step;
        if (current > target) {
            current = target;
        }
    } else if (current > target) {
        current -= step;
        if (current < target) {
            current = target;
        }
    }
    if (current < 0.001f) {
        return 0.0f;
    }
    if (current > 0.999f && target >= 1.0f) {
        return 1.0f;
    }
    return current;
}

uint32_t
modal_mix(uint32_t a, uint32_t b, int amount)
{
    int ar;
    int ag;
    int ab;
    int br;
    int bg;
    int bb;
    int r;
    int g;
    int bl;

    if (amount < 0) {
        amount = 0;
    }
    if (amount > 255) {
        amount = 255;
    }
    ar = (int)((a >> 16) & 0xff);
    ag = (int)((a >> 8) & 0xff);
    ab = (int)(a & 0xff);
    br = (int)((b >> 16) & 0xff);
    bg = (int)((b >> 8) & 0xff);
    bb = (int)(b & 0xff);
    r = ar + (br - ar) * amount / 255;
    g = ag + (bg - ag) * amount / 255;
    bl = ab + (bb - ab) * amount / 255;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)bl;
}

uint32_t
modal_panel_color(void)
{
    const ThemeDesc *desc = theme_desc_current();
    uint32_t clear = desc ? desc->clear : 0x010613;
    uint32_t dark = modal_mix(clear, 0x000000, 80);
    return modal_mix(dark, 0xffffff, 6);
}

uint32_t
modal_field_color(void)
{
    return modal_mix(modal_panel_color(), 0xffffff, 12);
}

uint32_t
modal_button_fill(float hover)
{
    int amount = 12 + (int)(hover * 14.0f);
    return modal_mix(modal_panel_color(), 0xffffff, amount);
}

float
modal_lift(float anim)
{
    (void)anim;
    return 0.0f;
}

static int
modal_alpha(float anim)
{
    int alpha = (int)(anim * 255.0f + 0.5f);
    if (alpha < 0) {
        return 0;
    }
    if (alpha > 255) {
        return 255;
    }
    return alpha;
}

void
modal_draw_overlay(void *hdc, float w, float h, float anim)
{
    int alpha = modal_alpha(anim) * 204 / 255;
    if (alpha <= 0) {
        return;
    }
    icon_fill_rect(hdc, 0.0f, 0.0f, w, h, 0x000000, alpha);
}

void
modal_draw_card(void *hdc, float x, float y, float w, float h, float anim, float scale)
{
    const ThemeChrome *chrome = theme_chrome();
    float radius = (float)modal_px(MODAL_RADIUS, scale);
    int alpha = modal_alpha(anim);
    icon_round_rect(hdc, x, y + 4.0f, w, h, radius, 0x000000, alpha * 56 / 255);
    icon_round_rect(hdc, x, y, w, h, radius, modal_panel_color(), alpha * 252 / 255);
    icon_round_stroke(hdc, x, y, w, h, radius, chrome->muted, alpha * 40 / 255, 1.0f);
}

void
modal_place_close(
    float card_x,
    float card_y,
    float card_w,
    float scale,
    float *x,
    float *y,
    float *s
)
{
    float inset = (float)modal_px(MODAL_INSET, scale);
    float size = (float)modal_px(MODAL_CLOSE, scale);
    if (s) {
        *s = size;
    }
    if (x) {
        *x = card_x + card_w - inset - size + (float)modal_px(4, scale);
    }
    if (y) {
        *y = card_y + (float)modal_px(8, scale);
    }
}

void
modal_draw_close(void *hdc, float x, float y, float s, float hover, float anim)
{
    const ThemeChrome *chrome = theme_chrome();
    int fill = (int)(anim * hover * 40.0f);
    if (fill > 0) {
        icon_round_rect(hdc, x, y, s, s, s * 0.38f, chrome->hover, fill);
    }
    icon_set_alpha(modal_alpha(anim));
    icon_draw(
        hdc,
        ICON_X,
        x + s * 0.5f,
        y + s * 0.5f,
        s * 0.48f,
        hover > 0.2f ? chrome->hover : chrome->muted,
        1.15f
    );
    icon_set_alpha(255);
}

void
modal_draw_title(
    void *hdc,
    float x,
    float y,
    float w,
    float h,
    const wchar_t *text,
    float scale,
    float anim
)
{
    const ThemeChrome *chrome = theme_chrome();
    icon_draw_label_alpha(
        hdc,
        x,
        y,
        w,
        h,
        text,
        chrome->title_color,
        (float)modal_px(MODAL_TITLE_PX, scale),
        600,
        modal_alpha(anim)
    );
}

void
modal_draw_subtitle(
    void *hdc,
    float x,
    float y,
    float w,
    float h,
    const wchar_t *text,
    float scale,
    float anim
)
{
    const ThemeChrome *chrome = theme_chrome();
    icon_draw_label_alpha(
        hdc,
        x,
        y,
        w,
        h,
        text,
        chrome->muted,
        (float)modal_px(MODAL_SUB_PX, scale),
        400,
        modal_alpha(anim)
    );
}

void
modal_draw_field(
    void *hdc,
    float x,
    float y,
    float w,
    float h,
    const wchar_t *text,
    float anim,
    float scale
)
{
    modal_draw_input(hdc, x, y, w, h, text, L"", 0, 0, anim, scale);
}

void
modal_draw_input(
    void *hdc,
    float x,
    float y,
    float w,
    float h,
    const wchar_t *text,
    const wchar_t *placeholder,
    int focused,
    int caret_on,
    float anim,
    float scale
)
{
    const ThemeChrome *chrome = theme_chrome();
    float radius = h * 0.30f;
    float pad = (float)modal_px(14, scale);
    float px = (float)modal_px(13, scale);
    int alpha = (int)(anim * 255.0f);
    uint32_t fill = focused ? modal_mix(modal_field_color(), 0xffffff, 10) : modal_field_color();
    uint32_t stroke = focused ? chrome->hover : chrome->muted;
    int stroke_a = (int)(anim * (focused ? 88.0f : 34.0f));
    const wchar_t *show = (text && text[0]) ? text : (placeholder ? placeholder : L"");
    uint32_t fg = (text && text[0]) ? chrome->title_color : chrome->muted;
    float caret_x;
    float caret_h;
    float caret_y;
    float text_w = w - pad * 2.0f;

    icon_round_rect(hdc, x, y, w, h, radius, fill, alpha);
    icon_round_stroke(hdc, x, y, w, h, radius, stroke, stroke_a, focused ? 1.35f : 1.0f);
    icon_draw_label_alpha(
        hdc,
        x + pad,
        y,
        text_w,
        h,
        show,
        fg,
        px,
        400,
        modal_alpha(anim)
    );
    if (focused && caret_on) {
        caret_x = pad;
        if (text && text[0]) {
            caret_x += icon_measure_label(hdc, text, px, 400);
        }
        if (caret_x < pad) {
            caret_x = pad;
        }
        if (caret_x > w - pad) {
            caret_x = w - pad;
        }
        caret_h = h * 0.46f;
        caret_y = y + (h - caret_h) * 0.5f;
        icon_fill_rect(
            hdc,
            x + caret_x,
            caret_y,
            (float)modal_px(1.25f, scale),
            caret_h,
            chrome->title_color,
            alpha
        );
    }
}

void
modal_draw_button(
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
)
{
    const ThemeChrome *chrome = theme_chrome();
    float radius = h * 0.28f;
    float use_hover = disabled ? 0.0f : hover;
    uint32_t fill = modal_button_fill(use_hover);
    uint32_t fg = disabled ? chrome->muted : (use_hover > 0.2f ? chrome->hover : chrome->title_color);
    float pad = (float)modal_px(12, scale);
    float icon_s = (float)modal_px(18, scale);

    icon_round_rect(hdc, x, y, w, h, radius, fill, (int)(anim * (disabled ? 180.0f : 255.0f)));
    icon_round_stroke(
        hdc,
        x,
        y,
        w,
        h,
        radius,
        use_hover > 0.2f ? chrome->hover : chrome->muted,
        (int)(anim * (28.0f + use_hover * 24.0f)),
        1.0f
    );
    if (icon < ICON_COUNT) {
        icon_set_alpha(modal_alpha(anim));
        icon_draw(hdc, icon, x + pad + icon_s * 0.5f, y + h * 0.5f, icon_s, fg, 1.0f);
        icon_set_alpha(255);
        icon_draw_label_alpha(
            hdc,
            x + pad + icon_s + (float)modal_px(8, scale),
            y,
            w - pad * 2.0f - icon_s,
            h,
            text,
            fg,
            (float)modal_px(13, scale),
            600,
            modal_alpha(anim)
        );
    } else {
        icon_draw_label_center_alpha(
            hdc,
            x,
            y,
            w,
            h,
            text,
            fg,
            (float)modal_px(13, scale),
            600,
            modal_alpha(anim)
        );
    }
}
