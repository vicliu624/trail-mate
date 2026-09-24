#pragma once

#include "ui/screens/agenda/agenda_page_shell.h"

namespace ui::agenda::page
{
// UI navigation lifetime only. No store, scheduler, tile or GPS ownership.
// A bounded return context exists during a Map visit or app interruption.
class Flow
{
  public:
    explicit Flow(Host& host);
    ~Flow();
    Flow(const Flow&) = delete;
    Flow& operator=(const Flow&) = delete;
    static void enter(void* context, lv_obj_t* parent);
    static void exit(void* context, lv_obj_t* parent);
    void tick();
    bool mapActive() const { return return_ != nullptr; }
    // Prepare without destroying widgets; commit only after reminder dismissal.
    // nullptr means prepared, otherwise a localization key describes failure.
    const char* prepareDestination(int32_t latitude_e7, int32_t longitude_e7);
    void finishDestination(bool accepted);
    bool needsActivation() const;
    // Composition calls this when the shell chose not to resume Calendar.
    void discardSuspended();
    // Called outside widget callbacks after the source becomes unavailable.
    // End this media session, including any Map or suspended editor context.
    void invalidateStorage();

  private:
    struct ReturnContext;
    static bool requestMap(void*, const AgendaEditorModel&, bool return_detail);
    static bool requestTarget(void*, const AgendaEditorModel&);
    bool queueMap(const AgendaEditorModel&, bool return_detail, bool show_target);
    ReturnContext* captureReturn();
    static void requestReturn(void*);
    void close();
    void suspend();
    void restoreEditor();
    Host& host_;
    lv_obj_t* parent_ = nullptr;
    ReturnContext* return_ = nullptr;
    ReturnContext* reserve_ = nullptr; // Allocated before editing, not during interruption.
};
} // namespace ui::agenda::page
