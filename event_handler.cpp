#include <iostream>
#include <memory>
#include <string>
#include <sstream>
#include "common.h"
#include "dio.h"
#include "event_handler.h"
#include "log.h"
#include "operation.h"
#include "lpr.h"

EventHandler* EventHandler::eventHandler_ = nullptr;
std::mutex EventHandler::mutex_;

std::map<std::string, EventHandler::EventFunction> EventHandler::eventMap = 
{
    // DIO Event
    {   "Evt_handleDIOEvent"                    ,std::bind(&EventHandler::handleDIOEvent                   ,eventHandler_, std::placeholders::_1) },

    // LPR Event
    {   "Evt_handleLPRReceive"                  ,std::bind(&EventHandler::handleLPRReceive                 ,eventHandler_, std::placeholders::_1) },

    // Barcode Scanner Event
    {   "Evt_handleBarcodeReceived"             ,std::bind(&EventHandler::handleBarcodeReceived            ,eventHandler_, std::placeholders::_1) },

    // Touch N Go Reader Event
    // Touch N Go Reader Request Event
    {   "Evt_handleTnGPayRequest"               ,std::bind(&EventHandler::handleTnGPayRequest              ,eventHandler_, std::placeholders::_1) },
    {   "Evt_handleTnGPayCancelRequest"         ,std::bind(&EventHandler::handleTnGPayCancelRequest        ,eventHandler_, std::placeholders::_1) },
    {   "Evt_handleTnGEnableReaderRequest"      ,std::bind(&EventHandler::handleTnGEnableReaderRequest     ,eventHandler_, std::placeholders::_1) },
    // Touch N Go Reader Response Event
    {   "Evt_handleTnGPayResultReceived"        ,std::bind(&EventHandler::handleTnGPayResultReceived       ,eventHandler_, std::placeholders::_1) },
    {   "Evt_handleTnGCardNumReceived"          ,std::bind(&EventHandler::handleTnGCardNumReceived         ,eventHandler_, std::placeholders::_1) }
};

EventHandler::EventHandler()
{
}

EventHandler* EventHandler::getInstance()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (eventHandler_ == nullptr)
    {
        eventHandler_ = new EventHandler();
    }
    return eventHandler_;
}

void EventHandler::FnHandleEvents(const std::string& eventName, const BaseEvent* event)
{
    auto it = eventMap.find(eventName);

    if (it != eventMap.end())
    {
        EventFunction& handler = it->second;

        bool ret = false;
        try
        {
            ret = handler(event);
        }
        catch (const std::exception& e)
        {
            std::stringstream ss;
            ss << __func__ << ", Exception: " << e.what();
            Logger::getInstance()->FnLogExceptionError(ss.str());
        }
        catch (...)
        {
            std::stringstream ss;
            ss << __func__ << ", Exception: Unknown Exception";
            Logger::getInstance()->FnLogExceptionError(ss.str());
        }
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Event not found : " << eventName;
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
    }
}

bool EventHandler::handleDIOEvent(const BaseEvent* event)
{
    bool ret = true;

    const Event<int>* intEvent = dynamic_cast<const Event<int>*>(event);

    if (intEvent != nullptr)
    {
        DIO::DIO_EVENT dioEvent = static_cast<DIO::DIO_EVENT>(intEvent->data);

        switch (dioEvent)
        {
            case DIO::DIO_EVENT::LOOP_A_ON_EVENT:
            {
              //  Logger::getInstance()->FnLog("DIO::DIO_EVENT::LOOP_A_ON_EVENT");
                operation::getInstance()->tProcess.gbLoopApresent.store(true);
                operation::getInstance()->LoopACome();
                break;
            }
            case DIO::DIO_EVENT::LOOP_A_OFF_EVENT:
            {
             //   Logger::getInstance()->FnLog("DIO::DIO_EVENT::LOOP_A_OFF_EVENT");
                operation::getInstance()->tProcess.gbLoopApresent.store(false);
                operation::getInstance()->LoopAGone();
                break;
            }
            case DIO::DIO_EVENT::LOOP_B_ON_EVENT:
            {
                Logger::getInstance()->FnLog("DIO::DIO_EVENT::LOOP_B_ON_EVENT");
                break;
            }
            case DIO::DIO_EVENT::LOOP_B_OFF_EVENT:
            {
                Logger::getInstance()->FnLog("DIO::DIO_EVENT::LOOP_B_OFF_EVENT");
                break;
            }
            case DIO::DIO_EVENT::LOOP_C_ON_EVENT:
            {
           //     Logger::getInstance()->FnLog("DIO::DIO_EVENT::LOOP_C_ON_EVENT");
                operation::getInstance()->LoopCCome();
                break;
            }
            case DIO::DIO_EVENT::LOOP_C_OFF_EVENT:
            {
            //    Logger::getInstance()->FnLog("DIO::DIO_EVENT::LOOP_C_OFF_EVENT");
                operation::getInstance()->LoopCGone();
                break;
            }
            case DIO::DIO_EVENT::INTERCOM_ON_EVENT:
            {
                Logger::getInstance()->FnLog("DIO::DIO_EVENT::INTERCOM_ON_EVENT");
                break;
            }
            case DIO::DIO_EVENT::INTERCOM_OFF_EVENT:
            {
                Logger::getInstance()->FnLog("DIO::DIO_EVENT::INTERCOM_OFF_EVENT");
                break;
            }
            case DIO::DIO_EVENT::STATION_DOOR_OPEN_EVENT:
            {
                Logger::getInstance()->FnLog("DIO::DIO_EVENT::STATION_DOOR_OPEN_EVENT");
                operation::getInstance()->HandlePBSError(SDoorError);
                break;
            }
            case DIO::DIO_EVENT::STATION_DOOR_CLOSE_EVENT:
            {
                Logger::getInstance()->FnLog("DIO::DIO_EVENT::STATION_DOOR_CLOSE_EVENT");
                operation::getInstance()->HandlePBSError(SDoorNoError);
                break;
            }
            case DIO::DIO_EVENT::BARRIER_DOOR_OPEN_EVENT:
            {
                Logger::getInstance()->FnLog("DIO::DIO_EVENT::BARRIER_DOOR_OPEN_EVENT");
                operation::getInstance()->HandlePBSError(BDoorError);
                break;
            }
            case DIO::DIO_EVENT::BARRIER_DOOR_CLOSE_EVENT:
            {
                Logger::getInstance()->FnLog("DIO::DIO_EVENT::BARRIER_DOOR_CLOSE_EVENT");
                operation::getInstance()->HandlePBSError(BDoorNoError);
                break;
            }
            case DIO::DIO_EVENT::BARRIER_STATUS_ON_EVENT:
            {
                Logger::getInstance()->FnLog("DIO::DIO_EVENT::BARRIER_STATUS_ON_EVENT");
                if (DIO::getInstance()->FnGetManualOpenBarrierStatusFlag() == 1)
                {
                    DIO::getInstance()->FnSetManualOpenBarrierStatusFlag(0);
                    db::getInstance()->AddSysEvent("Barrier up");
                    //----- add manual open barrier(by operator)
                    operation::getInstance()->ManualOpenBarrier(false);
                }
                break;
            }
            case DIO::DIO_EVENT::BARRIER_STATUS_OFF_EVENT:
            {
                Logger::getInstance()->FnLog("DIO::DIO_EVENT::BARRIER_STATUS_OFF_EVENT");
                break;
            }
            case DIO::DIO_EVENT::MANUAL_OPEN_BARRIED_ON_EVENT:
            {
                Logger::getInstance()->FnLog("DIO::DIO_EVENT::MANUAL_OPEN_BARRIED_ON_EVENT");

                operation::getInstance()->writelog("Open barrier action(by operator)", "OPR");
                break;
            }
            case DIO::DIO_EVENT::MANUAL_OPEN_BARRIED_OFF_EVENT:
            {
                Logger::getInstance()->FnLog("DIO::DIO_EVENT::MANUAL_OPEN_BARRIED_OFF_EVENT");
                break;
            }
            case DIO::DIO_EVENT::LORRY_SENSOR_ON_EVENT:
            {
                Logger::getInstance()->FnLog("DIO::DIO_EVENT::LORRY_SENSOR_ON_EVENT");
                break;
            }
            case DIO::DIO_EVENT::LORRY_SENSOR_OFF_EVENT:
            {
                Logger::getInstance()->FnLog("DIO::DIO_EVENT::LORRY_SENSOR_OFF_EVENT");
                break;
            }
            case DIO::DIO_EVENT::ARM_BROKEN_ON_EVENT:
            {
                Logger::getInstance()->FnLog("DIO::DIO_EVENT::ARM_BROKEN_ON_EVENT");
                operation::getInstance()->HandlePBSError(BarrierStatus, 3);
                db::getInstance()->AddSysEvent("Arm failure detected.");
                break;
            }
            case DIO::DIO_EVENT::ARM_BROKEN_OFF_EVENT:
            {
                Logger::getInstance()->FnLog("DIO::DIO_EVENT::ARM_BROKEN_OFF_EVENT");
                operation::getInstance()->HandlePBSError(BarrierStatus, 0);
                db::getInstance()->AddSysEvent("Arm recovered successfully.");
                break;
            }
            case DIO::DIO_EVENT::BARRIER_OPEN_TOO_LONG_ON_EVENT:
            {
                Logger::getInstance()->FnLog("DIO::DIO_EVENT::BARRIER_OPEN_TOO_LONG_ON_EVENT");
                operation::getInstance()->HandlePBSError(BarrierStatus, 2);
                db::getInstance()->AddSysEvent("Barrier open too long detected.");
                break;
            }
            case DIO::DIO_EVENT::BARRIER_OPEN_TOO_LONG_OFF_EVENT:
            {
                Logger::getInstance()->FnLog("DIO::DIO_EVENT::BARRIER_OPEN_TOO_LONG_OFF_EVENT");
                operation::getInstance()->HandlePBSError(BarrierStatus, 0);
                db::getInstance()->AddSysEvent("Barrier open too long - closed successfully.");
                break;
            }
            default:
            {
                Logger::getInstance()->FnLog("DIO::DIO_EVENT::UNKNOWN_EVENT");
                break;
            }
        }
        std::stringstream ss;
        ss << __func__ << " Successfully, Event Data : " << static_cast<int>(dioEvent);
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Event Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
        ret = false;
    }

    return ret;
}

bool EventHandler::handleLPRReceive(const BaseEvent* event)
{
    bool ret = true;

    const Event<std::string>* strEvent = dynamic_cast<const Event<std::string>*>(event);

    if (strEvent != nullptr)
    {
        struct Lpr::LPREventData eventData = Lpr::getInstance()->deserializeEventData(strEvent->data);
        
        std::stringstream ss;
        ss << __func__ << " Successfully, Event Data : " << "camType : " << static_cast<int>(eventData.camType);
        ss << ", LPN : " << eventData.LPN;
        ss << ", TransID : " << eventData.TransID;
        ss << ", imagePath : " << eventData.imagePath;
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");

        Lpr::CType camType = eventData.camType;
        std::string LPN = eventData.LPN;
        std::string TransID = eventData.TransID;
        std::string imagePath = eventData.imagePath;
        operation::getInstance()->ReceivedLPR(camType,LPN, TransID, imagePath);
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Event Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
        ret = false;
    }
    
    return ret;
}

bool EventHandler::handleBarcodeReceived(const BaseEvent* event)
{
    bool ret = true;

    const Event<std::string>* strEvent = dynamic_cast<const Event<std::string>*>(event);

    if (strEvent != nullptr)
    {
        std::stringstream ss;
        ss << __func__ << " Successfully, Event Data : " << strEvent->data;
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
        // process barcode data
        operation::getInstance()->ProcessBarcodeData(strEvent->data);

    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Event Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
        ret = false;
    }

    return ret;
}

bool EventHandler::handleTnGPayRequest(const BaseEvent* event)
{
    bool ret = true;

    const Event<bool>* boolEvent = dynamic_cast<const Event<bool>*>(event);

    if (boolEvent != nullptr)
    {
        bool value = boolEvent->data;

        std::stringstream ss;
        ss << __func__ << " Successfully, Event Data : " << value;
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");

        // Handle Touch N Go Reader Pay Request
        if (value)
        {
            // Request Success
            operation::getInstance()->HandlePBSError(UPOSNoError);
        }
        else
        {
            // Request Fail
            operation::getInstance()->HandlePBSError(UPOSError);
        }
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Event Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
        ret = false;
    }

    return ret;
}

bool EventHandler::handleTnGPayCancelRequest(const BaseEvent* event)
{
    bool ret = true;

    const Event<bool>* boolEvent = dynamic_cast<const Event<bool>*>(event);

    if (boolEvent != nullptr)
    {
        bool value = boolEvent->data;

        std::stringstream ss;
        ss << __func__ << " Successfully, Event Data : " << value;
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");

        // Handle Touch N Go Reader Pay Cancel Request
        if (value)
        {
            // Request Success
            operation::getInstance()->HandlePBSError(UPOSNoError);
        }
        else
        {
            // Request Fail
            operation::getInstance()->HandlePBSError(UPOSError);
        }
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Event Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
        ret = false;
    }

    return ret;
}

bool EventHandler::handleTnGEnableReaderRequest(const BaseEvent* event)
{
    bool ret = true;

    const Event<bool>* boolEvent = dynamic_cast<const Event<bool>*>(event);

    if (boolEvent != nullptr)
    {
        bool value = boolEvent->data;

        std::stringstream ss;
        ss << __func__ << " Successfully, Event Data : " << value;
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");

        // Handle Touch N Go Reader Enable Reader Request
        if (value)
        {
            // Request Success
            operation::getInstance()->HandlePBSError(UPOSNoError);
        }
        else
        {
            // Request Fail
            operation::getInstance()->HandlePBSError(UPOSError);
        }
    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Event Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
        ret = false;
    }

    return ret;
}

bool EventHandler::handleTnGPayResultReceived(const BaseEvent* event)
{
    bool ret = true;

    const Event<std::string>* strEvent = dynamic_cast<const Event<std::string>*>(event);

    if (strEvent != nullptr)
    {
        std::stringstream ss;
        ss << __func__ << " Successfully, Event Data : " << strEvent->data;
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
        // process Touch N Go Pay Result Received
        operation::getInstance()->processTnGResponse("PayResult", strEvent->data);

    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Event Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
        ret = false;
    }

    return ret;
}

bool EventHandler::handleTnGCardNumReceived(const BaseEvent* event)
{
    bool ret = true;

    const Event<std::string>* strEvent = dynamic_cast<const Event<std::string>*>(event);

    if (strEvent != nullptr)
    {
        std::stringstream ss;
        ss << __func__ << " Successfully, Event Data : " << strEvent->data;
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
        // process Touch N Go Card Number Received
        operation::getInstance()->processTnGResponse("TnGCardNumResult", strEvent->data);

    }
    else
    {
        std::stringstream ss;
        ss << __func__ << " Event Data casting failed.";
        Logger::getInstance()->FnLog(ss.str());
        Logger::getInstance()->FnLog(ss.str(), eventLogFileName, "EVT");
        ret = false;
    }

    return ret;
}