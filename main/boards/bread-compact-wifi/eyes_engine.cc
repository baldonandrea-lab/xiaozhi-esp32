#include "eyes_engine.h"

#include <cmath>
#include <cstring>

// Su SSD1306 con esp_lvgl_port il "nero" accende il pixel (come nel display originale).
// Se vedi gli occhi in negativo, scambia questi due colori.
#ifndef EYES_COLOR_ON
#define EYES_COLOR_ON lv_color_black()
#endif
#ifndef EYES_COLOR_OFF
#define EYES_COLOR_OFF lv_color_white()
#endif

namespace {
constexpr float kPi = 3.14159265f;
constexpr uint32_t kFrameMs = 66;      // ~15 fps: meno traffico I2C e meno carico
                                       // (a 30 fps disturbava l'acquisizione audio)
constexpr uint32_t kBlinkMs = 170;     // durata ammiccamento

inline int32_t Q(float v) { return static_cast<int32_t>(lroundf(v)); }

float Approach(float cur, float target, float dt_ms, float tau_ms) {
    float a = 1.0f - std::exp(-dt_ms / tau_ms);
    return cur + (target - cur) * a;
}
}  // namespace

EyesEngine::~EyesEngine() {
    if (timer_ != nullptr) {
        lv_timer_delete(timer_);
        timer_ = nullptr;
    }
    if (obj_ != nullptr) {
        lv_obj_delete(obj_);
        obj_ = nullptr;
    }
}

EyesEngine::Shape EyesEngine::ShapeFor(const char* name, bool* known) {
    //            w   h   r   gx  gy  lidIn lidOut happy  sL   sR   oL   oR
    Shape s = {30, 36, 10, 0, 0, 0, 0, 0, 1, 1, 1, 1};
    bool ok = true;
    auto is = [&](const char* n) { return name != nullptr && std::strcmp(name, n) == 0; };

    if (name == nullptr || is("neutral") || is("robot_2") || is("gear")) {
        // occhi normali
    } else if (is("happy") || is("loving") || is("confident") || is("delicious") || is("kissy")) {
        s.happy = 0.55f;
    } else if (is("laughing") || is("funny") || is("silly")) {
        s.happy = 0.68f; s.w = 32;
    } else if (is("sad") || is("crying")) {
        s.lid_out = 0.48f; s.lid_in = 0.08f; s.gy = 5; s.h = 32;
    } else if (is("angry") || is("warning")) {
        s.lid_in = 0.52f; s.lid_out = 0.08f; s.w = 32;
    } else if (is("surprised") || is("shocked")) {
        s.w = 32; s.h = 44; s.r = 16;
    } else if (is("thinking")) {
        s.gx = 10; s.gy = -7; s.h = 32; s.lid_in = 0.18f; s.lid_out = 0.18f;
    } else if (is("sleepy") || is("relaxed")) {
        s.lid_in = 0.58f; s.lid_out = 0.58f; s.gy = 4;
    } else if (is("embarrassed")) {
        s.happy = 0.35f; s.gx = -8; s.gy = 5; s.h = 30;
    } else if (is("winking")) {
        s.happy = 0.4f; s.open_r = 0.08f;
    } else if (is("cool")) {
        s.lid_in = 0.42f; s.lid_out = 0.42f; s.w = 34; s.r = 6;
    } else if (is("confused")) {
        s.scale_l = 0.78f; s.scale_r = 1.08f; s.lid_in = 0.15f;
    } else {
        ok = false;
    }
    if (known != nullptr) *known = ok;
    return s;
}

void EyesEngine::Create(lv_obj_t* parent, int width, int height) {
    width_ = width;
    height_ = height;
    k_ = std::fmin(width / 128.0f, height / 64.0f);

    obj_ = lv_obj_create(parent);
    lv_obj_remove_style_all(obj_);
    lv_obj_set_size(obj_, width, height);
    lv_obj_set_pos(obj_, 0, 0);
    lv_obj_set_style_bg_color(obj_, EYES_COLOR_OFF, 0);
    lv_obj_set_style_bg_opa(obj_, LV_OPA_COVER, 0);
    lv_obj_remove_flag(obj_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(obj_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(obj_, DrawCb, LV_EVENT_DRAW_MAIN_END, this);

    target_ = cur_ = draw_ = ShapeFor("neutral", nullptr);
    t0_ = last_ms_ = lv_tick_get();
    next_blink_ = t0_ + 1500;
    next_saccade_ = t0_ + 2500;
    timer_ = lv_timer_create(TimerCb, kFrameMs, this);
}

bool EyesEngine::SetExpression(const char* name) {
    bool known = false;
    Shape s = ShapeFor(name, &known);
    target_ = s;
    sleeping_ = (name != nullptr && std::strcmp(name, "sleepy") == 0);
    return known;
}

void EyesEngine::SetActivity(Activity activity) { activity_ = activity; }

void EyesEngine::SetVisible(bool visible) {
    if (obj_ == nullptr) return;
    if (visible) {
        lv_obj_remove_flag(obj_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(obj_, LV_OBJ_FLAG_HIDDEN);
    }
}

uint32_t EyesEngine::Rand() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return rng_;
}

void EyesEngine::TimerCb(lv_timer_t* t) {
    auto* self = static_cast<EyesEngine*>(lv_timer_get_user_data(t));
    self->Tick(lv_tick_get());
}

void EyesEngine::Tick(uint32_t now) {
    float dt = static_cast<float>(now - last_ms_);
    if (dt <= 0) dt = 1;
    last_ms_ = now;
    float t = (now - t0_) / 1000.0f;

    // 1) bersaglio = espressione + comportamento dell'attività
    Shape goal = target_;
    float pulse = 1.0f, bounce = 0.0f;
    switch (activity_) {
        case Activity::Listening:   // occhi più grandi e attenti, FERMI:
            // mentre ascolta il display non deve ridisegnare di continuo
            goal.h *= 1.12f; goal.w *= 1.05f; goal.gy -= 1;
            break;
        case Activity::Speaking:    // piccolo rimbalzo mentre parla (lento)
            bounce = 1.5f * std::sin(t * 2 * kPi * 1.5f);
            break;
        case Activity::Connecting:  // sguardo che "cerca"
            goal.gx = 11.0f * std::sin(t * 2 * kPi * 0.5f);
            goal.lid_in = std::fmax(goal.lid_in, 0.15f);
            goal.lid_out = std::fmax(goal.lid_out, 0.15f);
            break;
        case Activity::Idle:
        default:
            break;
    }

    // 2) sguardi casuali quando è a riposo
    if (activity_ == Activity::Idle && !sleeping_ && target_.gx == 0 && target_.gy == 0) {
        if (now >= next_saccade_) {
            uint32_t r = Rand();
            if (r % 3 == 0) {
                sacc_x_ = 0; sacc_y_ = 0;          // torna al centro
            } else {
                sacc_x_ = static_cast<float>(static_cast<int>(Rand() % 21) - 10);
                sacc_y_ = static_cast<float>(static_cast<int>(Rand() % 10) - 5);
            }
            next_saccade_ = now + 1500 + Rand() % 3000;
        }
        goal.gx += sacc_x_;
        goal.gy += sacc_y_;
    } else {
        sacc_x_ = sacc_y_ = 0;
    }

    // 3) transizione morbida verso il bersaglio
    const float tau = 70.0f, tau_gaze = 55.0f;
    cur_.w = Approach(cur_.w, goal.w, dt, tau);
    cur_.h = Approach(cur_.h, goal.h, dt, tau);
    cur_.r = Approach(cur_.r, goal.r, dt, tau);
    cur_.gx = Approach(cur_.gx, goal.gx, dt, tau_gaze);
    cur_.gy = Approach(cur_.gy, goal.gy, dt, tau_gaze);
    cur_.lid_in = Approach(cur_.lid_in, goal.lid_in, dt, tau);
    cur_.lid_out = Approach(cur_.lid_out, goal.lid_out, dt, tau);
    cur_.happy = Approach(cur_.happy, goal.happy, dt, tau);
    cur_.scale_l = Approach(cur_.scale_l, goal.scale_l, dt, tau);
    cur_.scale_r = Approach(cur_.scale_r, goal.scale_r, dt, tau);
    cur_.open_l = Approach(cur_.open_l, goal.open_l, dt, 50.0f);
    cur_.open_r = Approach(cur_.open_r, goal.open_r, dt, 50.0f);

    // 4) ammiccamento (anche doppio); quando dorme respira lentamente
    float blink = 1.0f;
    if (sleeping_) {
        blink = 0.85f + 0.15f * std::sin(t * 2 * kPi * 0.25f);
    } else {
        if (!blinking_ && now >= next_blink_ && activity_ != Activity::Listening) {
            blinking_ = true;
            blink_start_ = now;
            double_blink_ = (Rand() % 5 == 0);
        }
        if (blinking_) {
            uint32_t e = now - blink_start_;
            uint32_t total = double_blink_ ? kBlinkMs * 2 + 60 : kBlinkMs;
            uint32_t phase = e;
            if (double_blink_ && e > kBlinkMs + 60) phase = e - kBlinkMs - 60;
            else if (double_blink_ && e > kBlinkMs) phase = kBlinkMs;  // pausa tra i due
            float p = std::fmin(1.0f, phase / static_cast<float>(kBlinkMs));
            blink = 0.1f + 0.9f * std::fabs(std::cos(p * kPi));
            if (e >= total) {
                blinking_ = false;
                next_blink_ = now + 2200 + Rand() % 4000;
            }
        }
    }

    draw_ = cur_;
    draw_.w *= pulse;
    draw_.h *= pulse;
    draw_.gy += bounce;
    draw_.open_l *= blink;
    draw_.open_r *= blink;

    // 5) ridisegna solo se cambia qualcosa di visibile (risparmia I2C)
    int32_t sig[12] = {
        Q(draw_.w * k_), Q(draw_.h * k_), Q(draw_.r * k_), Q(draw_.gx * k_), Q(draw_.gy * k_),
        Q(draw_.lid_in * 40), Q(draw_.lid_out * 40), Q(draw_.happy * 40),
        Q(draw_.scale_l * 40), Q(draw_.scale_r * 40), Q(draw_.open_l * 40), Q(draw_.open_r * 40)};
    if (obj_ != nullptr && std::memcmp(sig, drawn_sig_, sizeof(sig)) != 0) {
        std::memcpy(drawn_sig_, sig, sizeof(sig));
        // ridisegna solo il rettangolo che contiene gli occhi (vecchia + nuova posizione)
        lv_area_t area = EyesArea();
        lv_area_t dirty = area;
        if (has_last_area_) {
            dirty.x1 = LV_MIN(dirty.x1, last_area_.x1);
            dirty.y1 = LV_MIN(dirty.y1, last_area_.y1);
            dirty.x2 = LV_MAX(dirty.x2, last_area_.x2);
            dirty.y2 = LV_MAX(dirty.y2, last_area_.y2);
        }
        last_area_ = area;
        has_last_area_ = true;
        lv_obj_invalidate_area(obj_, &dirty);
    }
}

lv_area_t EyesEngine::EyesArea() const {
    // Rettangolo (coordinate schermo) che contiene i due occhi con un margine:
    // le palpebre sporgono di 2 px, il taglio "felice" del 12% in larghezza.
    const Shape& s = draw_;
    float smax = std::fmax(s.scale_l, s.scale_r);
    float w = s.w * smax * k_ * 1.25f;
    float h = s.h * smax * k_;
    float spacing = 24.0f * k_;
    float cx = width_ / 2.0f + s.gx * k_;
    float cy = height_ / 2.0f + s.gy * k_;
    lv_area_t a;
    a.x1 = static_cast<int32_t>(cx - spacing - w / 2) - 4;
    a.x2 = static_cast<int32_t>(cx + spacing + w / 2) + 4;
    a.y1 = static_cast<int32_t>(cy - h / 2) - 4;
    a.y2 = static_cast<int32_t>(cy + h / 2) + 4;
    if (a.x1 < 0) a.x1 = 0;
    if (a.y1 < 0) a.y1 = 0;
    if (a.x2 > width_ - 1) a.x2 = width_ - 1;
    if (a.y2 > height_ - 1) a.y2 = height_ - 1;
    return a;
}

void EyesEngine::DrawCb(lv_event_t* e) {
    auto* self = static_cast<EyesEngine*>(lv_event_get_user_data(e));
    lv_obj_t* obj = static_cast<lv_obj_t*>(lv_event_get_target(e));
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    self->Draw(lv_event_get_layer(e), &a);
}

void EyesEngine::Draw(lv_layer_t* layer, const lv_area_t* a) {
    float spacing = 24.0f;                      // metà distanza tra i centri (a 128x64)
    float mid = width_ / 2.0f;
    DrawEye(layer, a, mid - spacing * k_, true);
    DrawEye(layer, a, mid + spacing * k_, false);
}

void EyesEngine::DrawEye(lv_layer_t* layer, const lv_area_t* a, float cx_screen, bool left) {
    const Shape& s = draw_;
    float scale = left ? s.scale_l : s.scale_r;
    float open = left ? s.open_l : s.open_r;

    float w = s.w * scale * k_;
    float h_full = s.h * scale * k_;
    float h = std::fmax(2.0f * k_ + 1.0f, h_full * open);
    float cx = a->x1 + cx_screen + s.gx * k_;
    float cy = a->y1 + height_ / 2.0f + s.gy * k_;
    // da chiuso l'occhio "scende" verso la palpebra inferiore, come un vero battito
    cy += (h_full - h) * 0.25f;

    lv_area_t eye;
    eye.x1 = lroundf(cx - w / 2);
    eye.x2 = lroundf(cx + w / 2) - 1;
    eye.y1 = lroundf(cy - h / 2);
    eye.y2 = lroundf(cy + h / 2) - 1;

    lv_draw_rect_dsc_t rd;
    lv_draw_rect_dsc_init(&rd);
    rd.bg_color = EYES_COLOR_ON;
    rd.bg_opa = LV_OPA_COVER;
    rd.radius = static_cast<int32_t>(std::fmin(s.r * scale * k_, std::fmin(w, h) / 2));
    lv_draw_rect(layer, &rd, &eye);

    // palpebra superiore inclinata (copre con il colore di sfondo)
    if (s.lid_in > 0.01f || s.lid_out > 0.01f) {
        float y_in = eye.y1 + s.lid_in * h;
        float y_out = eye.y1 + s.lid_out * h;
        float x_in = left ? eye.x2 + 2 : eye.x1 - 2;
        float x_out = left ? eye.x1 - 2 : eye.x2 + 2;
        float top = eye.y1 - 2;

        lv_draw_triangle_dsc_t td;
        lv_draw_triangle_dsc_init(&td);
        td.color = EYES_COLOR_OFF;
        td.opa = LV_OPA_COVER;
        td.p[0] = {static_cast<lv_value_precise_t>(x_out), static_cast<lv_value_precise_t>(top)};
        td.p[1] = {static_cast<lv_value_precise_t>(x_in), static_cast<lv_value_precise_t>(top)};
        td.p[2] = {static_cast<lv_value_precise_t>(x_in), static_cast<lv_value_precise_t>(y_in)};
        lv_draw_triangle(layer, &td);
        td.p[0] = {static_cast<lv_value_precise_t>(x_out), static_cast<lv_value_precise_t>(top)};
        td.p[1] = {static_cast<lv_value_precise_t>(x_in), static_cast<lv_value_precise_t>(y_in)};
        td.p[2] = {static_cast<lv_value_precise_t>(x_out), static_cast<lv_value_precise_t>(y_out)};
        lv_draw_triangle(layer, &td);
    }

    // occhi "felici": un'ellisse di sfondo taglia la parte bassa -> forma ad arco
    if (s.happy > 0.02f) {
        float cut_top = eye.y2 - s.happy * h;
        lv_area_t cut;
        cut.x1 = lroundf(cx - w * 0.62f);
        cut.x2 = lroundf(cx + w * 0.62f);
        cut.y1 = lroundf(cut_top);
        cut.y2 = lroundf(cut_top + h * 1.1f);
        lv_draw_rect_dsc_t cd;
        lv_draw_rect_dsc_init(&cd);
        cd.bg_color = EYES_COLOR_OFF;
        cd.bg_opa = LV_OPA_COVER;
        cd.radius = LV_RADIUS_CIRCLE;
        lv_draw_rect(layer, &cd, &cut);
    }
}
