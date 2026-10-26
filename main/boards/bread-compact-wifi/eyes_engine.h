// Occhi animati per OLED monocromatico (LVGL 9) - mascotte Xiaozhi
// Disegna due occhi con palpebre, sguardo, ammiccamenti ed espressioni.
// Non dipende dal resto del firmware: si usa da EyesDisplay.
#pragma once

#include <lvgl.h>
#include <cstdint>

class EyesEngine {
public:
    enum class Activity { Idle, Connecting, Listening, Speaking };

    EyesEngine() = default;
    ~EyesEngine();

    // Crea l'oggetto a tutto schermo e avvia l'animazione (chiamare con LVGL bloccato).
    void Create(lv_obj_t* parent, int width, int height);
    // Nome emozione di Xiaozhi ("happy", "sad", "thinking"...). false se sconosciuta.
    bool SetExpression(const char* name);
    void SetActivity(Activity activity);
    void SetVisible(bool visible);
    // Avanza l'animazione: chiamato dal timer LVGL (pubblico per i test).
    void Tick(uint32_t now_ms);

private:
    struct Shape {
        float w, h, r;          // dimensioni occhio (px a 128x64)
        float gx, gy;           // sguardo
        float lid_in, lid_out;  // palpebra superiore lato naso / lato esterno (0..1)
        float happy;            // taglio inferiore a "sorriso" (0..1)
        float scale_l, scale_r; // dimensione relativa occhio sinistro / destro
        float open_l, open_r;   // apertura (1 = aperto)
    };

    static Shape ShapeFor(const char* name, bool* known);
    static void TimerCb(lv_timer_t* t);
    static void DrawCb(lv_event_t* e);
    void Draw(lv_layer_t* layer, const lv_area_t* a);
    void DrawEye(lv_layer_t* layer, const lv_area_t* a, float cx, bool left);
    uint32_t Rand();
    lv_area_t EyesArea() const;

    lv_obj_t* obj_ = nullptr;
    lv_timer_t* timer_ = nullptr;
    int width_ = 128, height_ = 64;
    float k_ = 1.0f;                       // scala per display 128x32

    Shape target_{}, cur_{}, draw_{};
    Activity activity_ = Activity::Idle;
    bool sleeping_ = false;

    uint32_t last_ms_ = 0, t0_ = 0;
    uint32_t next_blink_ = 0, blink_start_ = 0;
    bool blinking_ = false, double_blink_ = false;
    uint32_t next_saccade_ = 0;
    float sacc_x_ = 0, sacc_y_ = 0;
    uint32_t rng_ = 0x1234567u;
    int32_t drawn_sig_[12] = {0};
    lv_area_t last_area_{};
    bool has_last_area_ = false;
};
