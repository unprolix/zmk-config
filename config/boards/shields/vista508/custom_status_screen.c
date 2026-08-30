/*
 * Status screen for the Vista508, laid out for its 144x168 portrait panel.
 *
 * ZMK's built-in screen targets a nice!view (160x68 landscape) and pins each
 * widget to a corner, which on a narrow portrait panel truncates the layer name
 * ("hierophant" came out as "hierophan..."). This lays the same widgets out
 * vertically instead and lets the layer name wrap.
 *
 * Written against LVGL 9 rather than forward-porting the vendor's screen, whose
 * ~200 LVGL 8 call sites and 1.5MB art blob buy artwork we do not need.
 *
 * Widgets are reused from ZMK as-is; all four are plain labels, which is what
 * makes the wrapping and alignment below possible.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zmk/display/status_screen.h>
#include <zmk/display.h>
#include <zmk/battery.h>
#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/activity.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/keymap.h>
#include <zmk-leader-key/leader_state.h>
#include <zmk/display/widgets/wpm_status.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/hid.h>

#if IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)
#include <zmk/events/hid_indicators_changed.h>
#include <zmk/hid_indicators.h>
#endif

#include "bt_names.h"
#include "circlecube_img.h"
#include "hierophant_img.h"
#include "vista_canvas.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* From ../common/bt_names.c: the host on a profile, by name where it has one. */
const char *jjb_bt_name_for(uint8_t profile);

/*
 * THIS FILE IS COMPILED BY MORE THAN ONE SHIELD.
 *
 * vista508 builds it for the Rolio and toucan_display builds it for the
 * Toucan, which carries the same 144x168 Sharp panel; see
 * toucan_display/CMakeLists.txt, which reaches across for these sources rather
 * than keeping a second copy of them. So a Kconfig this file starts depending
 * on has to be enabled in EVERY such shield's .conf, and forgetting one breaks
 * that keyboard while the other goes on building perfectly.
 *
 * That is not hypothetical: montserrat_18 arrived here with the emblem-crossing
 * corner readouts, went into vista508.conf alone, and the Toucan's left half
 * stopped building for a day before anyone noticed.
 *
 * The guards below turn that into a build error naming its own fix, stated
 * where the requirement actually lives instead of in one shield's conf. ADD TO
 * THEM whenever this file gains a dependency.
 */
#if !IS_ENABLED(CONFIG_LV_USE_CANVAS)
#error "vista508 status screen: needs CONFIG_LV_USE_CANVAS=y in this shield's .conf"
#endif
#if !IS_ENABLED(CONFIG_LV_USE_LABEL)
#error "vista508 status screen: needs CONFIG_LV_USE_LABEL=y in this shield's .conf"
#endif
#if !IS_ENABLED(CONFIG_LV_FONT_MONTSERRAT_18)
#error "vista508 status screen: CORNER_FONT is montserrat 18 -- set CONFIG_LV_FONT_MONTSERRAT_18=y in this shield's .conf"
#endif

/* Track the panel size from devicetree so this follows any geometry change. */
#define SCREEN_W DT_PROP(DT_CHOSEN(zephyr_display), width)
#define SCREEN_H DT_PROP(DT_CHOSEN(zephyr_display), height)

/* Leave room for the themed screen's own padding on each side. */
#define CONTENT_W  (SCREEN_W - 16)
#define ROW_H      22
#define STATUS_MAX 24

/*
 * The status readouts sit in the top corners. They no longer have to FIT there
 * -- being XORed, they may cross the art freely -- but they still use a smaller
 * face than the middle band, because at the default 20px "BT2 unpaired" runs
 * past the panel edge rather than merely over the emblem.
 */
/*
 * The corner readouts are XORed into the emblem canvas rather than being labels
 * laid over it.
 *
 * They used to be labels with an opaque backing, which made them legible over
 * any art at the cost of blanking a box of it. On the hierophant that box fell
 * in a corner the spear never reaches and went unnoticed; the circle-cube runs
 * the full width of the panel, and the backing took a visible bite out of the
 * disc. XOR costs nothing -- see vista_xor_canvas() -- so the readout and the
 * art both survive wherever they overlap.
 *
 * The consequence is that the corners belong to the canvas: they are repainted
 * whenever it is, and every change to either has to go through panel_redraw().
 *
 * 18px, and drawn nine times in a 3x3 neighbourhood. LVGL antialiases its
 * fonts and a 1-bit panel thresholds the grey away, which at 14 removed enough
 * of each glyph to leave fragments; overdrawing helped and was not enough on
 * its own.
 */
#define CORNER_H    20
#define CORNER_FONT (&lv_font_montserrat_18)

/* How far the corner readouts sit below the top edge, and the glyphs above the
   bottom one. Both keep a stroke off the panel border. */
#define CORNER_LIFT 1
#define MODS_LIFT   3

/*
 * Connection and battery are drawn here rather than with ZMK's own widgets.
 * Those render LVGL glyphs -- WIFI/USB/OK/CLOSE/SETTINGS -- which are compact
 * enough for a nice!view but cryptic, and this panel has room for words.
 * The state comes from the same APIs ZMK's widgets use.
 *
 * Cached as text because the canvas owns the pixels: a repaint for any reason
 * -- an emblem swap, an animation step, a layer change -- has to be able to put
 * these back without waiting for the next connection or battery event.
 */
static char conn_text[STATUS_MAX];
static char batt_text[STATUS_MAX];
static bool batt_shown;

/*
 * The base layer gets an emblem instead of its name, and held modifiers get a
 * row of line-art glyphs. Both are canvases rather than labels: the emblem is
 * a 1bpp bitmap, and the glyphs exist in no font available here.
 *
 * Their buffers are static rather than drawn from LVGL's pool, which is sized
 * for the widgets (CONFIG_LV_Z_MEM_POOL_SIZE); the emblem alone is larger than
 * a nice!view's entire screen.
 */
static lv_obj_t *emblem_canvas;
static lv_obj_t *mods_canvas_left;
static lv_obj_t *mods_canvas_right;

/*
 * Two emblems, one canvas.
 *
 * The canvas is the FULL PANEL for both of them and each emblem is centred
 * inside it, rather than the canvas being re-declared at each emblem's own
 * dimensions. The two are different shapes -- the hierophant is a tall 125x168
 * and the circle-cube a square 144x144 -- and carrying that difference in the
 * canvas geometry meant calling lv_canvas_set_buffer() on a live, already
 * rendered canvas, which took the keyboard down every time the toggle was
 * pressed. (Not a buffer overrun: the buffer is sized for the larger of the
 * pair and both fit. Re-buffering in place tears down and rebuilds the image
 * source under a widget LVGL is still holding.)
 *
 * Fixed geometry means lv_canvas_set_buffer() is called exactly once, at
 * construction, and a toggle is only a fill-and-blit. It costs nothing
 * visually: the emblem was centred on the panel either way.
 */
/*
 * An emblem is one or more frames of the same size. A still emblem simply has
 * one; the circle-cube has three, differing only in which face of the inner
 * cube carries its 50% checkerboard shading, so cycling them turns the cube --
 * clockwise, which is the frame order, not the model's. See circlecube_img.h.
 */
struct emblem {
    const uint8_t *const *frames;
    uint8_t frame_count;
    lv_coord_t w, h, stride;
};

static const uint8_t *const hierophant_frames[] = {hierophant_bits};

static const struct emblem emblems[] = {
    {hierophant_frames, ARRAY_SIZE(hierophant_frames), HIEROPHANT_W, HIEROPHANT_H,
     HIEROPHANT_STRIDE},
    {circlecube_frames, ARRAY_SIZE(circlecube_frames), CIRCLECUBE_W, CIRCLECUBE_H,
     CIRCLECUBE_STRIDE},
};

static uint8_t emblem_choice;
static uint8_t emblem_frame;

/*
 * Whether the art is drawn at all.
 *
 * The canvas itself is ALWAYS visible, because the corner readouts live in it
 * now and they are wanted on every layer. What comes and goes is the emblem:
 * the middle band belongs to the layer name or the leader list on every layer
 * but the base one.
 */
static bool emblem_shown;

/*
 * The canvas is the panel, not the largest emblem: an emblem may be narrower
 * than 144 (the hierophant is) and centring it inside a canvas cropped to the
 * widest of the pair would still be centring it on the panel, but a future
 * emblem shorter than 168 would sit against the top edge instead of the
 * middle. Take the geometry from the display and be done with it.
 */
#define EMBLEM_BUF_W SCREEN_W
#define EMBLEM_BUF_H SCREEN_H

BUILD_ASSERT(HIEROPHANT_W <= EMBLEM_BUF_W && HIEROPHANT_H <= EMBLEM_BUF_H,
             "hierophant emblem is larger than the panel");
BUILD_ASSERT(CIRCLECUBE_W <= EMBLEM_BUF_W && CIRCLECUBE_H <= EMBLEM_BUF_H,
             "circle-cube emblem is larger than the panel");

/*
 * Aligned because LVGL rounds a draw buffer's data pointer UP to
 * LV_DRAW_BUF_ALIGN; left to its own devices a uint8_t array can be offset by
 * up to three bytes, which silently costs the last rows of the canvas.
 */
static uint8_t emblem_buf[VISTA_CANVAS_BUF_SIZE(EMBLEM_BUF_W, EMBLEM_BUF_H)]
    __aligned(CONFIG_LV_DRAW_BUF_ALIGN);
static uint8_t mods_buf_left[VISTA_CANVAS_BUF_SIZE(VISTA_MOD_ROW_W, VISTA_MOD_ROW_H)];
static uint8_t mods_buf_right[VISTA_CANVAS_BUF_SIZE(VISTA_MOD_ROW_W, VISTA_MOD_ROW_H)];

/*
 * Scratch for rendering one corner readout before XORing it in.
 *
 * Full panel width so the label can be given the whole edge and asked to align
 * itself within it -- that is what puts the battery hard against the right
 * margin without measuring the string. Tall enough for the 18px face plus the
 * pixel the 3x3 dilation adds above and below.
 *
 * One buffer serves both corners: everything that draws here runs on the
 * display work queue, one readout at a time.
 */
#define TEXT_SCRATCH_W SCREEN_W
#define TEXT_SCRATCH_H (CORNER_H + 4)

static lv_obj_t *text_canvas;
static uint8_t text_buf[VISTA_CANVAS_BUF_SIZE(TEXT_SCRATCH_W, TEXT_SCRATCH_H)]
    __aligned(CONFIG_LV_DRAW_BUF_ALIGN);

/*
 * True while a leader sequence is being entered.
 *
 * The leader callback hides the emblem when a sequence starts, but the LAYER
 * callback runs on any layer change and would happily put it back -- and the
 * leader key is reached through a layer, so a change arrives in the middle of
 * every sequence. The result was the emblem drawn underneath the candidate
 * list. Whoever paints the middle band has to agree on who owns it, so the
 * layer callback asks this before showing anything.
 */
static bool leader_active;

/*
 * Faux-bold by dilation: the same text drawn at every offset in a 3x3
 * neighbourhood, so each stem grows a pixel in EVERY direction; the first
 * offset is the undisplaced copy.
 *
 * LVGL ships Montserrat in regular weight only -- there is no bold face to
 * switch to, and generating one offline would mean carrying a second full
 * glyph set. Overdrawing costs nothing in flash.
 *
 * Offsetting only right and down, as this did at first, thickens the stem by a
 * pixel but also shifts the whole word half a pixel off centre and reads as
 * blurred rather than bold. Going out in all directions keeps it centred and is
 * what actually looks heavier.
 *
 * Used by both the middle band, where it is drawn as nine stacked labels, and
 * the corner readouts, where it is nine passes over one scratch canvas.
 */
#define BOLD_COPIES 9

static const lv_coord_t bold_offsets[BOLD_COPIES][2] = {
    {0, 0},  {-1, -1}, {0, -1}, {1, -1}, {-1, 0},
    {1, 0},  {-1, 1},  {0, 1},  {1, 1},
};

static const struct emblem *current_emblem(void) {
    return &emblems[emblem_choice % ARRAY_SIZE(emblems)];
}

/*
 * Render one corner readout into the scratch canvas and XOR it into the panel.
 *
 * The label is given the full panel width and asked to align itself inside it,
 * which is what puts the battery hard against the right margin without
 * measuring the string first.
 *
 * Drawn nine times, once at every offset in a 3x3 neighbourhood, so each stem
 * grows a pixel in EVERY direction. That is the faux-bold the 1-bit panel
 * needs; offsetting only right and down thickens the stroke but drags the word
 * half a pixel off centre and reads as blurred rather than bold.
 */
static void xor_corner_text(const char *text, lv_text_align_t align) {
    if (text_canvas == NULL || text == NULL || text[0] == '\0') {
        return;
    }

    lv_canvas_fill_bg(text_canvas, VISTA_CANVAS_BACKGROUND, LV_OPA_COVER);

    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.color = VISTA_CANVAS_FOREGROUND;
    dsc.font = CORNER_FONT;
    dsc.align = align;
    dsc.text = text;

    lv_layer_t layer;
    lv_canvas_init_layer(text_canvas, &layer);
    for (size_t i = 0; i < BOLD_COPIES; i++) {
        const lv_coord_t dx = bold_offsets[i][0];
        const lv_coord_t dy = bold_offsets[i][1] + 1; /* room for the top row of the dilation */
        lv_area_t coords = {dx, dy, dx + TEXT_SCRATCH_W - 1, dy + TEXT_SCRATCH_H - 1};
        lv_draw_label(&layer, &dsc, &coords);
    }
    lv_canvas_finish_layer(text_canvas, &layer);

    vista_xor_canvas(emblem_canvas, 0, CORNER_LIFT, text_canvas);
}

/*
 * Repaint the whole panel canvas: background, the emblem if it is showing, and
 * the corner readouts XORed on top.
 *
 * It is all or nothing because the readouts are XORed rather than drawn over.
 * XOR has no undo -- flipping the same pixels twice restores them, but only if
 * nothing underneath moved in between -- so the only safe way to change any
 * part of this canvas is to rebuild it from the background up. That is cheap:
 * a fill, a blit and two short strings.
 *
 * Must run on the display work queue like anything else touching LVGL.
 */
static void panel_redraw(void) {
    if (emblem_canvas == NULL) {
        return;
    }

    lv_canvas_fill_bg(emblem_canvas, VISTA_CANVAS_BACKGROUND, LV_OPA_COVER);

    if (emblem_shown) {
        const struct emblem *e = current_emblem();
        const uint8_t *bits = e->frames[emblem_frame % e->frame_count];
        vista_draw_bitmap(emblem_canvas, (EMBLEM_BUF_W - e->w) / 2, (EMBLEM_BUF_H - e->h) / 2, bits,
                          e->w, e->h, e->stride);
    }

    xor_corner_text(conn_text, LV_TEXT_ALIGN_LEFT);
    if (batt_shown) {
        xor_corner_text(batt_text, LV_TEXT_ALIGN_RIGHT);
    }

    /*
     * The canvas holds a plain buffer, so LVGL has no idea its contents
     * changed; without this the panel keeps showing the previous frame until
     * something else happens to invalidate the same area.
     */
    lv_obj_invalidate(emblem_canvas);
}

/*
 * Step the emblem's animation.
 *
 * Rearmed only while an animated emblem is actually on screen -- a Sharp memory
 * LCD costs a line write per changed row, and there is nothing to be gained
 * from turning a cube nobody is looking at. emblem_show() starts and stops it.
 */
#define EMBLEM_FRAME_MS 900

static void emblem_animate_work(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(emblem_animate, emblem_animate_work);

/*
 * The animation runs ONLY while the keyboard is being used.
 *
 * Without this the timer rearms itself every EMBLEM_FRAME_MS for as long as the
 * base layer is showing -- which is almost always -- so an untouched keyboard
 * went on waking the CPU and writing all 144x168 pixels rather more than once a
 * second, indefinitely. A memory LCD holds its image with no help; repainting
 * an unchanged screen buys nothing and a turning cube nobody is looking at buys
 * less than that.
 *
 * Reading the state rather than latching it from the event keeps this correct
 * at construction too, when no activity event has been seen yet.
 */
static bool emblem_is_animated(void) {
    return emblem_shown && current_emblem()->frame_count > 1 &&
           zmk_activity_get_state() == ZMK_ACTIVITY_ACTIVE;
}

static void emblem_animation_sync(void) {
    if (emblem_is_animated()) {
        k_work_reschedule_for_queue(zmk_display_work_q(), &emblem_animate,
                                    K_MSEC(EMBLEM_FRAME_MS));
    } else {
        k_work_cancel_delayable(&emblem_animate);
    }
}

static void emblem_animate_work(struct k_work *work) {
    ARG_UNUSED(work);
    if (!emblem_is_animated()) {
        return;
    }
    emblem_frame = (emblem_frame + 1) % current_emblem()->frame_count;
    panel_redraw();
    emblem_animation_sync();
}

/*
 * Start and stop the animation with the keyboard's activity state.
 *
 * Going through ZMK_DISPLAY_WIDGET_LISTENER rather than a bare ZMK_LISTENER is
 * deliberate: it marshals the callback onto the display work queue, and waking
 * on ACTIVE repaints the panel, which must not happen from the event thread.
 */
struct rolio_activity_state {
    enum zmk_activity_state state;
};

static void rolio_activity_update_cb(struct rolio_activity_state state) {
    ARG_UNUSED(state);
    if (emblem_is_animated()) {
        /* Coming back: put the emblem on screen before the next frame is due. */
        panel_redraw();
    }
    emblem_animation_sync();
}

static struct rolio_activity_state rolio_activity_get_state(const zmk_event_t *eh) {
    ARG_UNUSED(eh);
    return (struct rolio_activity_state){.state = zmk_activity_get_state()};
}

ZMK_DISPLAY_WIDGET_LISTENER(rolio_activity_status, struct rolio_activity_state,
                            rolio_activity_update_cb, rolio_activity_get_state)
ZMK_SUBSCRIPTION(rolio_activity_status, zmk_activity_state_changed);

/* Show or hide the art, and start or stop the animation to match. */
static void emblem_show(bool shown) {
    if (emblem_shown == shown) {
        return;
    }
    emblem_shown = shown;
    /* Always begin on the same frame, so a swap looks deliberate. */
    emblem_frame = 0;
    panel_redraw();
    emblem_animation_sync();
}

/* Called from the toggle behaviour, already on the display queue. */
void vista_emblem_next(void) {
    emblem_choice = (emblem_choice + 1) % ARRAY_SIZE(emblems);
    emblem_frame = 0;
    panel_redraw();
    emblem_animation_sync();
}

/*
 * Caps lock, as the host reports it. Bit 1 of the HID keyboard LED report.
 *
 * Cached rather than polled: the indicators are stored per endpoint, so a
 * repaint landing while the endpoint switches reads an empty slot and would
 * decide caps is off when the host never said so.
 */
#define HID_LED_CAPS_LOCK BIT(1)

static uint8_t cached_indicators;

static bool host_caps_lock(void) { return (cached_indicators & HID_LED_CAPS_LOCK) != 0; }

/* The layer that gets an emblem instead of its name. */
#define EMBLEM_LAYER_NAME "hierophant"

/*
 * The battery readout is only shown on this layer.
 *
 * Both halves' charge is worth knowing occasionally and never worth a permanent
 * corner: it changes over hours, and the corner it occupied is the one the
 * emblem wants. SYSTEM is where the other rarely-needed things live and is
 * reached deliberately, so it is where to look when the question arises.
 */
#define BATTERY_LAYER_NAME "system"

/* Peripheral level arrives by event; there is no polling accessor for it. */
static uint8_t peripheral_soc;
static bool peripheral_seen;
/*
 * Local layer-name widget.
 *
 * ZMK's own layer_status widget formats into `char text[14]` as
 * LV_SYMBOL_KEYBOARD " %s". LV_SYMBOL_KEYBOARD is three UTF-8 bytes, so with
 * the space that leaves ten bytes for the name plus a terminator -- and
 * "hierophant" is exactly ten, so snprintf drops its last character. Any layer
 * name of ten or more characters loses its tail. This one has room to spare and
 * omits the symbol, which buys back four more columns.
 */
#define LAYER_NAME_MAX 32


/*
 * Faux-bold for the layer name: the same text drawn twice, one pixel apart, so
 * the strokes thicken. LVGL ships Montserrat in regular weight only -- there is
 * no bold face to switch to, and generating one offline would mean carrying a
 * second full glyph set for a single label. Overdrawing costs one more object
 * and nothing in flash.
 *
 * Both copies must always be set and shown together; layer_set_text() is the
 * only thing that should touch either.
 */
/*
 * A label drawn several times over, to fake a bold weight. LVGL ships
 * Montserrat in regular only, and generating a bold face offline would mean
 * carrying a second full glyph set for two labels.
 */

struct bold_label {
    lv_obj_t *copies[BOLD_COPIES];
};

static struct bold_label layer_bold;
static struct bold_label leader_bold;

/*
 * Faux-bold by dilation: the same text drawn at every offset in a 3x3
 * neighbourhood, so each stem grows a pixel in EVERY direction; the first
 * offset is the undisplaced copy.
 *
 * Offsetting only right and down, as this did at first, thickens the stem by a
 * pixel but also shifts the whole word half a pixel off centre and reads as
 * blurred rather than bold. Going out in all directions keeps it centred and is
 * what actually looks heavier.
 *
 * A larger face would be the other way to get weight, but the middle band is
 * only CONTENT_W wide and LV_LABEL_LONG_WRAP cannot break a single word:
 * "superscript" already fills the line at this size, so a bigger one would be
 * clipped rather than wrapped.
 */

static void bold_label_create(struct bold_label *bl, lv_obj_t *parent, lv_coord_t dy) {
    for (size_t i = 0; i < BOLD_COPIES; i++) {
        lv_obj_t *l = lv_label_create(parent);
        lv_obj_set_width(l, CONTENT_W);
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_align(l, LV_ALIGN_CENTER, bold_offsets[i][0], bold_offsets[i][1] + dy);
        lv_label_set_text(l, "");
        bl->copies[i] = l;
    }
}

static void bold_label_set_text(struct bold_label *bl, const char *text) {
    for (size_t i = 0; i < BOLD_COPIES; i++) {
        if (bl->copies[i] != NULL) {
            lv_label_set_text(bl->copies[i], text);
        }
    }
}

static void bold_label_set_hidden(struct bold_label *bl, bool hidden) {
    for (size_t i = 0; i < BOLD_COPIES; i++) {
        if (bl->copies[i] == NULL) {
            continue;
        }
        if (hidden) {
            lv_obj_add_flag(bl->copies[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(bl->copies[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static bool bold_label_ready(const struct bold_label *bl) { return bl->copies[0] != NULL; }

static void layer_set_text(const char *text) { bold_label_set_text(&layer_bold, text); }

static void layer_set_hidden(bool hidden) { bold_label_set_hidden(&layer_bold, hidden); }

struct rolio_layer_state {
    zmk_keymap_layer_index_t index;
    const char *label;
};

static void rolio_layer_update_cb(struct rolio_layer_state state) {
    if (!bold_label_ready(&layer_bold)) {
        return;
    }

    char name[LAYER_NAME_MAX];

    if (state.label == NULL || strlen(state.label) == 0) {
        snprintf(name, sizeof(name), "%i", state.index);
    } else {
        snprintf(name, sizeof(name), "%s", state.label);
    }

    /*
     * Caps lock never displaces what the band was going to say -- it goes on the
     * line above. On the base layer that costs only the emblem, which is
     * decorative; on every other layer the layer name stays and CAPS sits over
     * it. A modal state where every keystroke comes out wrong until you notice
     * earns the largest thing on the panel, not a corner glyph competing with
     * the battery reading.
     */
    bool caps = host_caps_lock();
    bool base = state.label != NULL && strcmp(state.label, EMBLEM_LAYER_NAME) == 0;

    /* Battery is a SYSTEM-layer readout; elsewhere the corner is the emblem's. */
    bool on_system = state.label != NULL && strcmp(state.label, BATTERY_LAYER_NAME) == 0;
    if (batt_shown != on_system) {
        batt_shown = on_system;
        panel_redraw();
    }

    char text[LAYER_NAME_MAX + 8];
    if (caps) {
        /* The base layer's name is never shown, so caps stands alone there. */
        snprintf(text, sizeof(text), base ? "CAPS" : "CAPS\n%s", name);
    } else {
        snprintf(text, sizeof(text), "%s", name);
    }

    layer_set_text(text);

    /*
     * The base layer is where the keyboard sits almost all the time, so it
     * shows the emblem rather than repeating a name that never changes. The
     * two share a band and are never both visible.
     */
    if (leader_active) {
        return; /* the leader list owns the band until the sequence ends */
    }

    /* Caps takes the band, so the emblem stands down while it is on. */
    bool emblem = base && !caps;
    emblem_show(emblem);
    layer_set_hidden(emblem);
}

static struct rolio_layer_state rolio_layer_get_state(const zmk_event_t *eh) {
#if IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)
    const struct zmk_hid_indicators_changed *ind = as_zmk_hid_indicators_changed(eh);
    if (ind != NULL) {
        cached_indicators = (uint8_t)ind->indicators;
    }
#endif
    zmk_keymap_layer_index_t index = zmk_keymap_highest_layer_active();
    return (struct rolio_layer_state){
        .index = index, .label = zmk_keymap_layer_name(zmk_keymap_layer_index_to_id(index))};
}

ZMK_DISPLAY_WIDGET_LISTENER(rolio_layer_status, struct rolio_layer_state, rolio_layer_update_cb,
                            rolio_layer_get_state)
ZMK_SUBSCRIPTION(rolio_layer_status, zmk_layer_state_changed);
#if IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)
/* Caps lock arrives from the host, so the band has to follow it too. */
ZMK_SUBSCRIPTION(rolio_layer_status, zmk_hid_indicators_changed);
#endif
#if IS_ENABLED(CONFIG_ZMK_WIDGET_WPM_STATUS)
static struct zmk_widget_wpm_status wpm_status_widget;
#endif

/* ------------------------------------------------------------------ */
/* Connection                                                          */
/* ------------------------------------------------------------------ */

struct rolio_conn_state {
    struct zmk_endpoint_instance selected;
    enum zmk_transport preferred;
    bool profile_connected;
    bool profile_bonded;
};

static void rolio_conn_update_cb(struct rolio_conn_state state) {
    char text[STATUS_MAX];
    enum zmk_transport transport = state.selected.transport;
    bool connected = transport != ZMK_TRANSPORT_NONE;

    /* When nothing is connected, show what it is reaching for instead. */
    if (!connected) {
        transport = state.preferred;
    }

    switch (transport) {
    case ZMK_TRANSPORT_USB:
        snprintf(text, sizeof(text), connected ? "USB" : "USB ...");
        break;
    case ZMK_TRANSPORT_BLE:
        if (!state.profile_bonded) {
            snprintf(text, sizeof(text), "BT%d unpaired",
                     zmk_ble_active_profile_index() + 1);
        } else {
            /*
             * Name the host where it can be named: "BT2 quignon" answers the
             * question "which machine am I typing into", where "BT2 ok" only
             * says that something is listening.
             *
             * The name replaces the state word rather than joining it. Seeing
             * a name at all means connected -- jjb_bt_name_for() reads the
             * profile's bonded address, and the corner is only this wide.
             * Falling back to "ok" keeps the old reading for a host that will
             * not say what it is called and has no table entry.
             */
            const int profile = zmk_ble_active_profile_index();
            const char *who = state.profile_connected ? jjb_bt_name_for((uint8_t)profile) : NULL;
            snprintf(text, sizeof(text), "BT%d %s", profile + 1,
                     who != NULL ? who : (state.profile_connected ? "ok" : "..."));
        }
        break;
    default:
        snprintf(text, sizeof(text), "offline");
        break;
    }

    if (strcmp(conn_text, text) != 0) {
        snprintf(conn_text, sizeof(conn_text), "%s", text);
        panel_redraw();
    }
}

static struct rolio_conn_state rolio_conn_get_state(const zmk_event_t *eh) {
    return (struct rolio_conn_state){
        .selected = zmk_endpoint_get_selected(),
        .preferred = zmk_endpoint_get_preferred_transport(),
        .profile_connected = zmk_ble_active_profile_is_connected(),
        .profile_bonded = !zmk_ble_active_profile_is_open(),
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(rolio_conn_status, struct rolio_conn_state, rolio_conn_update_cb,
                            rolio_conn_get_state)
ZMK_SUBSCRIPTION(rolio_conn_status, zmk_endpoint_changed);
ZMK_SUBSCRIPTION(rolio_conn_status, zmk_ble_active_profile_changed);

/* ------------------------------------------------------------------ */
/* Battery, both halves                                                */
/* ------------------------------------------------------------------ */

struct rolio_batt_state {
    uint8_t central;
    uint8_t peripheral;
    bool have_peripheral;
};

static void rolio_batt_update_cb(struct rolio_batt_state state) {
    char text[STATUS_MAX];

    /*
     * No percent signs: this now lives in a 60px corner beside the emblem's
     * spear, and "L88% R91%" overruns it. The two numbers are self-evidently
     * percentages.
     */
    if (state.have_peripheral) {
        snprintf(text, sizeof(text), "L%d R%d", state.central, state.peripheral);
    } else {
        snprintf(text, sizeof(text), "L%d", state.central);
    }

    if (strcmp(batt_text, text) != 0) {
        snprintf(batt_text, sizeof(batt_text), "%s", text);
        if (batt_shown) {
            panel_redraw();
        }
    }
}

static struct rolio_batt_state rolio_batt_get_state(const zmk_event_t *eh) {
    /*
     * The peripheral's level only ever arrives as an event, so latch it as it
     * goes past; there is no accessor to poll it back out of ZMK.
     */
    const struct zmk_peripheral_battery_state_changed *ev =
        as_zmk_peripheral_battery_state_changed(eh);
    if (ev != NULL) {
        peripheral_soc = ev->state_of_charge;
        peripheral_seen = true;
    }

    return (struct rolio_batt_state){
        .central = zmk_battery_state_of_charge(),
        .peripheral = peripheral_soc,
        .have_peripheral = peripheral_seen,
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(rolio_batt_status, struct rolio_batt_state, rolio_batt_update_cb,
                            rolio_batt_get_state)
ZMK_SUBSCRIPTION(rolio_batt_status, zmk_battery_state_changed);
ZMK_SUBSCRIPTION(rolio_batt_status, zmk_peripheral_battery_state_changed);

/* ------------------------------------------------------------------ */
/* Held modifiers                                                      */
/* ------------------------------------------------------------------ */

/*
 * Shows which modifiers are live, which is what makes a home-row mod that
 * fired when it should not actually visible.
 *
 * Driven off keycode events rather than a modifier event: ZMK declares a
 * zmk_modifiers_state_changed type but never raises it, so the flags are read
 * back out of HID state instead. Going through ZMK_DISPLAY_WIDGET_LISTENER
 * matters -- it marshals the callback onto the display work queue, and LVGL
 * must not be touched from the event thread.
 */
struct rolio_mods_state {
    zmk_mod_flags_t mods;
};

/*
 * Which glyph goes in which corner, and in what order within it.
 *
 * Bottom-left is GUI then ALT; bottom-right is SHIFT then CTRL. Splitting them
 * is what gives the emblem its lower middle back -- a single row of four held
 * roughly half the bottom edge, and the circle-cube, which runs the full width
 * of the panel, visibly lost the corner reserved for GUI.
 *
 * The order within each pair is jjb's, not derived from anything: read
 * outward-in on the left and inward-out on the right and it is GUI, ALT,
 * SHIFT, CTRL across the bottom.
 */
struct mod_slot {
    enum vista_mod_icon icon;
    zmk_mod_flags_t mods;
};

static const struct mod_slot mods_left[VISTA_MOD_GROUP_SLOTS] = {
    {VISTA_MOD_ICON_GUI, MOD_LGUI | MOD_RGUI},
    {VISTA_MOD_ICON_ALT, MOD_LALT | MOD_RALT},
};

static const struct mod_slot mods_right[VISTA_MOD_GROUP_SLOTS] = {
    {VISTA_MOD_ICON_SHIFT, MOD_LSFT | MOD_RSFT},
    {VISTA_MOD_ICON_CTRL, MOD_LCTL | MOD_RCTL},
};

static void mods_group_draw(lv_obj_t *canvas, const struct mod_slot *group,
                            zmk_mod_flags_t held) {
    if (canvas == NULL) {
        return;
    }

    lv_canvas_fill_bg(canvas, VISTA_CANVAS_BACKGROUND, LV_OPA_COVER);

    const lv_coord_t inset = (VISTA_MOD_ICON_SLOT_W - VISTA_MOD_ICON_SIZE) / 2;
    for (uint8_t slot = 0; slot < VISTA_MOD_GROUP_SLOTS; slot++) {
        if (held & group[slot].mods) {
            vista_draw_mod_icon(canvas, slot * VISTA_MOD_ICON_SLOT_W + inset, VISTA_MOD_ICON_PAD,
                                group[slot].icon);
        }
    }
}

static void rolio_mods_update_cb(struct rolio_mods_state state) {
    mods_group_draw(mods_canvas_left, mods_left, state.mods);
    mods_group_draw(mods_canvas_right, mods_right, state.mods);
}

static struct rolio_mods_state rolio_mods_get_state(const zmk_event_t *eh) {
    return (struct rolio_mods_state){.mods = zmk_hid_get_explicit_mods()};
}

ZMK_DISPLAY_WIDGET_LISTENER(rolio_mods_status, struct rolio_mods_state, rolio_mods_update_cb,
                            rolio_mods_get_state)
ZMK_SUBSCRIPTION(rolio_mods_status, zmk_keycode_state_changed);

/* ------------------------------------------------------------------ */
/* Leader sequence                                                     */
/* ------------------------------------------------------------------ */

/*
 * While a leader sequence is being entered, replace the layer name with what
 * is still reachable. With 116 sequences defined, listing them all the instant
 * leader is pressed would be noise, so nothing is listed until the candidate
 * set is small enough to be worth reading.
 */
#define LEADER_LIST_THRESHOLD 6
#define LEADER_TEXT_MAX       96
#define LEADER_NAME_MAX       32

static void rolio_leader_update_cb(struct zmk_leader_state_changed state) {
    if (!bold_label_ready(&leader_bold) || !bold_label_ready(&layer_bold)) {
        return;
    }

    if (!state.active) {
        /*
         * Hand the band back. Whether the layer name or the emblem should
         * reappear depends on the layer, so ask the layer widget rather than
         * guessing here -- and clear the flag first, or it will decline.
         */
        leader_active = false;
        bold_label_set_text(&leader_bold, "");
        rolio_layer_update_cb(rolio_layer_get_state(NULL));
        return;
    }

    leader_active = true;
    layer_set_hidden(true);
    emblem_show(false);

    char text[LEADER_TEXT_MAX];
    int used = snprintf(text, sizeof(text), "LEADER");

    if (state.candidate_count == 0) {
        snprintf(text, sizeof(text), "LEADER\nno match");
    } else if (state.candidate_count > LEADER_LIST_THRESHOLD) {
        snprintf(text, sizeof(text), "LEADER\n%d options", state.candidate_count);
    } else {
        for (uint8_t i = 0; i < state.candidate_count && used < (int)sizeof(text) - 1; i++) {
            const char *name = zmk_leader_candidate_name(i);
            if (name == NULL) {
                break;
            }
            /*
             * Names come from the devicetree node, so they arrive as
             * home_address_web. Underscores read badly at this width and give
             * the label nowhere to wrap.
             */
            char pretty[LEADER_NAME_MAX];
            snprintf(pretty, sizeof(pretty), "%s", name);
            vista_humanize(pretty);
            used += snprintf(text + used, sizeof(text) - used, "\n%s", pretty);
        }
    }

    bold_label_set_text(&leader_bold, text);
}

static struct zmk_leader_state_changed rolio_leader_get_state(const zmk_event_t *eh) {
    const struct zmk_leader_state_changed *ev = as_zmk_leader_state_changed(eh);
    if (ev != NULL) {
        return *ev;
    }
    return (struct zmk_leader_state_changed){0};
}

ZMK_DISPLAY_WIDGET_LISTENER(rolio_leader_status, struct zmk_leader_state_changed,
                            rolio_leader_update_cb, rolio_leader_get_state)
ZMK_SUBSCRIPTION(rolio_leader_status, zmk_leader_state_changed);

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);

    /*
     * Deliberately NOT lv_obj_remove_style_all() here. That strips the mono
     * theme's background along with the padding, leaving bg_opa transparent so
     * the labels render with no contrast and the panel comes out blank. Keep
     * the themed screen exactly as ZMK's built-in one does and adjust only the
     * widgets below.
     */
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    /*
     * Follow vista_canvas.h's light/dark switch. The mono theme paints the
     * screen light with dark text; overriding both here rather than in each
     * widget means the labels inherit it, including ZMK's own WPM widget, which
     * this file does not construct and so cannot style individually.
     *
     * Setting only the background would leave dark text on a dark screen.
     */
    lv_obj_set_style_bg_color(screen, VISTA_CANVAS_BACKGROUND, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(screen, VISTA_CANVAS_FOREGROUND, LV_PART_MAIN);

    /*
     * The emblem covers the whole panel, and the layer name and leader list
     * occupy the middle of it; exactly one of the three is visible at a time.
     * Only the narrow spear reaches the top and bottom edges, so the four
     * corners stay free for the status readouts -- see hierophant_img.h.
     *
     * Created FIRST, for two independent reasons. LVGL's z-order follows
     * creation order, and this is a full-panel object -- anything made before
     * it would be painted over, which is the whole corner layout. And
     * rolio_layer_status_init() runs the layer callback straight away, the
     * callback being what chooses between the emblem and the layer name; with
     * the canvas still NULL the base layer would show its name until the first
     * layer change.
     */
    emblem_canvas = lv_canvas_create(screen);
    lv_canvas_set_buffer(emblem_canvas, emblem_buf, EMBLEM_BUF_W, EMBLEM_BUF_H,
                         VISTA_CANVAS_COLOR_FORMAT);
    lv_obj_align(emblem_canvas, LV_ALIGN_TOP_MID, 0, 0);

    /*
     * The scratch the corner readouts are rendered into before being XORed in.
     * A canvas rather than a bare buffer because LVGL will only draw text
     * through a layer, and lv_canvas_init_layer() needs a canvas object.
     *
     * Never shown: it is a drawing surface, not part of the screen. It is still
     * parented to the screen because an LVGL object needs a parent to exist.
     */
    text_canvas = lv_canvas_create(screen);
    lv_canvas_set_buffer(text_canvas, text_buf, TEXT_SCRATCH_W, TEXT_SCRATCH_H,
                         VISTA_CANVAS_COLOR_FORMAT);
    lv_obj_add_flag(text_canvas, LV_OBJ_FLAG_HIDDEN);

    panel_redraw();

    rolio_conn_status_init();
    rolio_batt_status_init();

    /*
     * Layer name: the whole point of this screen. LV_LABEL_LONG_WRAP breaks on
     * word boundaries only, so a single long word like "hierophant" cannot wrap
     * and overflows instead -- at the default 20px font the trailing "t" was
     * clipped. Use the smaller face so the longest layer name fits on one line,
     * and keep WRAP as a fallback for multi-word names.
     */
    bold_label_create(&layer_bold, screen, 0);


    rolio_layer_status_init();

    /* Occupies the same middle band as the layer name; only one shows at a time. */
    bold_label_create(&leader_bold, screen, 0);
    rolio_leader_status_init();

    /*
     * Modifier glyphs, a pair in each bottom corner. The emblem takes the whole
     * middle band, so there is no room for a line of their own; two pairs leave
     * the middle of the bottom edge to the art instead of one row of four
     * taking half of it.
     *
     * Both are lifted off the bottom edge: sitting flush, the glyphs' lowest
     * stroke merged into the panel border and they read as clipped.
     */
    mods_canvas_left = lv_canvas_create(screen);
    lv_canvas_set_buffer(mods_canvas_left, mods_buf_left, VISTA_MOD_ROW_W, VISTA_MOD_ROW_H,
                         VISTA_CANVAS_COLOR_FORMAT);
    lv_canvas_fill_bg(mods_canvas_left, VISTA_CANVAS_BACKGROUND, LV_OPA_COVER);
    lv_obj_align(mods_canvas_left, LV_ALIGN_BOTTOM_LEFT, 0, -MODS_LIFT);

    mods_canvas_right = lv_canvas_create(screen);
    lv_canvas_set_buffer(mods_canvas_right, mods_buf_right, VISTA_MOD_ROW_W, VISTA_MOD_ROW_H,
                         VISTA_CANVAS_COLOR_FORMAT);
    lv_canvas_fill_bg(mods_canvas_right, VISTA_CANVAS_BACKGROUND, LV_OPA_COVER);
    lv_obj_align(mods_canvas_right, LV_ALIGN_BOTTOM_RIGHT, 0, -MODS_LIFT);

    rolio_mods_status_init();
    rolio_activity_status_init();

    /*
     * WPM along the bottom.
     *
     * Currently OFF -- CONFIG_ZMK_WIDGET_WPM_STATUS=n in vista508.conf. The
     * layout is kept so turning the symbol back on restores it, but two things
     * have moved underneath it since: the bottom-right corner now belongs to
     * the SHIFT/CTRL glyph pair, and this is still a plain label drawn OVER the
     * canvas rather than XORed into it, so it will mask whatever art is beneath
     * exactly as the corner readouts used to. Both want addressing before it
     * goes back on.
     */
#if IS_ENABLED(CONFIG_ZMK_WIDGET_WPM_STATUS)
    zmk_widget_wpm_status_init(&wpm_status_widget, screen);
    lv_obj_t *wpm = zmk_widget_wpm_status_obj(&wpm_status_widget);
    lv_obj_set_width(wpm, LV_SIZE_CONTENT);
    lv_obj_set_style_text_font(wpm, CORNER_FONT, LV_PART_MAIN);
    lv_obj_set_style_text_align(wpm, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_align(wpm, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
#endif

    /*
     * Say the stacking outright instead of inferring it from creation order.
     * Everything else is a corner readout or the middle band; the emblem is the
     * only full-panel object and belongs beneath all of them.
     */
    lv_obj_move_background(emblem_canvas);

    return screen;
}
