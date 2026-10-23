#include "eyes_display.h"

#include "application.h"
#include "system_info.h"
#include "lvgl_font.h"
#include "lvgl_theme.h"

#include <esp_log.h>

#define TAG "EyesDisplay"

EyesDisplay::EyesDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                         int width, int height, bool mirror_x, bool mirror_y)
    : OledDisplay(panel_io, panel, width, height, mirror_x, mirror_y) {}

EyesDisplay::~EyesDisplay() {
    DisplayLockGuard lock(this);
    if (state_timer_ != nullptr) lv_timer_delete(state_timer_);
    if (notification_timer_ != nullptr) lv_timer_delete(notification_timer_);
    if (text_box_ != nullptr) lv_obj_delete(text_box_);
}

void EyesDisplay::SetupUI() {
    if (setup_ui_called_) {
        return;
    }
    Display::SetupUI();  // segna come inizializzato (non costruisce la UI testuale originale)

    DisplayLockGuard lock(this);
    auto screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    eyes_.Create(screen, width_, height_);

    // Riquadro di testo per i messaggi di sistema (nascosto di solito)
    auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    text_box_ = lv_obj_create(screen);
    lv_obj_remove_style_all(text_box_);
    lv_obj_set_size(text_box_, width_, height_);
    lv_obj_set_style_bg_color(text_box_, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(text_box_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(text_box_, 2, 0);
    lv_obj_set_scrollbar_mode(text_box_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(text_box_, LV_OBJ_FLAG_HIDDEN);

    text_label_ = lv_label_create(text_box_);
    lv_obj_set_width(text_label_, width_ - 4);
    lv_obj_set_style_text_font(text_label_, lvgl_theme->text_font()->font(), 0);
    lv_obj_set_style_text_color(text_label_, lv_color_black(), 0);
    lv_obj_set_style_text_align(text_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(text_label_, LV_LABEL_LONG_MODE_WRAP);
    lv_label_set_text(text_label_, "");

    // Legge lo stato del dispositivo e anima gli occhi di conseguenza
    state_timer_ = lv_timer_create(StateTimerCb, 100, this);
}

void EyesDisplay::StateTimerCb(lv_timer_t* t) {
    auto* self = static_cast<EyesDisplay*>(lv_timer_get_user_data(t));
    using A = EyesEngine::Activity;
    switch (Application::GetInstance().GetDeviceState()) {
        case kDeviceStateListening: self->eyes_.SetActivity(A::Listening); break;
        case kDeviceStateSpeaking:  self->eyes_.SetActivity(A::Speaking); break;
        case kDeviceStateStarting:
        case kDeviceStateConnecting:
        case kDeviceStateActivating:
        case kDeviceStateUpgrading: self->eyes_.SetActivity(A::Connecting); break;
        default:                    self->eyes_.SetActivity(A::Idle); break;
    }
}

void EyesDisplay::NotificationTimerCb(lv_timer_t* t) {
    auto* self = static_cast<EyesDisplay*>(lv_timer_get_user_data(t));
    self->notification_text_.clear();
    self->notification_timer_ = nullptr;
    lv_timer_delete(t);
    self->RefreshMode();
}

void EyesDisplay::ShowText(const std::string& text) {
    lv_label_set_text(text_label_, text.c_str());
    lv_obj_update_layout(text_box_);
    // centra in verticale; se il testo è più lungo dello schermo scorre su e giù
    int32_t label_h = lv_obj_get_height(text_label_);
    int32_t free_h = height_ - 4 - label_h;
    lv_anim_delete(text_label_, nullptr);
    if (free_h >= 0) {
        lv_obj_set_y(text_label_, free_h / 2);
    } else {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, text_label_);
        lv_anim_set_exec_cb(&a, [](void* obj, int32_t v) { lv_obj_set_y(static_cast<lv_obj_t*>(obj), v); });
        lv_anim_set_values(&a, 0, free_h);
        lv_anim_set_duration(&a, static_cast<uint32_t>(-free_h) * 60 + 600);
        lv_anim_set_delay(&a, 1500);
        lv_anim_set_reverse_delay(&a, 1500);
        lv_anim_set_reverse_duration(&a, static_cast<uint32_t>(-free_h) * 60 + 600);
        lv_anim_set_repeat_delay(&a, 1500);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_start(&a);
    }
}

// Testo visibile: notifiche temporanee, oppure messaggi di sistema fuori dalla conversazione
void EyesDisplay::RefreshMode() {
    if (text_box_ == nullptr) return;
    const std::string& text = !notification_text_.empty() ? notification_text_ : system_text_;
    if (text.empty()) {
        lv_anim_delete(text_label_, nullptr);
        lv_obj_add_flag(text_box_, LV_OBJ_FLAG_HIDDEN);
        eyes_.SetVisible(true);
    } else {
        ShowText(text);
        lv_obj_remove_flag(text_box_, LV_OBJ_FLAG_HIDDEN);
        eyes_.SetVisible(false);
    }
}

void EyesDisplay::SetChatMessage(const char* role, const char* content) {
    DisplayLockGuard lock(this);
    // Le frasi della conversazione (utente/assistente) non vengono mostrate: solo occhi.
    if (role == nullptr || std::string(role) != "system") {
        return;
    }
    system_text_ = (content != nullptr) ? content : "";
    // all'avvio Xiaozhi mostra la versione del firmware: la saltiamo, restano gli occhi
    if (system_text_ == SystemInfo::GetUserAgent()) {
        system_text_.clear();
    }
    RefreshMode();
}

void EyesDisplay::RemoveChatMessage(const char* role, const char* content) {
    DisplayLockGuard lock(this);
    if (role != nullptr && std::string(role) == "system" && content != nullptr &&
        system_text_ == content) {
        system_text_.clear();
        RefreshMode();
    }
}

bool EyesDisplay::SetEmotion(const char* emotion) {
    DisplayLockGuard lock(this);
    bool known = eyes_.SetExpression(emotion);
    if (!known) {
        ESP_LOGI(TAG, "Emozione '%s' senza occhi dedicati: uso neutral", emotion ? emotion : "");
    }
    return known;
}

void EyesDisplay::SetStatus(const char* status) {
    // Niente barra di stato: lo stato si vede dal comportamento degli occhi.
    (void)status;
}

void EyesDisplay::ShowNotification(const char* notification, int duration_ms) {
    ShowNotification(std::string(notification != nullptr ? notification : ""), duration_ms);
}

void EyesDisplay::ShowNotification(const std::string& notification, int duration_ms) {
    DisplayLockGuard lock(this);
    if (text_box_ == nullptr) return;
    notification_text_ = notification;
    if (notification_timer_ != nullptr) {
        lv_timer_delete(notification_timer_);
        notification_timer_ = nullptr;
    }
    if (!notification_text_.empty()) {
        notification_timer_ = lv_timer_create(NotificationTimerCb, duration_ms, this);
    }
    RefreshMode();
}

void EyesDisplay::UpdateStatusBar(bool update_all) {
    // Nessuna icona Wi-Fi/batteria/muto da aggiornare.
    (void)update_all;
}
