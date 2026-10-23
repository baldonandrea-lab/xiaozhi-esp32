// Display "solo occhi" per la mascotte: sostituisce l'interfaccia testuale dell'OLED.
// Mostra il testo SOLO quando serve davvero (configurazione Wi-Fi, codice di
// attivazione, errori, aggiornamento firmware, notifiche come il volume).
#pragma once

#include "display/oled_display.h"
#include "eyes_engine.h"

#include <string>

class EyesDisplay : public OledDisplay {
public:
    EyesDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
                int height, bool mirror_x, bool mirror_y);
    ~EyesDisplay();

    void SetupUI() override;
    void SetChatMessage(const char* role, const char* content) override;
    void RemoveChatMessage(const char* role, const char* content) override;
    bool SetEmotion(const char* emotion) override;
    void SetStatus(const char* status) override;
    void ShowNotification(const char* notification, int duration_ms = 3000) override;
    void ShowNotification(const std::string& notification, int duration_ms = 3000) override;
    void UpdateStatusBar(bool update_all = false) override;

private:
    static void StateTimerCb(lv_timer_t* t);
    static void NotificationTimerCb(lv_timer_t* t);
    void ShowText(const std::string& text);
    void RefreshMode();

    EyesEngine eyes_;
    lv_obj_t* text_box_ = nullptr;
    lv_obj_t* text_label_ = nullptr;
    lv_timer_t* state_timer_ = nullptr;
    lv_timer_t* notification_timer_ = nullptr;
    std::string system_text_;
    std::string notification_text_;
};
