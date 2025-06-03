#pragma once

#include <iostream>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include "event_manager.h"

class EventHandler
{

public:
    typedef std::function<bool(const BaseEvent*)> EventFunction;

    static std::map<std::string, EventHandler::EventFunction> eventMap;

    void FnHandleEvents(const std::string& eventName, const BaseEvent* event);
    static EventHandler* getInstance();

    /**
     * Singleton EventHandler should not be cloneable.
     */
    EventHandler(EventHandler& eventHandler) = delete;

    /**
     * Singleton EventHandler should not be assignable.
     */
    void operator=(const EventHandler&) = delete;

private:
    static EventHandler* eventHandler_;
    static std::mutex mutex_;
    EventHandler();

    // DIO Event Handler
    bool handleDIOEvent(const BaseEvent* event);

    // LPR Event Handler
    bool handleLPRReceive(const BaseEvent* event);

    // Printer Event Handler
    bool handlePrinterStatus(const BaseEvent* event);

    // Barcode Scanner Event Handler
    bool handleBarcodeReceived(const BaseEvent* event);

    // Touch N Go Reader Event Handler
    bool handleTnGPayRequest(const BaseEvent* event);
    bool handleTnGPayCancelRequest(const BaseEvent* event);
    bool handleTnGEnableReaderRequest(const BaseEvent* event);
    bool handleTnGPayResultReceived(const BaseEvent* event);
    bool handleTnGCardNumReceived(const BaseEvent* event);
};