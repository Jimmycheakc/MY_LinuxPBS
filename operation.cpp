
#include <sys/mount.h>
#include <iostream>
#include <string>
#include <memory>
#include <chrono>
#include <thread>
#include <filesystem>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <cstdio>
#include <map>
#include "common.h"
#include "gpio.h"
#include "operation.h"
#include "ini_parser.h"
#include "structuredata.h"
#include "db.h"
#include "led.h"
#include "lcd.h"
#include "log.h"
#include "udp.h"
#include "dio.h"
#include "lpr.h"
#include "printer.h"
#include "barcode_reader.h"
#include "boost/algorithm/string.hpp"
#include "touchngo_reader.h"

operation* operation::operation_ = nullptr;
std::mutex operation::mutex_;

operation::operation()
{
    isOperationInitialized_.store(false);
}

operation* operation::getInstance()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (operation_ == nullptr)
    {
        operation_ = new operation();
    }
    return operation_;
}

void operation::OperationInit(io_context& ioContext)
{
    Setdefaultparameter();
    operationStrand_ = std::make_unique<boost::asio::io_context::strand>(ioContext);
    gtStation.iSID = std::stoi(IniParser::getInstance()->FnGetStationID());
    tParas.gsCentralDBName = IniParser::getInstance()->FnGetCentralDBName();
    tParas.gsCentralDBServer = IniParser::getInstance()->FnGetCentralDBServer();
    //
    iCurrentContext = &ioContext;
    //--- broad cast UDP
    tProcess.gsBroadCastIP = getIPAddress();
    try
    {
        m_udp = new udpclient(ioContext, tProcess.gsBroadCastIP, 2001, 2001, true);
    }
    catch (const boost::system::system_error& e) // Catch Boost.Asio system errors
    {
        std::string cppString(e.what());
        writelog ("Boost.Asio Exception during PMS UDP initialization: "+ cppString,"OPR");
    }
    catch (const std::exception& e) {
        std::string cppString(e.what());
        writelog ("Exception during PMS UDP initialization: "+ cppString,"OPR");
    }
    catch (...)
    {
        writelog ("Unknown Exception during PMS UDP initialization.","OPR");
    }
    // monitor UDP
    try
    {
        m_Monitorudp = new udpclient(ioContext, tParas.gsCentralDBServer, 2008,2008);
    }
    catch (const boost::system::system_error& e) // Catch Boost.Asio system errors
    {
        std::string cppString1(e.what());
        writelog ("Boost.Asio Exception during Monitor UDP initialization: "+ cppString1,"OPR");
    }
    catch (const std::exception& e) {
        std::string cppString1(e.what());
        writelog ("Exception during Monitor UDP initialization: "+ cppString1,"OPR");
    }
    catch (...)
    {
        writelog ("Unknown Exception during Monitor UDP initialization.","OPR");
    }
    //
    int iRet = 0;
    m_db = db::getInstance();

    //iRet = m_db->connectlocaldb("DSN={MariaDB-server};DRIVER={MariaDB ODBC 3.0 Driver};SERVER=127.0.0.1;PORT=3306;DATABASE=linux_pbs;UID=linuxpbs;PWD=SJ2001;",2,2,1);
    iRet = m_db->connectlocaldb("DRIVER={MariaDB ODBC 3.0 Driver};SERVER=localhost;PORT=3306;DATABASE=linux_pbs;UID=linuxpbs;PWD=SJ2001;",2,2,1);
    if (iRet != 1) {
        writelog ("Unable to connect local DB.","OPR");
        exit(0); 
    }
    string m_connstring;
    writelog ("Connect Central (" +tParas.gsCentralDBServer +  ") DB:"+tParas.gsCentralDBName,"OPR");
    for (int i = 0 ; i < 5; ++i ) {
      //  m_connstring = "DSN=mssqlserver;DATABASE=" + tParas.gsCentralDBName + ";UID=sa;PWD=yzhh2007";
        m_connstring = "DRIVER=FreeTDS;SERVER=" + tParas.gsCentralDBServer + ";PORT=1433;DATABASE=" + tParas.gsCentralDBName + ";UID=sa;PWD=yzhh2007";
        iRet = m_db->connectcentraldb(m_connstring,tParas.gsCentralDBServer,2,2,1);
        if (iRet == 1) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
    // sync central time
    if (iRet == 1) {
        m_db->synccentraltime();
    } else
    {
        tProcess.giSystemOnline = 1;
    }
    //
    if (LoadParameter()) {
        Initdevice(ioContext);
        //----
        writelog("EPS in operation","OPR");
        //------
        tProcess.setIdleMsg(0, operation::getInstance()->tMsg.Msg_DefaultLED[0]);
        tProcess.setIdleMsg(1, operation::getInstance()->tMsg.Msg_Idle[1]);
        //-------
        Clearme();
        isOperationInitialized_.store(true);
        DIO::getInstance()->FnStartDIOMonitoring();
        SendMsg2Server("90",",,,,,Starting OK");
        //-----
        m_db->downloadseason();
        m_db->moveOfflineTransToCentral();

        // Check Barrier
        writelog("Check barrier", "OPR");
        if (tParas.gbLockBarrier == true)
        {
            writelog("Startup: continue open barrier", "OPR");
            continueOpenBarrier();
        }
        //-----
    }else {
        tProcess.gbInitParamFail = 1;
        writelog("Unable to load parameter, Please download or check!", "OPR");
        if (iRet == 1)
        {
            m_db->downloadstationsetup();
            m_db->loadstationsetup();
        }
    }
}

bool operation::LoadParameter()
{
    DBError iReturn;
    bool gbLoadParameter = true;
    //------
    iReturn = m_db->loadstationsetup();
    if (iReturn != 0) {
        if (iReturn ==1) {
            writelog ("No data for station setup table", "OPR");
        }else{
            writelog ("Error for loading station setup", "OPR");
        }
        gbLoadParameter = false; 
    } 
    iReturn = m_db->loadmessage();
    if (iReturn != 0) {
        if (iReturn ==1) {
            writelog ("No data for LED message table", "OPR");
        }else{
            writelog ("Error for loading LED message", "OPR");
        }
        gbLoadParameter = false; 
    }

    iReturn = m_db->loadExitmessage();
    if (iReturn != 0) {
        if (iReturn ==1) {
            writelog ("No data for LED Exit message table", "OPR");
        }else{
            writelog ("Error for loading LED Exit message", "OPR");
        }
        gbLoadParameter = false; 
    }

    iReturn = m_db->loadParam();
    if (iReturn != 0) {
        if (iReturn ==1) {
            writelog ("No data for Parameter table", "OPR");
        }else{
            writelog ("Error for loading parameter", "OPR");
        }
        gbLoadParameter = false; 
    }
    iReturn = m_db->loadvehicletype();
    if (iReturn != 0) {
        if (iReturn ==1) {
            writelog ("No data for vehicle type table", "OPR");
        }else{
            writelog ("Error for loading vehicle type", "OPR");
        }
       gbLoadParameter = false; 
    }
    iReturn = m_db->loadTR(2);
    if (iReturn != 0)
    {
        if (iReturn == 1)
        {
            writelog("No data for TR table", "OPR");
        }
        else
        {
            writelog("Error for loading TR", "OPR");
        }
        gbLoadParameter = false;
    }
     iReturn = m_db->LoadTariffTypeInfo();
     if (iReturn != 0)
    {
        if (iReturn == 1)
        {
            writelog("No data for Tariff Type Info table", "OPR");
        }
        else
        {
            writelog("Error for loading Tariff Type Info", "OPR");
        }
        gbLoadParameter = false;
    }

    iReturn = m_db->LoadHoliday();
    
    if (iReturn != 0)
    {
        if (iReturn == 1)
        {
            writelog("No data for holiday table", "OPR");
        }
        else
        {
            writelog("Error for loading holiday", "OPR");
        }
        gbLoadParameter = false;
    }

    iReturn = m_db->LoadTariff();

     if (iReturn != 0)
    {
        if (iReturn == 1)
        {
            writelog("No data for Tariff table", "OPR");
        }
        else
        {
            writelog("Error for loading Tariff", "OPR");
        }
        gbLoadParameter = false;
    }

    iReturn = m_db->LoadXTariff();

     if (iReturn != 0)
    {
        if (iReturn == 1)
        {
            writelog("No data for XTariff table", "OPR");
        }
        else
        {
            writelog("Error for loading XTariff", "OPR");
        }
        gbLoadParameter = false;
    }
    return gbLoadParameter;
}

bool operation:: LoadedparameterOK()
{
    if (tProcess.gbloadedLEDMsg == true && tProcess.gbloadedLEDExitMsg == true && tProcess.gbloadedParam==true && tProcess.gbloadedStnSetup==true && tProcess.gbloadedVehtype ==true) return true;
    else return false;
}

bool operation::FnIsOperationInitialized() const
{
    return isOperationInitialized_.load();
}

void operation::FnLoopATimeoutHandler()
{
  //  Logger::getInstance()->FnLog("Loop A Timeout handler.", "", "OPR");
  //  LoopACome();
}

void operation::LoopACome()
{
    //--------
    writelog ("Loop A Come","OPR");

    // Loop A timer - To prevent loop A hang
    pLoopATimer_->expires_from_now(boost::posix_time::seconds(operation::getInstance()->tParas.giOperationTO));
    pLoopATimer_->async_wait(boost::asio::bind_executor(*operationStrand_, [this] (const boost::system::error_code &ec)
    {
        if (!ec)
        {
            this->FnLoopATimeoutHandler();
        }
        else if (ec == boost::asio::error::operation_aborted)
        {
            Logger::getInstance()->FnLog("Loop A timer cancelled.", "", "OPR");
        }
        else
        {
            std::stringstream ss;
            ss << "Loop A timer timeout error :" << ec.message();
            Logger::getInstance()->FnLog(ss.str(), "", "OPR");
        }
    }));

    ShowLEDMsg(tMsg.Msg_LoopA[0], tMsg.Msg_LoopA[1]);
    Clearme();
    DIO::getInstance()->FnSetLCDBacklight(1);
    //----
    int vechicleType = GetVTypeFromLoop();
    std::string transID = "";
    bool useFrontCamera = false;
    // Motorcycle - use rear camera
    if (vechicleType == 7)
    {
        useFrontCamera = false;
        transID = tParas.gscarparkcode + "-" + std::to_string (gtStation.iSID) + "B-" + Common::getInstance()->FnGetDateTimeFormat_yyyymmddhhmmss();
    }
    // Car or Lorry - use front camera
    else
    {
        useFrontCamera = true;
        transID = tParas.gscarparkcode + "-" + std::to_string (gtStation.iSID) + "F-" + Common::getInstance()->FnGetDateTimeFormat_yyyymmddhhmmss();
    }
    tEntry.gsTransID = transID;
    Lpr::getInstance()->FnSendTransIDToLPR(tEntry.gsTransID, useFrontCamera);

    //----
    ShowLEDMsg("Reading IU...^Please wait", "Reading IU...^Please wait");
 //   operation::getInstance()->EnableCashcard(true);
}

void operation::LoopAGone()
{
    writelog ("Loop A End","OPR");

    // Cancel the loop A timer 
    pLoopATimer_->cancel();

    //------
    DIO::getInstance()->FnSetLCDBacklight(0);
    EnableCashcard(false);
}
void operation::LoopCCome()
{
    writelog ("Loop C Come","OPR");

}
void operation::LoopCGone()
{
    writelog ("Loop C End","OPR");
    //-----
    Clearme();
    //------
    if (tProcess.gbLoopApresent.load() == true)
    {
        LoopACome();
    }else{
        //shwo default Msg
    }
}
void operation::VehicleCome(string sNo)
{

    if (sNo == "") return;
    //--------------------

    if (sNo.length()==10 && sNo == tProcess.gsDefaultIU) {
        SendMsg2Server ("90",sNo+",,,,,Default IU");
        writelog ("Default IU: "+sNo,"OPR");
        //---------
        return;
    }
    //-----
    if (sNo.length() == 10) EnableCashcard(false);
    //----
    if (gtStation.iType == tientry) {

        if(tEntry.gbEntryOK == false) PBSEntry (sNo);
    } 
    else{
        //check IU or card status
        CheckIUorCardStatus(sNo, Ant);
    }
    return;
}

void operation::Openbarrier()
{
    if (tProcess.giCardIsIn == 1) {
        ShowLEDMsg ("Please Take^CashCard.","Please Take^Cashcard.");
        return;
    }

    if (tProcess.giBarrierContinueOpened == 1)
    {
        return;
    }
    writelog ("Open Barrier","OPR");

    if (tParas.gsBarrierPulse == 0){tParas.gsBarrierPulse = 500;}

    DIO::getInstance()->FnSetOpenBarrier(1);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(tParas.gsBarrierPulse));
    
    DIO::getInstance()->FnSetOpenBarrier(0);
}

void operation::closeBarrier()
{
    writelog ("Close barrier.", "OPR");
    
    if (tParas.gsBarrierPulse == 0){tParas.gsBarrierPulse = 500;}

    // Reset the continue open barrier
    DIO::getInstance()->FnSetOpenBarrier(0);

    DIO::getInstance()->FnSetCloseBarrier(1);
    //
    std::this_thread::sleep_for(std::chrono::milliseconds(tParas.gsBarrierPulse));
    //
    DIO::getInstance()->FnSetCloseBarrier(0);

    tProcess.giBarrierContinueOpened = 0;
}

void operation::continueOpenBarrier()
{
    Logger::getInstance()->FnLog("Continue Open Barrier", "OPR");

    DIO::getInstance()->FnSetOpenBarrier(1);

    tProcess.giBarrierContinueOpened = 1;
}

void operation::Clearme()
{
    tProcess.giShowType = 1;
    tProcess.giIsSeason = 0;
    tProcess.giCardIsIn = 0;
    tProcess.gbsavedtrans = false;
    tProcess.sEnableReader = false;
    //---
    if (gtStation.iType == tientry)
    {
        tEntry.esid = to_string(gtStation.iSID);
        tEntry.sSerialNo = "";     
        tEntry.sIUTKNo = "";
        tEntry.sEntryTime = "";
        tEntry.iTransType = 1;
        tEntry.iRateType = 0;
        tEntry.iStatus = 0;      

        tEntry.sCardNo = "";
        tEntry.sFee = 0.00;
        tEntry.sPaidAmt = 0.00;
        tEntry.sGSTAmt = 0.00;
        tEntry.sReceiptNo = "";
        tEntry.iCardType=0;
        tEntry.sOweAmt= 0.00;

        tEntry.sLPN[0] = "";
        tEntry.sLPN[1] = "";
        tEntry.iVehicleType = 0;
        tEntry.gbEntryOK = false;
        tEntry.gsTransID = ""; 
    }
    else
    {
        tExit.xsid = to_string(gtStation.iSID);
	    tExit.sExitTime = "";
	    tExit.sIUNo = "";
	    tExit.sCardNo = "";
	    tExit.iTransType = 0;
	    tExit.lParkedTime = 0;
	    tExit.sFee = 0.00 ;
	    tExit.sPaidAmt = 0.00;
	    tExit.sReceiptNo = "";
        tExit.iflag4Receipt= 0;
	    tExit.iStatus = 0;
        tExit.sOweAmt = 0.00;
        tExit.sPrePaid = 0.00;
	    tExit.sRedeemAmt = 0.00;
	    tExit.iRedeemTime = 0;
	    tExit.sRedeemNo = "";
	    tExit.sGSTAmt = 0.00;
	    tExit.sCHUDebitCode = "";
	    tExit.iCardType = 0;
	    tExit.sTopupAmt = 0;
	    tExit.uposbatchno = "";
	    tExit.feefrom = "EPS";
	    tExit.lpn = "";
	
	    tExit.iRateType = 0;
	    tExit.sEntryTime = "";
	
        tExit.dtValidTo = "";
	    tExit.dtValidFrom = "";

	    tExit.iEntryID =0;
	    tExit.sExitNo = "";
	    tExit.iUseMultiCard = 0;
	    tExit.iNeedSendIUtoEntry = 0;
	    tExit.iPartialseason = 0;
	    tExit.sRegisterCard = "";
	    tExit.iAttachedTransType = 0;

	    tExit.sCalFeeTime = "";
	    tExit.sRebateAmt = 0.00;
	    tExit.sRebateBalance = 0.00;
	    tExit.sRebateDate = "";
	    tExit.sLPN[0] = "";
        tExit.sLPN[1] = "";
	    tExit.iVehicleType = 0;

	    tExit.entry_lpn = "";
	    tExit.video_location = "";
	    tExit.video1_location = "";

	    tExit.giDeductionStatus = init;
        tExit.gbPaid.store(false);
        tExit.bNoEntryRecord = -1;
        tExit.bPayByEZPay.store(false);

    }

}

std::string operation::getSerialPort(const std::string& key)
{
    std::map<std::string, std::string> serial_port_map = 
    {
        {"1", "/dev/ttyCH9344USB7"}, // J3
        {"2", "/dev/ttyCH9344USB6"}, // J4
        {"3", "/dev/ttyCH9344USB5"}, // J5
        {"4", "/dev/ttyCH9344USB4"}, // J6
        {"5", "/dev/ttyCH9344USB3"}, // J7
        {"6", "/dev/ttyCH9344USB2"}, // J8
        {"7", "/dev/ttyCH9344USB1"}, // J9
        {"8", "/dev/ttyCH9344USB0"}  // J10
    };

    auto it = serial_port_map.find(key);

    if (it != serial_port_map.end())
    {
        return it->second;
    }
    else
    {
        return "";
    }
}

void operation::Initdevice(io_context& ioContext)
{
    if (tParas.giCommPortLED > 0)
    {
        int max_char_per_row = 0;

        if (tParas.giLEDMaxChar < 20)
        {
            max_char_per_row =  LED::LED216_MAX_CHAR_PER_ROW;
        }
        else
        {
            max_char_per_row =  LED::LED226_MAX_CHAR_PER_ROW;
        }
        LEDManager::getInstance()->createLED(ioContext, 9600, getSerialPort(std::to_string(tParas.giCommPortLED)), max_char_per_row);
    }

    if (tParas.giCommportLED401 > 0)
    {
        LEDManager::getInstance()->createLED(ioContext, 9600, getSerialPort(std::to_string(tParas.giCommportLED401)), LED::LED614_MAX_CHAR_PER_ROW);
    }

    if (LCD::getInstance()->FnLCDInit())
    {
        pLCDIdleTimer_ = std::make_unique<boost::asio::deadline_timer>(ioContext);

        pLCDIdleTimer_->expires_from_now(boost::posix_time::seconds(1));
        pLCDIdleTimer_->async_wait(boost::asio::bind_executor(*operationStrand_, [this] (const boost::system::error_code &ec)
        {
            if (!ec)
            {
                this->LcdIdleTimerTimeoutHandler();
            }
            else if (ec == boost::asio::error::operation_aborted)
            {
                Logger::getInstance()->FnLog("LCD Idle timer cancelled.", "", "OPR");
            }
            else
            {
                std::stringstream ss;
                ss << "LCD Idle timer timeout error :" << ec.message();
                Logger::getInstance()->FnLog(ss.str(), "", "OPR");
            }
        }));
    }

    if (tParas.giCommPortPrinter > 0)
    {
        Printer::getInstance()->FnSetPrintMode(2);
        Printer::getInstance()->FnSetDefaultAlign(Printer::CBM_ALIGN::CBM_LEFT);
        Printer::getInstance()->FnSetDefaultFont(2);
        //Printer::getInstance()->FnSetLeftMargin(0);
        Printer::getInstance()->FnSetSelfTestInterval(2000);
        Printer::getInstance()->FnSetSiteID(10);
        Printer::getInstance()->FnSetPrinterType(Printer::PRINTER_TYPE::CBM1000);
        Printer::getInstance()->FnPrinterInit(9600, getSerialPort(std::to_string(tParas.giCommPortPrinter)));
    }

    DIO::getInstance()->FnDIOInit();
    Lpr::getInstance()->FnLprInit(ioContext);
    BARCODE_READER::getInstance()->FnBarcodeReaderInit();

    // Loop A timer
    pLoopATimer_ = std::make_unique<boost::asio::deadline_timer>(ioContext);
}

void operation::LcdIdleTimerTimeoutHandler()
{
    if ((tProcess.gbcarparkfull.load() == false) && (tProcess.gbLoopApresent.load() == false))
    {
        ShowLEDMsg(tProcess.getIdleMsg(0), tProcess.getIdleMsg(1));
        std::string LCDMsg = "";
        if (IniParser::getInstance()->FnGetShowTime())
        {
            LCDMsg = Common::getInstance()->FnGetDateTimeFormat_ddmmyyy_hhmmss();
        }
        else
        {
            LCDMsg = tParas.gsCompany;
        }
        char * sLCDMsg = const_cast<char*>(LCDMsg.data());
        LCD::getInstance()->FnLCDDisplayRow(2, sLCDMsg);
    }
    else if ((tProcess.gbcarparkfull.load() == true) && (tProcess.gbLoopApresent.load() == false))
    {
        ShowLEDMsg(tProcess.getIdleMsg(0), tProcess.getIdleMsg(1));
    }

    // Restart the lcd idle timer
    pLCDIdleTimer_->expires_at(pLCDIdleTimer_->expires_at() + boost::posix_time::seconds(1));
    pLCDIdleTimer_->async_wait(boost::asio::bind_executor(*operationStrand_, [this] (const boost::system::error_code &ec)
    {
        if (!ec)
        {
            this->LcdIdleTimerTimeoutHandler();
        }
        else if (ec == boost::asio::error::operation_aborted)
        {
            Logger::getInstance()->FnLog("LCD Idle timer cancelled.", "", "OPR");
        }
        else
        {
            std::stringstream ss;
            ss << "LCD Idle timer timeout error :" << ec.message();
            Logger::getInstance()->FnLog(ss.str(), "", "OPR");
        }
    }));
}

void operation::ShowLEDMsg(string LEDMsg, string LCDMsg)
{
    static std::string sLastLEDMsg;
    static std::string sLastLCDMsg;

    if (sLastLEDMsg != LEDMsg)
    {
        sLastLEDMsg = LEDMsg;
        writelog ("LED Message:" + LEDMsg,"OPR");

        if (LEDManager::getInstance()->getLED(getSerialPort(std::to_string(tParas.giCommPortLED))) != nullptr)
        {
            LEDManager::getInstance()->getLED(getSerialPort(std::to_string(tParas.giCommPortLED)))->FnLEDSendLEDMsg("***", LEDMsg, LED::Alignment::CENTER);
        }
    }

    if (sLastLCDMsg != LCDMsg)
    {
        sLastLCDMsg = LCDMsg;
        writelog ("LCD Message:" + LCDMsg,"OPR");

        char* sLCDMsg = const_cast<char*>(LCDMsg.data());
        LCD::getInstance()->FnLCDDisplayScreen(sLCDMsg);
    }
}

void operation::PBSEntry(string sIU)
{
    int iRet;

    tEntry.sIUTKNo = sIU;
    tEntry.sEntryTime= Common::getInstance()->FnGetDateTimeFormat_yyyy_mm_dd_hh_mm_ss();

    if (sIU == "") return;
    //check blacklist
    SendMsg2Server("90",sIU+",,,,,PMS_DVR");
    iRet = m_db->IsBlackListIU(sIU);
    if (iRet >= 0){
        ShowLEDMsg(tMsg.MsgBlackList[0], tMsg.MsgBlackList[1]);
        SendMsg2Server("90",sIU+",,,,,Blacklist IU");
        if(iRet ==0) return;
    }
    //check block 
    string gsBlockIUPrefix = IniParser::getInstance()->FnGetBlockIUPrefix();
   // writelog ("blockIUprfix =" +gsBlockIUPrefix, "OPR");
    if(gsBlockIUPrefix.find(sIU.substr(0,3)) !=std::string::npos && sIU.length() == 10)
    {
        ShowLEDMsg("Lorry No Entry^Pls Reverse","Lorry No Entry^Pls Reverse");
        SendMsg2Server("90",sIU+",,,,,Block IU");
        return;
    }

    if(sIU.length()==10) 
	    tEntry.iTransType= db::getInstance()->FnGetVehicleType(sIU.substr(0,3));
	else {
        tEntry.iTransType=GetVTypeFromLoop();
    }
    if (tEntry.iTransType == 9) {
        ShowLEDMsg(tMsg.Msg_authorizedvehicle[0],tMsg.Msg_authorizedvehicle[1]);
        tEntry.iStatus = 0;
        SaveEntry();
        Openbarrier();
        return;
    }
    if (tProcess.gbcarparkfull.load() == true && tParas.giFullAction == iLock)
    {   
        ShowLEDMsg(tMsg.Msg_LockStation[0], tMsg.Msg_LockStation[1]);
        writelog("Loop A while station Locked","OPR");
        return;
    }
    tEntry.iVehicleType = (tEntry.iTransType -1 )/3;
    iRet = CheckSeason(sIU,1);

    if (tProcess.gbcarparkfull.load() == true && iRet == 1 && (std::stoi(tSeason.rate_type) !=0) && tParas.giFullAction ==iNoPartial )
    {   
        writelog ("VIP Season Only.", "OPR");
        ShowLEDMsg("Carpark Full!^VIP Season Only", "Carpark Full!^VIP Season Only");
        return;
    } 
    if (tProcess.gbcarparkfull.load() == true && iRet != 1 )
    {   
        writelog ("Season Only.", "OPR");
        ShowLEDMsg("Carpark Full!^Season only", "Carpark Full!^Season only");
        return;
    } 
    if (iRet == 6 && sIU.length()>10)
    {
        writelog ("season passback","OPR");
        SendMsg2Server("90",sIU+",,,,,Season Passback");
        ShowLEDMsg(tMsg.Msg_SeasonPassback[0],tMsg.Msg_SeasonPassback[1]);
        return;
    }
    if (iRet ==1 or iRet == 4 or iRet == 6) {
        tEntry.iTransType = GetSeasonTransType(tEntry.iVehicleType,std::stoi(tSeason.rate_type), tEntry.iTransType);
        tProcess.giShowType = 0;
    }

    if (iRet == 10) {
        ShowLEDMsg(tMsg.Msg_SeasonAsHourly[0],tMsg.Msg_SeasonAsHourly[1]);
        tProcess.giShowType = 2;
    }
    if (iRet != 1) {
        ShowLEDMsg(tMsg.Msg_WithIU[0],tMsg.Msg_WithIU[1]);
    }
        //---------
    SaveEntry();
    tEntry.gbEntryOK = true;
    Openbarrier();
}


void operation:: Setdefaultparameter()
{
    tParas = {0};

    tProcess.gsDefaultIU = "1096000001";
    tProcess.glNoofOfflineData = 0;
    tProcess.giSystemOnline = -1;
    //clear error msg
     for (int i= 0; i< Errsize; ++i){
        tPBSError[i].ErrNo = 0;
        tPBSError[i].ErrCode =0;
	    tPBSError[i].ErrMsg = "";
    }
    tProcess.gbcarparkfull.store(false);
    tProcess.gbLoopApresent.store(false);
    tProcess.gbLoopAIsOn = false;
    tProcess.gbLoopBIsOn = false;
    tProcess.gbLoopCIsOn = false;
    tProcess.gbLorrySensorIsOn = false;
    //--------
    tProcess.giSyncTimeCnt = 0;
    tProcess.gbloadedParam = false;
	tProcess.gbloadedVehtype = false;
	tProcess.gbloadedLEDMsg = false;
    tProcess.gbloadedLEDExitMsg = false;
	tProcess.gbloadedStnSetup = false;
    //-------
    tProcess.gbInitParamFail = 0;
    tProcess.giCardIsIn = 0;
    //-------
    tProcess.giLastHousekeepingDate = 0;
    tProcess.setLastIUNo("");

    tProcess.setLastIUEntryTime(std::chrono::steady_clock::now());
    tProcess.setLastTransTime(std::chrono::steady_clock::now());
    //---
    tProcess.fbReadIUfromAnt.store(false);
    tProcess.fiLastCHUCmd = 0;
    tProcess.gsLastDebitFailTime = "";
    tProcess.gsLastPaidTrans = "";
	tProcess.gsLastCardNo = "";
	tProcess.gfLastCardBal = 0;
    tProcess.gbLastPaidStatus.store(false);
    tProcess.glLastSerialNo = 0;
    //----
    tProcess.giUPOSLoginCnt = 0;
    tProcess.giBarrierContinueOpened = 0;
}

string operation:: getIPAddress() 

{
    FILE* pipe = popen("ifconfig", "r");
    if (!pipe) {
        std::cerr << "Error in popen\n";
        return "";
    }

    char buffer[128];
    std::string result = "";
    int iRet = 0;

    // Read the output of the 'ifconfig' command
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result = buffer;
         if (iRet == 1) {
             size_t pos = result.find("inet addr:");
             result = result.substr(pos + 10);
             result.erase(result.find(' '));
            break;
         }

         if (result.find("eth0") != std::string::npos) {
             iRet = 1;
         }
    }
    
    pclose(pipe);
    //------
    tParas.gsLocalIP = result;
    writelog ("local IP address: " + result, "OPR");
    //-----
    size_t lastDotPosition = result.find_last_of('.');
    result = result.substr(0, lastDotPosition + 1)+ "255";
    // Output the result
    return result;

}

void operation:: Sendmystatus()
{
  //EPS error index:  0=antenna,     1=printer,   2=DB, 3=Reader, 4=UPOS
  //5=Param error, 6=DIO,7=Loop A hang,8=CHU, 9=ups, 10= LCSC
  //11= station door status, 12 = barrier door status, 13= TGD controll status
  //14 = TGD sensor status 15=Arm drop status,16=barrier status,17=ticket status d DateTime
    CE_Time dt;
	string str="";

    for (int i= 0; i< Errsize; ++i){
        str += std::to_string(tPBSError[i].ErrNo) + ",";
    }
	str+="0;0,"+dt.DateTimeNumberOnlyString()+",";
	SendMsg2Server("00",str);
	
}

void operation::FnSendMyStatusToMonitor()
{
    //EPS error index:  0=antenna,     1=printer,   2=DB, 3=Reader, 4=UPOS
    //5=Param error, 6=DIO,7=Loop A hang,8=CHU, 9=ups, 10= LCSC
    //11= station door status, 12 = barrier door status, 13= TGD controll status
    //14 = TGD sensor status 15=Arm drop status,16=barrier status,17=ticket status d DateTime
    CE_Time dt;
	string str="";
    //-----
    for (int i= 0; i< Errsize; ++i){
        str += std::to_string(tPBSError[i].ErrNo) + ",";
    }
	str+="0;0,"+dt.DateTimeNumberOnlyString()+",";
	SendMsg2Monitor("300",str);
}

void operation::FnSyncCentralDBTime()
{
    m_db->synccentraltime();
}

void operation::FnSendDIOInputStatusToMonitor(int pinNum, int pinValue)
{
    std::string str = std::to_string(pinNum) + "," + std::to_string(pinValue);
    SendMsg2Monitor("302", str);
}

void operation::FnSendDateTimeToMonitor()
{
    std::string str = Common::getInstance()->FnGetDateTimeFormat_yyyymmddhhmm();
    SendMsg2Monitor("304", str);
}

void operation::FnSendLogMessageToMonitor(std::string msg)
{
    if (m_Monitorudp->FnGetMonitorStatus())
    {
        std::string str = "[" + gtStation.sPCName + "|" + std::to_string(gtStation.iSID) + "|" + "305" + "|" + msg + "|]";
        m_Monitorudp->send(str);
    }
}

void operation::FnSendLEDMessageToMonitor(std::string line1TextMsg, std::string line2TextMsg)
{
    std::string str = line1TextMsg + "," + line2TextMsg;
    SendMsg2Monitor("306", str);
}

void operation::FnSendCmdDownloadParamAckToMonitor(bool success)
{
    std::string str = "99";

    if (!success)
    {
        str = "98";
    }

    SendMsg2Monitor("310", str);
}

void operation::FnSendCmdDownloadIniAckToMonitor(bool success)
{
    std::string str = "99";

    if (!success)
    {
        str = "98";
    }

    SendMsg2Monitor("309", str);
}

void operation::FnSendCmdGetStationCurrLogToMonitor()
{
    try
    {
        // Get today's date
        auto today = std::chrono::system_clock::now();
        auto todayDate = std::chrono::system_clock::to_time_t(today);
        std::tm* localToday = std::localtime(&todayDate);

        std::string logFilePath = Logger::getInstance()->LOG_FILE_PATH;

        // Extract year, month and day
        std::ostringstream ossToday;
        ossToday << std::setw(2) << std::setfill('0') << (localToday->tm_year % 100);
        ossToday << std::setw(2) << std::setfill('0') << (localToday->tm_mon + 1);
        ossToday << std::setw(2) << std::setfill('0') << localToday->tm_mday;

        std::string todayDateStr = ossToday.str();

        // Iterate through the files in the log file path
        int foundNo_ = 0;
        for (const auto& entry : std::filesystem::directory_iterator(logFilePath))
        {
            if ((entry.path().filename().string().find(todayDateStr) != std::string::npos) &&
                (entry.path().extension() == ".log"))
            {
                foundNo_ ++;
            }
        }

        bool copyFileFail = false;
        if (foundNo_ > 0)
        {
            std::stringstream ss;
            ss << "Found " << foundNo_ << " log files.";
            Logger::getInstance()->FnLog(ss.str(), "", "OPR");

            // Create the mount poin directory if doesn't exist
            std::string mountPoint = "/mnt/logbackup";
            std::string sharedFolderPath = operation::getInstance()->tParas.gsLogBackFolder;
            std::replace(sharedFolderPath.begin(), sharedFolderPath.end(), '\\', '/');
            std::string username = IniParser::getInstance()->FnGetCentralUsername();
            std::string password = IniParser::getInstance()->FnGetCentralPassword();

            if (!std::filesystem::exists(mountPoint))
            {
                std::error_code ec;
                if (!std::filesystem::create_directories(mountPoint, ec))
                {
                    Logger::getInstance()->FnLog(("Failed to create " + mountPoint + " directory : " + ec.message()), "", "OPR");
                    throw std::runtime_error(("Failed to create " + mountPoint + " directory : " + ec.message()));
                }
                else
                {
                    Logger::getInstance()->FnLog(("Successfully to create " + mountPoint + " directory."), "", "OPR");
                }
            }
            else
            {
                Logger::getInstance()->FnLog(("Mount point directory: " + mountPoint + " exists."), "", "OPR");
            }

            // Mount the shared folder
            std::string mountCommand = "sudo mount -t cifs " + sharedFolderPath + " " + mountPoint +
                                        " -o username=" + username + ",password=" + password;
            int mountStatus = std::system(mountCommand.c_str());
            if (mountStatus != 0)
            {
                Logger::getInstance()->FnLog(("Failed to mount " + mountPoint), "", "OPR");
                throw std::runtime_error("Failed to mount " + mountPoint);
            }
            else
            {
                Logger::getInstance()->FnLog(("Successfully to mount " + mountPoint), "", "OPR");
            }

            // Copy files to mount folder
            for (const auto& entry : std::filesystem::directory_iterator(logFilePath))
            {
                if ((entry.path().filename().string().find(todayDateStr) != std::string::npos) &&
                    (entry.path().extension() == ".log"))
                {
                    std::error_code ec;
                    std::filesystem::copy(entry.path(), mountPoint / entry.path().filename(), std::filesystem::copy_options::overwrite_existing, ec);
                    
                    if (!ec)
                    {
                        std::stringstream ss;
                        ss << "Copy file : " << entry.path() << " successfully.";
                        Logger::getInstance()->FnLog(ss.str(), "", "OPR");
                    }
                    else
                    {
                        std::stringstream ss;
                        ss << "Failed to copy log file : " << entry.path();
                        Logger::getInstance()->FnLog(ss.str(), "", "OPR");
                        copyFileFail = true;
                        break;
                    }
                }
            }

            // Unmount the shared folder
            std::string unmountCommand = "sudo umount " + mountPoint;
            int unmountStatus = std::system(unmountCommand.c_str());
            if (unmountStatus != 0)
            {
                Logger::getInstance()->FnLog(("Failed to unmount " + mountPoint), "", "OPR");
                throw std::runtime_error("Failed to unmount " + mountPoint);
            }
            else
            {
                Logger::getInstance()->FnLog(("Successfully to unmount " + mountPoint), "", "OPR");
            }
        }
        else
        {
            Logger::getInstance()->FnLog("No Log files to upload.", "", "OPR");
            throw std::runtime_error("No Log files to upload.");
        }

        if (!copyFileFail)
        {
            SendMsg2Monitor("314", "99");
        }
        else
        {
            SendMsg2Monitor("314", "98");
        }
    }
    catch (const std::exception& e)
    {
        std::stringstream ss;
        ss << __func__ << " Exception: " << e.what();
        Logger::getInstance()->FnLog(ss.str(), "", "OPR");
        SendMsg2Monitor("314", "98");
    }
    catch (...)
    {
        std::stringstream ss;
        ss << __func__ << " Unknown Exception.";
        Logger::getInstance()->FnLog(ss.str(), "", "OPR");
        SendMsg2Monitor("314", "98");
    }
}

bool operation::copyFiles(const std::string& mountPoint, const std::string& sharedFolderPath, 
                        const std::string& username, const std::string& password, const std::string& outputFolderPath)
{
    // Create the mount poin directory if doesn't exist
    if (!std::filesystem::exists(mountPoint))
    {
        std::error_code ec;
        if (!std::filesystem::create_directories(mountPoint, ec))
        {
            Logger::getInstance()->FnLog(("Failed to create " + mountPoint + " directory : " + ec.message()), "", "OPR");
            return false;
        }
        else
        {
            Logger::getInstance()->FnLog(("Successfully to create " + mountPoint + " directory."), "", "OPR");
        }
    }
    else
    {
        Logger::getInstance()->FnLog(("Mount point directory: " + mountPoint + " exists."), "", "OPR");
    }

    // Mount the shared folder
    std::string mountCommand = "sudo mount -t cifs " + sharedFolderPath + " " + mountPoint +
                                " -o username=" + username + ",password=" + password;
    int mountStatus = std::system(mountCommand.c_str());
    if (mountStatus != 0)
    {
        Logger::getInstance()->FnLog(("Failed to mount " + mountPoint), "", "OPR");
        return false;
    }
    else
    {
        Logger::getInstance()->FnLog(("Successfully to mount " + mountPoint), "", "OPR");
    }

    // Create the output folder if it doesn't exist
    if (!std::filesystem::exists(outputFolderPath))
    {
        std::error_code ec;
        if (!std::filesystem::create_directories(outputFolderPath, ec))
        {
            Logger::getInstance()->FnLog(("Failed to create " + outputFolderPath + " directory : " + ec.message()), "", "OPR");
            umount(mountPoint.c_str()); // Unmount if folder creation fails
            return false;
        }
        else
        {
            Logger::getInstance()->FnLog(("Successfully to create " + outputFolderPath + " directory."), "", "OPR");
        }
    }
    else
    {
        Logger::getInstance()->FnLog(("Output folder directory : " + outputFolderPath + " exists."), "", "OPR");
    }

    // Copy files to mount point
    bool foundIni = false;
    std::filesystem::path folder(mountPoint);
    if (std::filesystem::exists(folder) && std::filesystem::is_directory(folder))
    {
        for (const auto& entry : std::filesystem::directory_iterator(folder))
        {
            std::string filename = entry.path().filename().string();

            if (std::filesystem::is_regular_file(entry)
                && (filename.size() >= 4) && (filename == "LinuxPBS.ini"))
            {
                foundIni = true;
                std::filesystem::path dest_file = outputFolderPath / entry.path().filename();
                std::filesystem::copy(entry.path(), dest_file, std::filesystem::copy_options::overwrite_existing);

                std::stringstream ss;
                ss << "Copy " << entry.path() << " to " << dest_file << " successfully";
                Logger::getInstance()->FnLog(ss.str(), "", "OPR");
            }
        }
    }
    else
    {
        Logger::getInstance()->FnLog("Folder doesn't exist or is not a directory.", "", "OPR");
        umount(mountPoint.c_str());
        return false;
    }

    // Unmount the shared folder
    std::string unmountCommand = "sudo umount " + mountPoint;
    int unmountStatus = std::system(unmountCommand.c_str());
    if (unmountStatus != 0)
    {
        Logger::getInstance()->FnLog(("Failed to unmount " + mountPoint), "", "OPR");
        return false;
    }
    else
    {
        Logger::getInstance()->FnLog(("Successfully to unmount " + mountPoint), "", "OPR");
    }

    if (!foundIni)
    {
        Logger::getInstance()->FnLog("Ini file not found.", "", "OPR");
        return false;
    }

    return true;
}

bool operation::CopyIniFile(const std::string& serverIpAddress, const std::string& stationID)
{
    if ((!serverIpAddress.empty()) && (!stationID.empty()))
    {
        std::string sharedFilePath = "//" + serverIpAddress + "/carpark/LinuxPBS/Ini/Stn" + stationID;

        std::stringstream ss;
        ss << "Ini Shared File Path : " << sharedFilePath;
        Logger::getInstance()->FnLog(ss.str(), "", "OPR");

        return copyFiles("/mnt/ini", sharedFilePath, IniParser::getInstance()->FnGetCentralUsername(), IniParser::getInstance()->FnGetCentralPassword(), "/home/root/carpark/Ini");
    }
    else
    {
        Logger::getInstance()->FnLog("Server IP address or station ID empty.", "", "OPR");
        return false;
    }
}

void operation::SendMsg2Monitor(string cmdcode,string dstr)
{
    if (m_Monitorudp->FnGetMonitorStatus())
    {
        string str="["+ gtStation.sPCName +"|"+to_string(gtStation.iSID)+"|"+cmdcode+"|";
        str+=dstr+"|]";
        m_Monitorudp->send(str);
        //----
        writelog ("Message to Monitor: " + str,"OPR");
    }
}

void operation::SendMsg2Server(string cmdcode,string dstr)
{
	string str="["+ gtStation.sName+"|"+to_string(gtStation.iSID)+"|"+cmdcode+"|";
	str+=dstr+"|]";
	m_udp->send(str);
    //----
    writelog ("Message to PMS: " + str,"OPR");
}

int operation::CheckSeason(string sIU,int iInOut)
{
   int iRet;
   string sMsg;
   string sLCD;
   iRet = db::getInstance()->isvalidseason(sIU,iInOut,gtStation.iZoneID);
   //showLED message
   if (iRet != 8 ) {
        FormatSeasonMsg(iRet, sIU, sMsg, sLCD);
   } 
   return iRet;
}

void operation::writelog(string sMsg, string soption)
{
    std::stringstream dbss;
	dbss << sMsg;
    Logger::getInstance()->FnLog(dbss.str(), "", soption);

}

void operation::HandlePBSError(EPSError iEPSErr, int iErrCode)
{

    string sErrMsg = "";
    string sCmd = "";
    
    switch(iEPSErr){
        case PrinterNoError:
        {   
            if (tPBSError[1].ErrNo < 0){
                sCmd = "04";
                sErrMsg = "Printer OK";
            }
            tPBSError[1].ErrNo = 0;
            break;
        }
        case PrinterError:
        {
            tPBSError[1].ErrNo = -1;
            tPBSError[1].ErrMsg = "Printer Error";
            sCmd = "04";
            sErrMsg = tPBSError[1].ErrMsg;
            break;
        }
        case PrinterNoPaper:
        {
            tPBSError[1].ErrNo = -2;
            tPBSError[1].ErrMsg = "Printer Paper Low";
            sCmd = "04";
            sErrMsg = tPBSError[1].ErrMsg;
            break;
        }
        case DBNoError:
        {
            tPBSError[2].ErrNo = 0;
            break;
        }
        case DBFailed:
        {
            tPBSError[2].ErrNo = -1;
            break;
        }
        case DBUpdateFail:
        {
            tPBSError[2].ErrNo = -2;
            break;
        }
        case UPOSNoError:
        {
            if (tPBSError[4].ErrNo < 0){
                sCmd = "06";
                sErrMsg = "UPOS OK";
            }
            tPBSError[4].ErrNo = 0;
            break;
        }
        case UPOSError:
        {
            tPBSError[4].ErrNo = -1;
            tPBSError[4].ErrMsg = "UPOS Error";
            sCmd = "06";
            sErrMsg = tPBSError[4].ErrMsg;
            break;
        }
        case TariffError:
        {
            tPBSError[5].ErrNo = -1;     
            sCmd = "08";
            sErrMsg = "5Tariff Error";
            break;
        }
        case TariffOk:
        {
            tPBSError[5].ErrNo = 0 ;    
            sCmd = "08";
            sErrMsg = "5Tariff OK";
            break;
        }
        case HolidayError:
        {
            tPBSError[5].ErrNo = -2;
            sCmd = "08";
            sErrMsg = "5No Holiday Set";
            break;
        }
        case HolidayOk:
        {
            tPBSError[5].ErrNo = 0;    
            sCmd = "08";
            sErrMsg = "5Holiday OK";
            break;
        }
        case DIOError:
        {
            tPBSError[6].ErrNo = -1;
            sCmd = "08";
            sErrMsg = "6DIO Error";
            break;
        }
        case DIOOk:
        {
            tPBSError[6].ErrNo = 0;
            sCmd = "08";
            sErrMsg = "6DIO OK";
            break;
        }
        case LoopAHang:
        {
            tPBSError[7].ErrNo = -1;
            sCmd = "08";
            sErrMsg = "7Loop A Hang";
            break;
        }
        case LoopAOk:
        {
            tPBSError[7].ErrNo = 0;
            sCmd = "08";
            sErrMsg = "7Loop A OK";
            break;
        }
        case SDoorError:
        {
            tPBSError[11].ErrNo = -1;
            tPBSError[11].ErrMsg= "Station door open";
            sCmd = "71";
            sErrMsg = tPBSError[11].ErrMsg;
            break;
        }
        case SDoorNoError:
        {
            tPBSError[11].ErrNo = 0;
            sCmd = "71";
            sErrMsg = "Station door close";
            break;
        }
        case BDoorError:
        {
            tPBSError[12].ErrNo = -1;
            tPBSError[12].ErrMsg= "barrier door open";
            sCmd = "72";
            sErrMsg = tPBSError[12].ErrMsg;
            break;
        }
        case BDoorNoError:
        {
            tPBSError[12].ErrNo = 0;
            tPBSError[12].ErrMsg= "barrier door close";
            sCmd = "72";
            sErrMsg = tPBSError[12].ErrMsg;
            break;
        }
        case ReaderNoError:
        {
            tPBSError[iReader].ErrNo = 0;
            sCmd= "05";
            tPBSError[iReader].ErrMsg= "Card Reader OK";
            break;
        }
        case ReaderError:
        {
            tPBSError[iReader].ErrNo = -1;
            sCmd = "05";
            tPBSError[iReader].ErrMsg= "Card Reader Error";
            break;
        }
        case BarrierStatus:
        {
            tPBSError[iBarrierStatus].ErrNo = iErrCode;
            sCmd = "08";
            switch (iErrCode)
            {
                case 0:
                {
                    tPBSError[iBarrierStatus].ErrMsg = "Barrier Status: Closed";
                    sErrMsg = "9Barrier Status: Closed";
                    break;
                }
                case 1:
                {
                    tPBSError[iBarrierStatus].ErrMsg = "Barrier Status: Open";
                    sErrMsg = "9Barrier Status: Open";
                    break;
                }
                case 2:
                {
                    tPBSError[iBarrierStatus].ErrMsg = "Barrier Status: Open Too Long";
                    sErrMsg = "9Barrier Status: Open Too Long";
                    break;
                }
                case 3:
                {
                    tPBSError[iBarrierStatus].ErrMsg = "Barrier Status: Arm Drop Down";
                    sErrMsg = "9Barrier Status: Arm Drop Down";
                    SendMsg2Server("90", ",,,,,barrierarmdrop");
                    break;
                }
                case 4:
                {
                    tPBSError[iBarrierStatus].ErrMsg = "Barrier Status: Fail To Open";
                    sErrMsg = "9Barrier Status: Fail To Open";
                    break;
                }
                case 5:
                {
                    tPBSError[iBarrierStatus].ErrMsg = "Barrier Status: Fail To Close";
                    sErrMsg = "9Barrier Status: Fail To Close";
                    break;
                }
                default:
                {
                    tPBSError[iBarrierStatus].ErrMsg = "Barrier Status: Unknown";
                    sErrMsg = "9Barrier Status: Unknown";
                    break;
                }
            }
        }
        default:
            break;
    }
    
    if (sErrMsg != "" ){
        writelog (sErrMsg, "OPR");
        SendMsg2Server(sCmd, sErrMsg); 
        //-------
        Sendmystatus();   
    }

}

int operation::GetVTypeFromLoop()
{
    //car: 1 M/C: 7 Lorry: 4
    int ret = 1;
    if (operation::getInstance()->tProcess.gbLoopAIsOn == true && operation::getInstance()->tProcess.gbLoopBIsOn == true)
    {
        writelog ("Vehicle Type is car.", "OPR");
        ret = 1;
    }
    else if (operation::getInstance()->tProcess.gbLoopAIsOn == true || operation::getInstance()->tProcess.gbLoopBIsOn == true)
    {
        writelog ("Vehicle Type is MortorCycle.", "OPR");
        ret = 7;
    }
    else if (operation::getInstance()->tProcess.gbLoopAIsOn == true && operation::getInstance()->tProcess.gbLorrySensorIsOn == true)
    {
        writelog ("Vehicle Type is Lorry.", "OPR");
        ret = 4;
    }
    return ret;

}

void operation::SaveEntry()
{
    int iRet;
    std::string sLPRNo = "";
    
    if (tEntry.sIUTKNo== "") return;
    writelog ("Save Entry trans:"+ tEntry.sIUTKNo, "OPR");

    iRet = db::getInstance()->insertentrytrans(tEntry);
    //----
    if (iRet == iDBSuccess)
    {
        tProcess.setLastIUNo(tEntry.sIUTKNo);
        tProcess.setLastIUEntryTime(std::chrono::steady_clock::now());
    }
    //-------
    tPBSError[iDB].ErrNo = (iRet == iDBSuccess) ? 0 : (iRet == iCentralFail) ? -1 : -2;

    if ((tEntry.sLPN[0] != "")|| (tEntry.sLPN[1] != ""))
	{
		if((tEntry.iTransType == 7) || (tEntry.iTransType == 8) || (tEntry.iTransType == 22))
		{
			sLPRNo = tEntry.sLPN[1];
		}
		else
		{
			sLPRNo = tEntry.sLPN[0];
		}
	}

    std::string sMsg2Send = (iRet == iDBSuccess) ? "Entry OK" : (iRet == iCentralFail) ? "Entry Central Failed" : "Entry Local Failed";

    sMsg2Send = tEntry.sIUTKNo + ",,," + sLPRNo + "," + std::to_string(tProcess.giShowType) + "," + sMsg2Send;

    if (tEntry.iStatus == 0) {
        SendMsg2Server("90", sMsg2Send);
    }
    tProcess.gbsavedtrans = true;
}

void operation::ShowTotalLots(std::string totallots, std::string LEDId)
{
    if (LEDManager::getInstance()->getLED(getSerialPort(std::to_string(tParas.giCommportLED401))) != nullptr)
    {
        writelog ("Total Lot:"+ totallots,"OPR");
       
        LEDManager::getInstance()->getLED(getSerialPort(std::to_string(tParas.giCommportLED401)))->FnLEDSendLEDMsg(LEDId, totallots, LED::Alignment::RIGHT);
    }
}

void operation::FormatSeasonMsg(int iReturn, string sNo, string sMsg, string sLCD, int iExpires)
 {
    
    std::string sExp;
    int i;
    int giSeasonTransType;
    std::string sMsgPartialSeason;

    if (gtStation.iType == tientry){
         tEntry.iTransType = GetSeasonTransType(tEntry.iVehicleType,std::stoi(tSeason.rate_type), tEntry.iTransType);
         giSeasonTransType = tEntry.iTransType;
    } 
    else {
         tExit.iTransType = GetSeasonTransType(tExit.iVehicleType,std::stoi(tSeason.rate_type), tExit.iTransType);
        giSeasonTransType = tExit.iTransType;
    }
    tProcess.giShowType = 0;

    if (giSeasonTransType > 49) {
        sMsgPartialSeason = db::getInstance()->GetPartialSeasonMsg(giSeasonTransType);
        writelog("partial season msg:" + sMsgPartialSeason + ", trans type: " + std::to_string(giSeasonTransType),"OPR");
    }
    if (sMsgPartialSeason.empty()) {
        sMsgPartialSeason = "Season";
    }

    switch (iReturn) {
            case -1: 
                writelog("DB error when Check season", "OPR");
                break;
            case 0:  
                sMsg = tMsg.Msg_SeasonInvalid[0];
                sLCD = tMsg.Msg_SeasonInvalid[1];
                break;
            case 1:  
                if (gtStation.iType == tientry) {
                    sMsg = tMsg.Msg_ValidSeason[0];
                    sLCD = tMsg.Msg_ValidSeason[1];
                } else{
                    sMsg = tExitMsg.MsgExit_XValidSeason[0];
                    sLCD = tExitMsg.MsgExit_XValidSeason[1];
                } 
                break;
            case 2: 
                sMsg = tMsg.Msg_SeasonExpired[0];
                sLCD = tMsg.Msg_SeasonExpired[1];
                writelog("Season Expired", "OPR");
                break;
            case 3: 
                sMsg = tMsg.Msg_SeasonTerminated[0];
                sLCD = tMsg.Msg_SeasonTerminated[1];
                writelog ("Season terminated", "OPR");
                break;
            case 4:  
                sMsg = tMsg.Msg_SeasonBlocked[0];
                sLCD = tMsg.Msg_SeasonBlocked[1];
                writelog ("Season Blocked", "OPR");
                break;
            case 5:  
                sMsg = tMsg.Msg_SeasonInvalid[0];
                sLCD = tMsg.Msg_SeasonInvalid[1];
                writelog ("Season Lost", "OPR");
                break;
            case 6:
                sMsg = tMsg.Msg_SeasonPassback[0];
                sLCD = tMsg.Msg_SeasonPassback[1];
                writelog ("Season Passback", "OPR");
                break;
            case 7:  
                sMsg = tMsg.Msg_SeasonNotStart[0];
                sLCD = tMsg.Msg_SeasonNotStart[1];
                writelog ("Season Not Start", "OPR");
                break;
            case 8: 
                sMsg = "Wrong Season Type";
                sLCD = "Wrong Season Type";
                writelog ("Wrong Season Type", "OPR");
                break;
            case 9: 
                sMsg = "Complimentary";
                sLCD = "Complimentary";
                writelog ("Complimentary!", "OPR");
                break;
            case 10:     
                sMsg = tMsg.Msg_SeasonAsHourly[0];
                sLCD = tMsg.Msg_SeasonAsHourly[1];
                writelog ("Season As Hourly", "OPR");
                break;
            case 11:     
                sMsg = tMsg.Msg_ESeasonWithinAllowance[0];
                sLCD = tMsg.Msg_ESeasonWithinAllowance[1];
                writelog ("Season within allowance", "OPR");
                break;
            case 12:
                sMsg = tExitMsg.MsgExit_MasterSeason[0];
                sLCD = tExitMsg.MsgExit_MasterSeason[1];
                writelog ("Master Season", "OPR");
                break;
            case 13:     
                sMsg = tMsg.Msg_WholeDayParking[0];
                sLCD = tMsg.Msg_WholeDayParking[1];
                writelog ("Whole Day Season", "OPR");
                break;
            default:
                break;
        }

        size_t pos = sMsg.find("Season");
        if (pos != std::string::npos) sMsg.replace(pos, 6, sMsgPartialSeason);
        pos=sLCD.find("Season");
        if (pos != std::string::npos) sLCD.replace(pos, 6, sMsgPartialSeason);
        
        if (iReturn = 1 && std::stoi(tSeason.rate_type) != 0) {
            sMsg = sMsgPartialSeason;
            sLCD = sMsgPartialSeason;
        }
        
        ShowLEDMsg(sMsg,sLCD);
        
        if (iReturn != 1 || std::stoi(tSeason.rate_type) != 0) std::this_thread::sleep_for(std::chrono::milliseconds(500));

}

void operation::ManualOpenBarrier(bool bPMS)
{
     if (bPMS == true) writelog ("Manual open barrier by PMS", "OPR");
     else writelog ("Manual open barrier by operator", "OPR");
     //------------
     if (gtStation.iType == tientry){
	    tEntry.sEntryTime = Common::getInstance()->FnGetDateTimeFormat_yyyy_mm_dd_hh_mm_ss();
        tEntry.iStatus = 4;
        //---------
        m_db->AddRemoteControl(std::to_string(gtStation.iSID),"Manual open barrier","Auto save for IU:"+tEntry.sIUTKNo);
        SaveEntry();
        Openbarrier();
     }else{
        if (tExit.sExitTime == "") tExit.sExitTime = Common::getInstance()->FnGetDateTimeFormat_yyyy_mm_dd_hh_mm_ss();
        m_db->AddRemoteControl(std::to_string(gtStation.iSID),"Manual open barrier","Auto save for IU:"+tExit.sIUNo);
        CloseExitOperation(Manualopen);
     }
}

void operation::ManualCloseBarrier()
{
    writelog ("Manual Close barrier.", "OPR");
    m_db->AddRemoteControl(std::to_string(gtStation.iSID),"Manual close barrier","");
    
    closeBarrier();
}

int operation:: GetSeasonTransType(int VehicleType, int SeasonType, int TransType)
 {
    // VehicleType: 0=car, 1=lorry, 2=motorcycle
    // SeasonType(rate_type of season_mst): 3=Day, 4=Night, 5=GRO, 6=Park & Ride
    // TransType: TransType for Wholeday season

    //writelog ("Season Rate Type:"+std::to_string(SeasonType),"OPR");

    if (SeasonType == 3) {
        if (VehicleType == 0) {
            return 50; // car day season
        } else if (VehicleType == 1) {
            return 51; // Lorry day season
        } else if (VehicleType == 2) {
            return 52; // motorcycle day season
        }
    } else if (SeasonType == 4) {
        if (VehicleType == 0) {
            return 53; // car night season
        } else if (VehicleType == 1) {
            return 54; // Lorry night season
        } else if (VehicleType == 2) {
            return 55; // motorcycle night season
        }
    } else if (SeasonType == 5) {
        if (VehicleType == 0) {
            return 56; // car GRO season
        } else if (VehicleType == 1) {
            return 57; // Lorry GRO season
        } else if (VehicleType == 2) {
            return 58; // motorcycle GRO season
        }
    } else if (SeasonType == 6) {
        if (VehicleType == 0) {
            return 59; // Car Park Ride Season
        } else if (VehicleType == 1) {
            return 60; // Lorry Park Ride season
        } else if (VehicleType == 2) {
            return 61; // motorcycle Park Ride Season
        }
    } else if (SeasonType == 7) {
        if (VehicleType == 0 || VehicleType == 14) {
            return 62; // Car Handicapped Season
        } else if (VehicleType == 1) {
            return 63; // Lorry Handicapped season
        } else if (VehicleType == 2) {
            return 64; // motorcycle Handicapped Season
        }
    } else if (SeasonType == 8) {
        if (VehicleType == 0) {
            return 65; // Staff A Car
        } else if (VehicleType == 1) {
            return 66; // staff A Lorry
        } else if (VehicleType == 2) {
            return 67; // Staff A Motorcycle
        }
    } else if (SeasonType == 9) {
        if (VehicleType == 0) {
            return 68; // Staff B Car
        } else if (VehicleType == 1) {
            return 69; // staff B Lorry
        } else if (VehicleType == 2) {
            return 70; // Staff B Motorcycle
        }
    } else if (SeasonType == 10) {
        if (VehicleType == 0) {
            return 71; // Staff C Car
        } else if (VehicleType == 1) {
            return 72; // staff C Lorry
        } else if (VehicleType == 2) {
            return 73; // Staff C Motorcycle
        }
    } else if (SeasonType == 11) {
        if (VehicleType == 0) {
            return 74; // Staff D Car
        } else if (VehicleType == 1) {
            return 75; // staff D Lorry
        } else if (VehicleType == 2) {
            return 76; // Staff D Motorcycle
        }
    } else if (SeasonType == 12) {
        if (VehicleType == 0) {
            return 77; // Staff E Car
        } else if (VehicleType == 1) {
            return 78; // staff E Lorry
        } else if (VehicleType == 2) {
            return 79; // Staff E Motorcycle
        }
    } else if (SeasonType == 13) {
        if (VehicleType == 0) {
            return 80; // Staff F Car
        } else if (VehicleType == 1) {
            return 81; // staff F Lorry
        } else if (VehicleType == 2) {
            return 82; // Staff F Motorcycle
        }
    } else if (SeasonType == 14) {
        if (VehicleType == 0) {
            return 83; // Staff G Car
        } else if (VehicleType == 1) {
            return 84; // staff G Lorry
        } else if (VehicleType == 2) {
            return 85; // Staff G Motorcycle
        }
    } else if (SeasonType == 15) {
        if (VehicleType == 0) {
            return 86; // Staff H Car
        } else if (VehicleType == 1) {
            return 87; // staff H Lorry
        } else if (VehicleType == 2) {
            return 88; // Staff H Motorcycle
        }
    } else if (SeasonType == 16) {
        if (VehicleType == 0) {
            return 89; // Staff I Car
        } else if (VehicleType == 1) {
            return 90; // staff I Lorry
        } else if (VehicleType == 2) {
            return 91; // Staff I Motorcycle
        }
    } else if (SeasonType == 17) {
        if (VehicleType == 0) {
            return 92; // Staff J Car
        } else if (VehicleType == 1) {
            return 93; // staff J Lorry
        } else if (VehicleType == 2) {
            return 94; // Staff J Motorcycle
        }
    } else if (SeasonType == 18) {
        if (VehicleType == 0) {
            return 95; // Staff K Car
        } else if (VehicleType == 1) {
            return 96; // staff K Lorry
        } else if (VehicleType == 2) {
            return 97; // Staff K Motorcycle
        }
    } else if (TransType == 9) {
        return 9;
    } 
    return TransType + 1; // Whole day season
}

void operation:: EnableCashcard(bool bEnable)
{
    
    if  (bEnable == tProcess.sEnableReader) return;
    
    tProcess.sEnableReader = bEnable;

    if (bEnable == true){
        if (gtStation.iType == tiExit && tExit.sRedeemNo == "")  BARCODE_READER::getInstance()->FnBarcodeStartRead();
    }else 
    {
      BARCODE_READER::getInstance()->FnBarcodeStopRead();     
    } 

}

void operation::ProcessBarcodeData(string sBarcodedata)
{
    BARCODE_READER::getInstance()->FnBarcodeStopRead();
    ticketScan(sBarcodedata);
}

void operation::ReceivedLPR(Lpr::CType CType,string LPN, string sTransid, string sImageLocation)
{
    writelog ("Received Trans ID: "+sTransid + " LPN: "+ LPN ,"OPR");
    writelog ("Send Trans ID: "+ tEntry.gsTransID, "OPR");

    int i = static_cast<int>(CType);

    if (tEntry.gsTransID == sTransid && tProcess.gbLoopApresent.load() == true && tProcess.gbsavedtrans == false)
    {
       if (gtStation.iType == tientry) {
            tEntry.sLPN[i]=LPN;
       }else {
             tExit.sLPN[i]=LPN;
       }

    }
    else
    {
        if (gtStation.iType == tientry) db::getInstance()->updateEntryTrans(LPN,sTransid);
        else db::getInstance()->updateExitTrans(LPN,sTransid);
    }
}

void operation::PrintTR(bool bForSeason)
{
    
    std::string sSerialNo;

    tProcess.glLastSerialNo = tProcess.glLastSerialNo + 1;
    std::string combinedString = std::to_string(tProcess.glLastSerialNo) + std::to_string(gtStation.iSID);
    std::ostringstream formattedString;
    formattedString << std::setw(9) << std::setfill('0') << combinedString;
    sSerialNo = formattedString.str();
    // Temp: will do in future - SaveSerialNo

    if (gtStation.iType == tientry)
    {
        if (tProcess.giEntryDebit < 2)
        {
            tEntry.sSerialNo = tParas.gsHdTk + sSerialNo;
            tEntry.sEntryTime = Common::getInstance()->FnGetDateTimeFormat_yyyy_mm_dd_hh_mm_ss();
            // Temp: will do in future - i = CheckVType
            // Temp: will do in future - tEntry.sIUTKNo = cPrinter.bEncode(Format(Now, "yyyymmddHHmmss"), giStationID, i)
            // Temp: will do in future - tEntry.iTransType = i * 3  '0=car, 3=lorry, 6=M\cycle
        }
        else
        {
            tEntry.sReceiptNo = tParas.gsHdRec + sSerialNo;
        }
    }
    else
    {
       
        tExit.sReceiptNo = tParas.gsHdRec + sSerialNo;
        
    }

    std::string gsSite = tParas.gsSite;
    std::string gsCompany = tParas.gsCompany;
    std::string gsAddress = tParas.gsAddress;
    std::string gsZIP = tParas.gsZIP;
    std::string gsGSTNo = tParas.gsGSTNo;
    std::string gsTel = tParas.gsTel;
    std::string exitReceiptNo = "";
    std::string entrySerialNo = sSerialNo;
    std::string vehicleType = "";
    std::string iuNo = "";
    std::string cardNo = "";
    std::string entryTime = "";
    std::string exitTime = "";
    std::string parkTime = "";
    std::string amt = "";
    std::string cardBal = "";
    std::string owefee = "";
    std::string fee = "";
    std::string pm = "";
    std::string admin = "";
    std::string app = "";
    std::string tamt = "";
    std::string rdmamt = "";
    std::string rebateamt = "";
    std::string rebatebal = "";
    std::string rebatedate = "";
    std::string gstamt = "";

    std::vector<std::string> gsTR(operation::getInstance()->tTR.size());
    for (std::size_t i = 0; i < operation::getInstance()->tTR.size(); i++)
    {
        std::string gsTR_lowercase = operation::getInstance()->tTR[i].gsTR1.empty() ? "" : boost::algorithm::to_lower_copy(operation::getInstance()->tTR[i].gsTR1);

        if (gsTR_lowercase == "site")
        {
            gsTR[i] = operation::getInstance()->tTR[i].gsTR0 + gsSite;
        }
        else if (gsTR_lowercase == "comp")
        {
            gsTR[i] = operation::getInstance()->tTR[i].gsTR0 + gsCompany;
        }
        else if (gsTR_lowercase == "addr")
        {
            gsTR[i] = operation::getInstance()->tTR[i].gsTR0 + gsAddress;
        }
        else if (gsTR_lowercase == "zip")
        {
            gsTR[i] = operation::getInstance()->tTR[i].gsTR0 + gsZIP;
        }
        else if (gsTR_lowercase == "gstno")
        {
            gsTR[i] = operation::getInstance()->tTR[i].gsTR0 + gsGSTNo;
        }
        else if (gsTR_lowercase == "gsTel")
        {
            gsTR[i] = operation::getInstance()->tTR[i].gsTR0 + gsTel;
        }
        else if (gsTR_lowercase == "rno")
        {
            if ((tProcess.giEntryDebit == 2) && (gtStation.iType == tientry))
            {
                exitReceiptNo = tEntry.sReceiptNo;
            }
            else
            {
                exitReceiptNo = tExit.sReceiptNo;
            }
            gsTR[i] = operation::getInstance()->tTR[i].gsTR0 + " " + exitReceiptNo;
        }
        else if (gsTR_lowercase == "tno")
        {
            gsTR[i] = operation::getInstance()->tTR[i].gsTR0 + " " + entrySerialNo;
        }
        else if (gsTR_lowercase == "vtype")
        {
            if (gtStation.iType == tientry)
            {
                vehicleType = GetVTypeStr(tEntry.iTransType);
            }
            else
            {
                vehicleType = GetVTypeStr(tExit.iTransType);
            }
            gsTR[i] = operation::getInstance()->tTR[i].gsTR0 + " " + vehicleType;
        }
        else if (gsTR_lowercase == "itno")
        {
            if (gtStation.iType == tientry)
            {
                iuNo = tEntry.sIUTKNo;
            }
            else
            {
                iuNo = tExit.sIUNo;
                
            }
            gsTR[i] = operation::getInstance()->tTR[i].gsTR0 + " " + iuNo;
        }
        else if (gsTR_lowercase == "card")
        {
            if (tExit.bPayByEZPay.load() == false)
            {
                
                if ((tProcess.giEntryDebit == 2) && (gtStation.iType == tientry))
                {
                    cardNo = tEntry.sCardNo;
                }
                else
                {
                    if (tExit.sPaidAmt > 0)
                    {
                        cardNo = tExit.sCardNo;
                    }
                }
            }
            gsTR[i] = operation::getInstance()->tTR[i].gsTR0 + " " + cardNo;
        }
        else if (gsTR_lowercase == "et")
        {
            if (gtStation.iType == tientry)
            {
                entryTime = Common::getInstance()->FnFormatDateTime(tEntry.sEntryTime, "%Y-%m-%d %H:%M:%S", "%d/%m/%Y %H:%M:%S");
            }
            else
            {
                if (tExit.sEntryTime == "")
                {
                    entryTime = " N/A";
                }
                else
                {
                    entryTime = Common::getInstance()->FnFormatDateTime(tExit.sEntryTime, "%Y-%m-%d %H:%M:%S", "%d/%m/%Y %H:%M:%S");
                }
            }
            gsTR[i] = operation::getInstance()->tTR[i].gsTR0 + " " + entryTime;
        }
        else if (gsTR_lowercase == "pt")
        {
            exitTime = Common::getInstance()->FnFormatDateTime(tExit.sExitTime, "%Y-%m-%d %H:%M:%S", "%d/%m/%Y %H:%M:%S");
            gsTR[i] = operation::getInstance()->tTR[i].gsTR0 + " " + exitTime;
        }
        else if (gsTR_lowercase == "pkt")
        {
            if (tExit.lParkedTime > 0) {
                parkTime = db::getInstance()->CalParkedTime(tExit.lParkedTime);
                gsTR[i] = operation::getInstance()->tTR[i].gsTR0 + " " + parkTime;
            }else  gsTR[i] = operation::getInstance()->tTR[i].gsTR0 + " " + "N/A";
        }
        else if (gsTR_lowercase == "amt")
        {
            std::string payType = "";
            try
            {
                std::ostringstream formattedStream;
                formattedStream << std::fixed << std::setprecision(2) << tExit.sPaidAmt;

                float formattedAmt = std::stof(formattedStream.str());

                if (formattedAmt > 0)
                {
                    if (tProcess.giEntryDebit == 0)
                    {
                        //amt = std::to_string(formattedAmt);
                        amt = Common::getInstance()->SetFeeFormat(tExit.sPaidAmt);
                    }
                    else
                    {
                        float sumAmt = tExit.sPaidAmt + tExit.sPrePaid;

                        std::ostringstream formattedSumAmtStream;
                        formattedSumAmtStream << std::fixed << std::setprecision(2) << sumAmt;

                        float formattedSumAmt = std::stof(formattedSumAmtStream.str());

                           // amt = std::to_string(formattedSumAmt);
                        amt = Common::getInstance()->SetFeeFormat(sumAmt);
                    }
                }

                if (tExit.bPayByEZPay.load() == true)
                {
                    payType = " (by EZPay)";
                }
                
            }
            catch (const std::exception& ex)
            {
                Logger::getInstance()->FnLog(std::string("String to float exception error: ") + ex.what(), "", "OPR");
            }
            gsTR[i] = operation::getInstance()->tTR[i].gsTR0 + " $" + amt + payType;
        }
        else if (gsTR_lowercase == "bal")
        {
            if (tExit.sPaidAmt > 0)
            {
                if ((tExit.bPayByEZPay.load() == false) && (tProcess.gfLastCardBal > 0))
                {
                    std::stringstream ss;
                    ss << "card type: " << tExit.iCardType << ", fsCardBal: " << tProcess.gfLastCardBal;
                    Logger::getInstance()->FnLog(ss.str(), "", "OPR");
                        
                    std::ostringstream formattedCardBalStream;
                    formattedCardBalStream << std::fixed << std::setprecision(2) << tProcess.gfLastCardBal;

                    cardBal = formattedCardBalStream.str();
                    
                }
            }
            gsTR[i] = operation::getInstance()->tTR[i].gsTR0 + " $" + cardBal;
        }
        else if (gsTR_lowercase == "owefee")
        {
            if (gtStation.iSubType == iXwithVEPay)
            {
                if (tExit.sOweAmt >= 0)
                {
                    // Temp: will do in futue - gsTR(i) = "(" & gtStations(gtStations(tExit.iEntryID).iVExitID).sZoneName & ") " & gsTR0(i) & " $" & Format(tExit.sOweAmt, "0.00")
                }
                else
                {
                    // Temp: will do in futue - gsTR(i) = "(" & gtStations(gtStations(tExit.iEntryID).iVExitID).sZoneName & ") " & gsTR0(i) & " $" & Format(0, "0.00")
                }
            }
            gsTR[i] = operation::getInstance()->tTR[i].gsTR0 + " $" + owefee;
        }
        else if (gsTR_lowercase == "fee")
        {
            if (tProcess.giEntryDebit == 0)
            {
                std::stringstream ss;
                ss << "tExit.sFee: " << tExit.sFee << ", tExit.sPrePaid: " << tExit.sPrePaid; 
                Logger::getInstance()->FnLog(ss.str(), "", "OPR");

                if (gtStation.iSubType == iXwithVEPay)
                {
                    float sumFee = tExit.sFee + tExit.sPrePaid;
                    std::ostringstream formattedSumFeeStream;
                    formattedSumFeeStream << std::fixed << std::setprecision(2) << sumFee;
                    fee = formattedSumFeeStream.str();
                    gsTR[i] = "(" + gtStation.sZoneName + ")" + operation::getInstance()->tTR[i].gsTR0 + " $" + fee;
                }
                else
                {
                    std::ostringstream formattedFeeStream;
                    formattedFeeStream << std::fixed << std::setprecision(2) << tExit.sFee;
                    fee = formattedFeeStream.str();
                    gsTR[i] = operation::getInstance()->tTR[i].gsTR0 + " $" + fee;
                }
            }
            else
            {
                float sumFee = tExit.sFee + tExit.sPrePaid;
                std::ostringstream formattedSumFeeStream;
                formattedSumFeeStream << std::fixed << std::setprecision(2) << sumFee;
                fee = formattedSumFeeStream.str();
                gsTR[i] = operation::getInstance()->tTR[i].gsTR0 + " $" + fee;
            }
        }
        else if (gsTR_lowercase == "pm")
        {
            // Temp: will do in futue - pm = tSeason.sPaidMth
            gsTR[i] = operation::getInstance()->tTR[i].gsTR0 + pm;
        }
        else if (gsTR_lowercase == "admin")
        {
            // Temp: will do in futue - admin = Format(tSeason.sAdminFee, "0.00")
            gsTR[i] = operation::getInstance()->tTR[i].gsTR0 + admin;
        }
        else if (gsTR_lowercase == "app")
        {
            // Temp: will do in futue - app = Format(tSeason.sAppFee, "0.00")
            gsTR[i] = operation::getInstance()->tTR[i].gsTR0 + app;
        }
        else if (gsTR_lowercase == "tamt")
        {
            // Temp: will do in futue - tamt = Format(tSeason.sPaidAmt + tSeason.sAdminFee + tSeason.sAdminFee, "0.00")
            gsTR[i] = operation::getInstance()->tTR[i].gsTR0 + tamt;
        }
        else if (gsTR_lowercase == "rdmamt")
        {
            if (tExit.sRedeemAmt > 0)
            {
                std::ostringstream formattedRdmamtStream;
                formattedRdmamtStream << std::fixed << std::setprecision(2) << tExit.sRedeemAmt;
                rdmamt = formattedRdmamtStream.str();
                gsTR[i] = operation::getInstance()->tTR[i].gsTR0 + " $" + rdmamt;
            }
            else
            {
                gsTR[i] = operation::getInstance()->tTR[i].gsTR0 + " N/A";
            }
        }
        else if (gsTR_lowercase == "rebateamt")
        {
            if (tExit.sRebateAmt > 0)
            {
                std::ostringstream formattedRebateAmtStream;
                formattedRebateAmtStream << std::fixed << std::setprecision(2) << tExit.sRebateAmt;
                rebateamt = formattedRebateAmtStream.str();
                gsTR[i] = operation::getInstance()->tTR[i].gsTR0 + " $" + rebateamt;
            }
            else
            {
                gsTR[i] = operation::getInstance()->tTR[i].gsTR0 + " N/A";
            }
        }
        else if (gsTR_lowercase == "rebatebal")
        {
            if (tExit.sRebateAmt > 0)
            {
                std::ostringstream formattedRebateBalStream;
                formattedRebateBalStream << std::fixed << std::setprecision(2) << tExit.sRebateAmt;
                rebatebal = formattedRebateBalStream.str();
                gsTR[i] = operation::getInstance()->tTR[i].gsTR0 + " $" + rebatebal;
            }
            else
            {
                gsTR[i] = operation::getInstance()->tTR[i].gsTR0 + " N/A";
            }
        }
        else if (gsTR_lowercase == "rebatedate")
        {
            if (tExit.sRebateAmt > 0)
            {
                // Temp: will do in futue - gsTR(i) = gsTR0(i) & Format(tExit.sRebateDate, "dd/mm/yyyy")
            }
            else
            {
                gsTR[i] = operation::getInstance()->tTR[i].gsTR0 + " N/A";
            }
        }
        else if (gsTR_lowercase == "gstamt")
        {
            if (tExit.sGSTAmt == 0)
            {
                if ((gtStation.iType == tientry) && (tProcess.giEntryDebit == 2))
                {
                    std::ostringstream formattedGstAmtStream;
                    formattedGstAmtStream << std::fixed << std::setprecision(2) << tEntry.sGSTAmt;
                    gstamt = formattedGstAmtStream.str();
                }
                else
                {
                    if (tExit.sPaidAmt > 0)
                    {
                        std::ostringstream formattedGstAmtStream;
                        formattedGstAmtStream << std::fixed << std::setprecision(2) << tExit.sGSTAmt;
                        gstamt = formattedGstAmtStream.str();
                    }
                    else
                    {
                        float sumGst = tExit.sPrePaid * tParas.gfGSTRate / (1 + tParas.gfGSTRate);
                        std::ostringstream formattedSumGstAmtStream;
                        formattedSumGstAmtStream << std::fixed << std::setprecision(2) << sumGst;
                        gstamt = formattedSumGstAmtStream.str();
                    }
                }
            }
            else
            {
                std::ostringstream formattedGstAmtStream;
                formattedGstAmtStream << std::fixed << std::setprecision(2) << tExit.sGSTAmt;
                gstamt = formattedGstAmtStream.str();
            }
            gsTR[i] = operation::getInstance()->tTR[i].gsTR0 + " " + std::to_string(tParas.gfGSTRate * 100) + "% GST $" + gstamt;
        }
        else
        {
            gsTR[i] = operation::getInstance()->tTR[i].gsTR0;
        }
    }

    Printer::getInstance()->FnPrintLine("Clear Buffer", 99);

    for (std::size_t i = 0; i < gsTR.size(); i++)
    {
        //std::cout << gsTR[i] << std::endl;
        char F = gsTR[i][0];

        if (F == '@')
        {
            int lines = std::stoi(gsTR[i].substr(1));
            Printer::getInstance()->FnFeedLine(lines);
        }
        else if (F == '*')
        {
            std::string barcode = gsTR[i].substr(1);
            Printer::getInstance()->FnPrintBarCode(barcode, operation::getInstance()->tTR[i].giTRF, operation::getInstance()->tTR[i].giTRA, 20);
        }
        else
        {
            if (!gsTR[i].empty() && gsTR[i].find("N/A") == std::string::npos)
            {
                Printer::getInstance()->FnPrintLine(gsTR[i], operation::getInstance()->tTR[i].giTRF, operation::getInstance()->tTR[i].giTRA);
            }
        }
    }

    Printer::getInstance()->FnFullCut();
    //---- update receipt No
    m_db->updateExitReceiptNo(sSerialNo,std::to_string(gtStation.iSID)); 
}

void operation::DebitOK(const std::string& sIUNO, const std::string& sCardNo, 
                const std::string& sPaidAmt, const std::string& sBal,
                int iCardType, const std::string& sTopupAmt,
                DeviceType iDevicetype, const std::string& sTransTime)
{

    //---------
    tExit.sCardNo = sCardNo;
    if (sPaidAmt != "") tExit.sPaidAmt = GfeeFormat(std::stof(sPaidAmt));
    tExit.iCardType = iCardType;
    //--------
    tProcess.gsLastPaidTrans = tExit.sIUNo;
    tProcess.gbLastPaidStatus.store(true);
    tProcess.gsLastCardNo = sCardNo;
    if (sBal != "") tProcess.gfLastCardBal= GfeeFormat(std::stof(sBal));
    else tProcess.gfLastCardBal = 0;
    //-------
    tExit.giDeductionStatus = DeductionSuccessed;

    CloseExitOperation(DeductionOK);

}

std::string operation::GetVTypeStr(int iVType)
{
    std::string sVType = "";

    if ((iVType < 3) || (iVType == 20))
    {
        sVType = "Car";
    }
    else if ((iVType < 6) || (iVType == 21))
    {
        sVType = "Lorry";
    }
    else if ((iVType < 9) || (iVType == 22))
    {
        sVType = "M/Cycle";
    }
    else if (iVType == 33)
    {
        sVType = "Container";
    }
    else
    {
        sVType = "Undefined Vehicle Type";
    }

    return sVType;
}

 void operation::CheckIUorCardStatus(string sCheckNo, DeviceType iDevicetype,string sCardNo, int sCardType, float sCardBal)
 {
    // device type : 0 = PMS(EntryTime), 1 = Ant, 2 = LCSC, 3 = UPOS, 4 = CHU

    string gsCompareNo;
    int iRet;
    string sMsg;
    //--------
    if (tExit.giDeductionStatus == CardExpired || tExit.giDeductionStatus == CardFault) {
        if( tProcess.gsLastCardNo == sCheckNo) {
            writelog ("Fault card! Change another", "OPR");
            ShowLEDMsg("Card Fault!","Card Fault!");
            EnableCashcard(true);
            return;
        }
        tExit.giDeductionStatus = WaitingCard;
    }
    //-------- check paid status
    if (tExit.gbPaid.load() == true ) {
        writelog ("Paid already!","OPR");
        ShowLEDMsg("Paid already^Have a nice Day!","Paid already^Have a nice day!");
        EnableCashcard(false);
        Openbarrier();
        return;
    }
    //--------check balance
    if (GfeeFormat(sCardBal) != GfeeFormat(tProcess.gfLastCardBal) && sCardNo == tProcess.gsLastCardNo && tProcess.gbLastPaidStatus.load()  == false ) {
        tProcess.gfLastCardBal= GfeeFormat(sCardBal);
        EnableCashcard(false);
        if (tExit.sPaidAmt == 0){
            //----Loop A come again
            Openbarrier();
        }else {
            CloseExitOperation(BalanceChange);
         }
        return;
    }
    //---------
    if (tExit.giDeductionStatus == WaitingCard) {
        //CheckCardOK 
        iRet = m_db->CheckCardOK(sCardNo);
        if (iRet > 0) {
            if (iRet == 5) {
                tExit.iTransType = 2;
                ShowLEDMsg("Master Card!", "Master Card!");
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
            else {
                tExit.iTransType = 10;
                ShowLEDMsg("Complimentary!", "Complimentary!");
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
            tExit.sCardNo = sCardNo;
            tExit.iCardType = sCardType;
            tExit.sPaidAmt = 0;
            CloseExitOperation(FreeParking);
            return;
        } 
       if (GfeeFormat(tExit.sPaidAmt) <= GfeeFormat(sCardBal))
        {
            
        }
        else
        {
            ShowLEDMsg("Fee: $"+ Common::getInstance()->SetFeeFormat(tExit.sPaidAmt) +"^Insufficient Bal","Fee: $"+ Common::getInstance()->SetFeeFormat(tExit.sPaidAmt) +"^Insufficient Bal");
            writelog("insufficient balance", "OPR");
            EnableCashcard(true);
            return;
        }
    }
    else {
        gsCompareNo = tProcess.gsLastPaidTrans;
        auto sameAsLastIUDuration = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - operation::getInstance()->tProcess.getLastTransTime());
        if (sCheckNo == gsCompareNo && sameAsLastIUDuration.count() <= tParas.giMaxTransInterval) {
            writelog("last trans No: " + gsCompareNo, "OPR");
            writelog("Same as last IU, duration :" + std::to_string(sameAsLastIUDuration.count()) + " less than Maximum interval: " + std::to_string(operation::getInstance()->tParas.giMaxTransInterval), "OPR");
            if (tProcess.gbLastPaidStatus.load()  == true) {
                if (iDevicetype == Ant) sMsg = "Same as last^Paid IU" ;
                else sMsg = "Same as last^Paid Card";
                //--------
                ShowLEDMsg(sMsg,sMsg);
                EnableCashcard(false);
                Openbarrier();
            }else
            {
                if (tExit.sPaidAmt > 0) {
                    if (iDevicetype == Ant) EnableCashcard(true);
                } 
                else PBSExit (sCheckNo,iDevicetype,sCardNo,sCardType,sCardBal);   
            }
        } else{
            PBSExit (sCheckNo,iDevicetype,sCardNo,sCardType,sCardBal);
        }
    }
 }

 void operation::PBSExit(string sIU, DeviceType iDevicetype, string sCardNo, int sCardType,float sCardBal)
{
    int iRet;
    CE_Time pt,pd,calTime;
  
    if (sIU == tExit.sIUNo) return;
    tExit.sIUNo = sIU;

    //check blacklist
    iRet = m_db->IsBlackListIU(sIU);
    if (iRet >= 0){
        ShowLEDMsg(tExitMsg.MsgExit_BlackList[0], tExitMsg.MsgExit_BlackList[1]);
        SendMsg2Server("90",sIU+",,,,,Blacklist IU");
        if(iRet ==0) return;
    }
    //check block 
    string gsBlockIUPrefix = IniParser::getInstance()->FnGetBlockIUPrefix();
   // writelog ("blockIUprfix =" +gsBlockIUPrefix, "OPR");
    if(gsBlockIUPrefix.find(sIU.substr(0,3)) !=std::string::npos && sIU.length() == 10)
    {
        ShowLEDMsg("Lorry No Exit^Pls Reverse","Lorry No Exit^Pls Reverse");
        SendMsg2Server("90",sIU+",,,,,Block IU");
        return;
    }
    //---Get Entry time
    if (tExit.bNoEntryRecord == -1) {
        iRet = m_db->FetchEntryinfo(sIU);
        if (tExit.sEntryTime == "") {
             tExit.bNoEntryRecord = 1;
             tExit.lParkedTime = -1;
        } else {
            writelog("Get Entry Time: " + tExit.sEntryTime, "OPR");
            tExit.bNoEntryRecord = 0;
        }
    } 
    //-----
    if(sIU.length()==10) 
	    tExit.iTransType= db::getInstance()->FnGetVehicleType(sIU.substr(0,3));
	else {
        tExit.iTransType=GetVTypeFromLoop();
    }

    if (tExit.iTransType == 9) {
        ShowLEDMsg(tMsg.Msg_authorizedvehicle[0],tMsg.Msg_authorizedvehicle[1]);
        CloseExitOperation(FreeParking);
        return;
    }

    tExit.iVehicleType = (tExit.iTransType - 1 )/3;

    iRet = CheckSeason(sIU,2);

    if (iRet == 1 or iRet == 12 or iRet == 9)
    {   
        if (tParas.giSeasonCharge == 0 || tExit.bNoEntryRecord == 1)  {
            if (iRet == 9){tExit.iTransType = 10;}
            tExit.sFee = 0;
            tExit.sPaidAmt = 0;
            tExit.sExitTime = Common::getInstance()->FnGetDateTimeFormat_yyyy_mm_dd_hh_mm_ss();
            CloseExitOperation(SeasonParking);
            return;
        }
    }
    if (tExit.bNoEntryRecord == 1){
          //---- complimentary Ticket
         if(tExit.sRedeemNo != "" && tExit.sRedeemAmt == 0 ){  
            tExit.iTransType = 10;
            tExit.sPaidAmt = 0;
            tExit.sCardNo = tExit.sRedeemNo;
            ShowLEDMsg("No Entry Record^Compl Ticket","No Entry Record^Compl Ticket");
            CloseExitOperation(FreeParking);
            EnableCashcard(false);
            return;
        }     
        //----------------------------
        int iAutoDebit;
		float sAmt;
        writelog("No Entry, Check Auto Debit.", "OPR");
		m_db->GetXTariff(iAutoDebit, sAmt, 0);
		writelog("Autocharge:"+std::to_string(iAutoDebit), "OPR");
		writelog("ChargeAmt:"+ Common::getInstance()->SetFeeFormat(sAmt), "OPR");
        if (iAutoDebit > 0) {
            tExit.sFee = GfeeFormat(sAmt);
            tExit.sExitTime = Common::getInstance()->FnGetDateTimeFormat_yyyy_mm_dd_hh_mm_ss();
        }else{
            SendMsg2Server("07",sIU);
            ShowLEDMsg("No Entry Record^Press Intercom","No Entry Record^Press Intercom");
            return;
        }
    }else
    {
        tExit.sExitTime = Common::getInstance()->FnGetDateTimeFormat_yyyy_mm_dd_hh_mm_ss();
        writelog("Cal Fee Time: " + tExit.sExitTime, "OPR");
        //-------
        pt.SetTime(tExit.sEntryTime);
        pd.SetTime(tExit.sExitTime);
        tExit.lParkedTime = calTime.diffmin(pt.GetUnixTimestamp(), pd.GetUnixTimestamp());
        //-------
        writelog("parked time: " + std::to_string(tExit.lParkedTime) + " Mins", "OPR");
        //---------
        if(iRet == 1)
        {  
            if (std::stoi(tSeason.rate_type) != 0) {
                // Show partial season fee 
                writelog ("Cal fee for partial season. RateType: " + tSeason.rate_type, "OPR");
                tExit.sFee = CalFeeRAM(tExit.sEntryTime, tExit.sExitTime, std::stoi(tSeason.rate_type));

            }else{
                if (tExit.sEntryTime < tSeason.date_from) {
                    writelog ("Season EntryTime early than date from, Cal fee for entry ~ date from", "OPR");
                    iRet = 8;
                    tExit.sFee = CalFeeRAM(tExit.sEntryTime, tSeason.date_from, tExit.iVehicleType);
                    ShowLEDMsg("Season Start On^" + tSeason.date_from.substr(0,16),"Season Start On^" + tSeason.date_from.substr(0,16));
                    //-----
                   std::this_thread::sleep_for(std::chrono::milliseconds(500));
                } else
                {
                    tExit.sFee = CalFeeRAM(tExit.sEntryTime, tExit.sExitTime, tExit.iVehicleType);
                }
            }
        }
        else
        {
            if(iRet == 2 && tSeason.date_to > tExit.sEntryTime) {
                writelog ("Season expired. EntryTime early than date to, cal fee for date to ~ exit", "OPR");
                tExit.sFee = CalFeeRAM(tSeason.date_to, tExit.sExitTime, tExit.iVehicleType);
                ShowLEDMsg("Season Expire On^" + tSeason.date_to.substr(0,16) ,"Season Expire On^" + tSeason.date_to.substr(0,16));
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            } else
            {
                tExit.sFee = CalFeeRAM(tExit.sEntryTime, tExit.sExitTime, tExit.iVehicleType);
            }
        }
        //------ whole day season 
        if ((iRet == 1 && std::stoi(tSeason.rate_type) == 0) or iRet == 9 or iRet == 12)
        {
            if (iRet == 9) { tExit.iTransType = 10;}
            tExit.sPaidAmt = 0;
            CloseExitOperation(SeasonParking);
            return;
        }
    }
    //-------
    if (tExit.iRedeemTime > 0) RedeemTime2Amt();
    //-----
    tExit.sPaidAmt = GfeeFormat(tExit.sFee - tExit.sRebateAmt - tExit.sRedeemAmt + tExit.sOweAmt);
    if (tExit.sPaidAmt < 0)  tExit.sPaidAmt = 0;
    //------
    writelog("Total paid Amt: " + Common::getInstance()->SetFeeFormat(tExit.sPaidAmt), "OPR");
    //------
    showFee2User();
    //-------------
   // ShowLEDMsg("Fee: $"+ Common::getInstance()->SetFeeFormat(tExit.sPaidAmt) +"^Please Wait...","Fee: $"+ Common::getInstance()->SetFeeFormat(tExit.sPaidAmt) +"^Please Wait...");
   if(tExit.sRedeemNo != "" && tExit.sRedeemAmt == 0 ){
        //---- complimentary Ticket
        tExit.iTransType = 10;
        tExit.sPaidAmt = 0;
        tExit.sCardNo = tExit.sRedeemNo;
        ShowLEDMsg("Fee: $"+ Common::getInstance()->SetFeeFormat(tExit.sPaidAmt) +"^Compl Ticket","Fee: $"+ Common::getInstance()->SetFeeFormat(tExit.sPaidAmt) +"^Compl Ticket");
        CloseExitOperation(FreeParking);
        EnableCashcard(false);
        return;
   } 
    if (tExit.sPaidAmt > 0) 
    {
        if(iDevicetype > Ant){
            if (GfeeFormat(tExit.sPaidAmt) <= GfeeFormat(sCardBal)){
            }
            else{
                tExit.giDeductionStatus = WaitingCard;
                writelog ("Insufficient balance", "OPR");
                writelog("Fee: $"+ Common::getInstance()->SetFeeFormat(tExit.sPaidAmt) + "Bal: $" + Common::getInstance()->SetFeeFormat(sCardBal) , "OPR");
                ShowLEDMsg("Fee: $"+ Common::getInstance()->SetFeeFormat(tExit.sPaidAmt) +"^Insufficient Balance","Fee: $"+ Common::getInstance()->SetFeeFormat(tExit.sPaidAmt) +"^Insufficient Balance");
                EnableCashcard(true);
            }
           
        }else
        {
            tExit.giDeductionStatus = WaitingCard;
           // ShowLEDMsg("Fee: $"+ Common::getInstance()->SetFeeFormat(tExit.sPaidAmt) +"^Waiting for Card","Fee: $"+ Common::getInstance()->SetFeeFormat(tExit.sPaidAmt) +"^Waiting for Card");
            EnableCashcard(true);
                //enable EPS
        }
    }
    else
    {
       CloseExitOperation(FreeParking);
    } 

}

float operation::CalFeeRAM(string eTime, string payTime,int iTransType, bool bNoGT)
{
    
    float parkingfee;
    
    parkingfee = m_db->CalFeeRAM2G(eTime,payTime,iTransType);
   
    return parkingfee;
}

void operation::SaveExit()
{
    int iRet;
    int giPMSEntryRecord = 0;
    std::string sLPRNo = "";
    CE_Time pt,pd,calTime;
    
    if (tExit.sIUNo== "") return;
    writelog ("Save Exit trans:"+ tExit.sIUNo, "OPR");
    //----
    if (tExit.sRedeemAmt > (tExit.sFee - tExit.sRebateAmt + tExit.sOweAmt)) tExit.sRedeemAmt = tExit.sFee - tExit.sRebateAmt + tExit.sOweAmt;
    //----
    if (tExit.bNoEntryRecord == 0 && tExit.lParkedTime <= 0) {
        if (tExit.lParkedTime == -1 ) giPMSEntryRecord = 1;
        pt.SetTime(tExit.sEntryTime);
        pd.SetTime(tExit.sExitTime);
        tExit.lParkedTime = calTime.diffmin(pt.GetUnixTimestamp(), pd.GetUnixTimestamp());
    }
    //----
    iRet = db::getInstance()->insertexittrans(tExit);
    if (iRet == iCentralSuccess){
        if (tExit.bNoEntryRecord == 0 && giPMSEntryRecord == 0 ) {
            iRet = db::getInstance()->updatemovementtrans(tExit);
        }else  {
            iRet = db::getInstance()->insert2movementtrans(tExit);
        }
    }
    //------ delete Local Entry
    db::getInstance()->UpdateLocalEntry(tExit.sIUNo);
    //----
    if (iRet == iCentralSuccess or iRet == iLocalSuccess)
    {
        tProcess.setLastIUNo(tExit.sIUNo);
        tProcess.setLastPaidTrans(tExit.sIUNo);
        tProcess.setLastTransTime(std::chrono::steady_clock::now());
    }
    //-------
    tPBSError[iDB].ErrNo = (iRet == iCentralSuccess or iRet == iLocalSuccess) ? 0 : (iRet == iCentralFail) ? -1 : -2;

    if ((tExit.sLPN[0] != "") or (tExit.sLPN[1] != ""))
	{
		if((tExit.iTransType == 7) or (tExit.iTransType == 8) or (tExit.iTransType == 22))
		{
			sLPRNo = tExit.sLPN[1];
		}
		else
		{
			sLPRNo = tExit.sLPN[0];
		}
	}

    std::string sMsg2Send = (iRet == iCentralSuccess or iRet == iLocalSuccess) ? "Exit OK" : (iRet == iCentralFail) ? "Exit Central Failed" : "Exit Local Failed";

    sMsg2Send = tExit.sIUNo + "," + tExit.sCardNo + "," + Common::getInstance()->SetFeeFormat(tExit.sPaidAmt) + "," + sLPRNo + "," + std::to_string(tProcess.giShowType) + "," + sMsg2Send;

    if (tEntry.iStatus == 0) {
        SendMsg2Server("90", sMsg2Send);
    }
    tProcess.gbsavedtrans = true;
    tExit.gbPaid.store(true);
    if (tExit.sPaidAmt == 0) {
        tProcess.gsLastCardNo = tExit.sCardNo;
        tProcess.gbLastPaidStatus.store(true);
    }
    //-------
    PrintReceipt();
}

void operation::PrintReceipt()
{
    // gbflag4Receipt:   0: default   1: button press  2: Receipt printed

    if (tExit.iflag4Receipt == 0) return;
    //--------
    if (tPBSError[iPrinter].ErrNo != 0) {
        writelog ("Printer Error while Print Receipt", "OPR");
        ShowLEDMsg("Printer Error^Press Intercom ", "Printer Error^Press Intercom");
        return;
    }
    //-------
   if (tExit.iflag4Receipt == 1 && tExit.gbPaid.load() == true && tExit.sPaidAmt > 0) {
        
        PrintTR(false);
        tExit.iflag4Receipt = 2;
 
   } 
}

float operation::GfeeFormat(float value) {
    return std::round(value * 100.0) / 100.0;
}

void operation::CloseExitOperation(TransType iStatus)
{
   
    string sLEDMsg = "";
    string sLCDMsg = "";
    //-----
    if (iStatus < 4){
        tExit.iStatus = 0;
    }
    switch (iStatus){
        case FreeParking:
        {
            //---- LED MSg
            sLEDMsg = "Fee = $0.00 ^ Have A Nice Day!";
            sLCDMsg = "Fee = $0.00 ^ Have A Nice Day!";
            break;
        }
        case SeasonParking:
        {
            //sLEDMsg = "Season Parking ^ Have A Nice Day!";
            //sLCDMsg = "Season parking ^ Have A Nice Day!";
            break;
        }
        case GracePeriod:
        {
            //---- LED MSg
            sLEDMsg = "Grace Period ^ Have A Nice Day!";
            sLCDMsg = "Grace period ^ Have A Nice Day!";
            break;
        }
        case DeductionOK:
        {
            //---- LED MSg
            sLEDMsg = "Paid Amt: " + Common::getInstance()->SetFeeFormat(tExit.sPaidAmt) + "^ Have A Nice Day!";
            sLCDMsg = "Paid Amt: " + Common::getInstance()->SetFeeFormat(tExit.sPaidAmt) + "^ Have A Nice Day!";
            break;
        }
        case DeductionFail:
        {
            //---- LED MSg
            break;
        }
        case BalanceChange:
        {
            tExit.iStatus = 3;
            sLEDMsg = "Paid Amt: " + Common::getInstance()->SetFeeFormat(tExit.sPaidAmt) + "^ Have A Nice Day!";
            sLCDMsg = "Paid Amt: " + Common::getInstance()->SetFeeFormat(tExit.sPaidAmt) + "^ Have A Nice Day!";
            break;
        }
        case Manualopen:
        {
            tExit.iStatus = 4;
            sLEDMsg = "Please process^ Have A Nice Day!";
            sLCDMsg = "Please Process^ Have A Nice Day!";
            break;
        }
        default:
			break;

    }

    if (sLEDMsg != "") ShowLEDMsg(sLEDMsg,sLCDMsg);

    writelog ("Enter close Exit for: " + tExit.sIUNo, "OPR");
    SaveExit();
    Openbarrier();
}

void operation::RedeemTime2Amt() 
{
    string sTmpTime;
    CE_Time pt;
    CE_Time pd;
    CE_Time calTime;
    float sAddFee;

    //--------
    if (tExit.sRedeemAmt > 0) {
        writelog ("Already has Redeem Amt: "+ Common::getInstance()->SetFeeFormat(tExit.sRedeemAmt), "OPR");
        return;
    }
    //
    if (tExit.iRedeemTime > 0)
    {
        if (tExit.sExitTime == "")
            sTmpTime = Common::getInstance()->FnGetDateTimeFormat_yyyy_mm_dd_hh_mm_ss();
        else
            sTmpTime = tExit.sExitTime;
        //-------
        if (tExit.bNoEntryRecord != 0)
        {
            pt.SetTime(sTmpTime);
            pt.SetTime(pt.GetUnixTimestamp() - tExit.iRedeemTime*60);
            tExit.sRedeemAmt = CalFeeRAM(pt.DateTimeString(), sTmpTime, tExit.iVehicleType);
        }
        else
        {
            if (tExit.lParkedTime == 0){
                pt.SetTime(tExit.sEntryTime);
                pd.SetTime(sTmpTime);
                tExit.lParkedTime = calTime.diffmin(pt.GetUnixTimestamp(), pd.GetUnixTimestamp());
            }

            if (tExit.iRedeemTime >= tExit.lParkedTime){
                tExit.sRedeemAmt = tExit.sFee;
            }else{
                pt.SetTime(tExit.sEntryTime);
                pt.SetTime(pt.GetUnixTimestamp() + tExit.iRedeemTime*60);
                tExit.sRedeemAmt = CalFeeRAM(tExit.sEntryTime, pt.DateTimeString(), tExit.iVehicleType);
                if (GfeeFormat(tExit.sFee - tExit.sRedeemAmt) == 0)  
                //---- handle same block, perentry, grace 
                {
                    sAddFee = CalFeeRAM(pt.DateTimeString(), sTmpTime, tExit.iVehicleType);
                    if (sAddFee > 0) {
                        tExit.sRedeemAmt = GfeeFormat(tExit.sRedeemAmt - sAddFee);
                        if (tExit.sRedeemAmt < 0) tExit.sRedeemAmt = 0;
                    }
                }
            }

        }
        if (tExit.sRedeemAmt > 0 ) {
            writelog ("Redemption Time to Amt: $" + Common::getInstance()->SetFeeFormat(tExit.sRedeemAmt), "OPR");
        }else
        {
            writelog ("Redeeming time for per entry or same block is not useful.", "OPR");
        }
    }  

}

void operation::ticketScan(std::string skeyedNo)
{
    int iRet = 0;
    std::string sCardTkNo = "";
    std::tm dtExpireTime;
    double gbRedeemAmt = 0.00;
    int giRedeemTime = 0;
    std::string sMsg = "";
    long lParkTime = 0;
    std::string TT = "";
    bool isRedemptionTicket = false;

    //--------
    if (tExit.sRedeemAmt > 0)
    {
        if (tExit.sFee >= 0.01)
        {
            if (tExit.sFee - tExit.sRedeemAmt - tExit.sRebateAmt + tExit.sOweAmt <= 0.00f)
            {
                ticketOK();
                return;
            }
            else
            {
                ShowLEDMsg("Amount Redeem,^Pls Pay Bal!", "Amount Redeem^Pls Pay Bal!");
                writelog("Amount Redeem, Pls Pay Bal.","OPR");
                return;
            }
        }
    }

    //--------
    if (skeyedNo.length() >= 12)
    {
        // Check if ticket barcode is 12 digits
        if ((Common::getInstance()->FnToUpper(skeyedNo.substr(0, 2)) == "SC") or (Common::getInstance()->FnToUpper(skeyedNo.substr(0, 2)) == "SR"))
        {
            if  (Common::getInstance()->FnToUpper(skeyedNo.substr(0, 2)) == "SR"){
                isRedemptionTicket = true;
            }
            sCardTkNo = Common::getInstance()->FnToUpper(skeyedNo.substr(0, 12));
        }
        // Check if ticket barcode is 9 digits
        else
        {
            sCardTkNo = Common::getInstance()->FnToUpper(skeyedNo.substr(0, 9));

            TT = sCardTkNo.substr(7, 1);
            if ((TT == "V") or (TT == "W") or (TT == "U") or (TT == "Z"))
            {
                if ((TT == "V") or (TT == "W"))
                {
                    isRedemptionTicket = true;
                }

                if (((TT == "V") or (TT == "W")) && (tParas.giExitTicketRedemption == 0))
                {
                    writelog("Redemption ticket but redemption disabled: " + sCardTkNo, "OPR");
                    ShowLEDMsg(tExitMsg.MsgExit_RedemptionTicket[0], tExitMsg.MsgExit_RedemptionTicket[1]);
                    goto Exit_Sub;
                }
            }

            ShowLEDMsg(tExitMsg.MsgExit_CardIn[0], tExitMsg.MsgExit_CardIn[1]);
        }
    }
    
    if (sCardTkNo.length() != 9 && sCardTkNo.length() != 12)
    {
        writelog("Invalid Ticket (Wrong Len): " + skeyedNo, "OPR");
        SendMsg2Server("90",skeyedNo + ",,,,,Invalid Ticket (Wrong Len)");
        ShowLEDMsg(tExitMsg.MsgExit_InvalidTicket[0], tExitMsg.MsgExit_InvalidTicket[1]);
        goto Exit_Sub;
    }

    // Ret : 0 = Expired, 1 = Valid, 2 = Used, 6 = Not Started, -1 = DB Error, 4 = Not Found
    iRet = db::getInstance()->isValidBarCodeTicket(isRedemptionTicket, skeyedNo, dtExpireTime, gbRedeemAmt, giRedeemTime);
   
    switch (iRet)
    {
        // DB Error
        case -1:
        {
            writelog("Central DB Error", "OPR");
            ShowLEDMsg(tExitMsg.MsgExit_SystemError[0], tExitMsg.MsgExit_SystemError[1]);
            SendMsg2Server("90", sCardTkNo + ",,,,,Central DB Error");
            goto Exit_Sub;
            break;
        }
        // Expired
        case 0:
        {
            writelog("Ticket Expired: " + sCardTkNo, "OPR");
            if (isRedemptionTicket == true)
            {
                ShowLEDMsg(tExitMsg.MsgExit_RedemptionExpired[0], tExitMsg.MsgExit_RedemptionExpired[1]);
                SendMsg2Server("90", sCardTkNo + ",,,,,Redemption Expired");
            }
            else
            {
                ShowLEDMsg(tExitMsg.MsgExit_CompExpired[0], tExitMsg.MsgExit_CompExpired[1]);
                SendMsg2Server("90", sCardTkNo + ",,,,,Complimentary Expired");
            }
            goto Exit_Sub;
            break;
        }
        // Valid
        case 1:
        {
            tExit.sRedeemNo = sCardTkNo;
            //--------
            if (isRedemptionTicket == true)
            {
                writelog("Redemption Ticket: " + sCardTkNo + ", Expire: " + Common::getInstance()->FnFormatDateTime(dtExpireTime, "%Y-%m-%d %H:%M:%S"), "OPR");

                if (giRedeemTime > 0)
                {
                    tExit.iRedeemTime = giRedeemTime;
                    sMsg = "Redemption: ^" + std::to_string(tExit.iRedeemTime) + " Mins";
                    if(tExit.sPaidAmt > 0) {
                        writelog ("Call Redeemtime2Amt", "OPR");
                        RedeemTime2Amt();
                    }
                }
                else
                {
                    tExit.sRedeemAmt = GfeeFormat(gbRedeemAmt);
                    sMsg = "Redemption: ^$" + Common::getInstance()->SetFeeFormat(tExit.sRedeemAmt);
                }

                writelog(sMsg, "OPR");
                ShowLEDMsg(sMsg, sMsg);
                if (tExit.sIUNo == "" && tExit.sCardNo == "")
                {
                    tExit.sTag = "Redemption";
                    SendMsg2Server("90", sCardTkNo + ",,,,,Redemption. Ticket Wait for Card");
                    EnableCashcard(true);
                    return;
                }
                showFee2User();
                float fee = 0.00f;
                try
                {
                    fee = std::stof(Common::getInstance()->SetFeeFormat((tExit.sFee - tExit.sRedeemAmt - tExit.sRebateAmt + tExit.sOweAmt)));
                }
                catch (const std::exception& e)
                {
                    writelog("Exception :" + std::string(e.what()), "OPR");
                }
                    
                if (fee <= 0.00f)
                {
                        ticketOK();
                }
                else
                {
                    ShowLEDMsg(tExitMsg.MsgExit_XNoCard[0], tExitMsg.MsgExit_XNoCard[1]);
                    EnableCashcard(true);
                    return;
                }

            }
                // Complimentary
            else
            {
                writelog("Complimentary Ticket: " + sCardTkNo + ", Expire: " + Common::getInstance()->FnFormatDateTime(dtExpireTime, "%Y-%m-%d %H:%M:%S"), "OPR");

                if (tParas.giNeedCard4Complimentary  == 0)
                {
                    ShowLEDMsg(tExitMsg.MsgExit_Complimentary[0], tExitMsg.MsgExit_Complimentary[1]);
                    if (tExit.sIUNo == "")
                    {
                        tExit.sIUNo = "N/A";
                    }
                    if (tExit.bPayByEZPay == true)
                    {
                        ShowLEDMsg("Complimentary^EZPay refunded", "");
                        writelog("Complimentary, $" + Common::getInstance()->SetFeeFormat(tExit.sFee) + " refunded to VCC/EZPay", "OPR");
                        tExit.sRedeemAmt = GfeeFormat(tExit.sFee);
                        tExit.sPaidAmt = 0;
                        tExit.sGSTAmt = 0;
                        db::getInstance()->update99PaymentTrans();
                        return;
                    }
                }
                else
                {
                    if (tExit.sIUNo == "")
                    {
                        ShowLEDMsg(tExitMsg.MsgExit_Complimentary[0], tExitMsg.MsgExit_Complimentary[1]);
                        tExit.sTag = "Complimentary";
                        SendMsg2Server("90", sCardTkNo + ",,,,,Compl. Ticket Wait for Card");
                        EnableCashcard(true);
                        return;
                    }
                    else
                    {
                        if (tExit.bPayByEZPay == true)
                        {
                            ShowLEDMsg("Complimentary^EZpay refunded", "");
                            writelog("Complimentary, $" + Common::getInstance()->SetFeeFormat(tExit.sFee) + " refunded to VCC/EZPay", "OPR");
                            tExit.sRedeemAmt = GfeeFormat(tExit.sFee);
                            tExit.sPaidAmt = 0;
                            tExit.sGSTAmt = 0;
                            db::getInstance()->update99PaymentTrans();
                            return;
                        }
                        ShowLEDMsg(tExitMsg.MsgExit_Complimentary[0], tExitMsg.MsgExit_Complimentary[1]);
                    }
                }
               ticketOK();    
            }   
            break;
        }
        // Used
        case 2:
        {
            writelog("Used Ticket: " + sCardTkNo, "OPR");
            ShowLEDMsg(tExitMsg.MsgExit_UsedTicket[0], tExitMsg.MsgExit_UsedTicket[1]);
            SendMsg2Server("90", sCardTkNo + ",,,,,Used Ticket");
            goto Exit_Sub;
            break;
        }
        // Not found
        case 4:
        {
            writelog("Ticket Not Found " + sCardTkNo, "OPR");
            ShowLEDMsg("Ticket Not Found", "Ticket Not Found");
            SendMsg2Server("90", sCardTkNo + ",,,,,Ticket Not Found");
            goto Exit_Sub;
            break;
        }
        // Not started
        case 6:
        {
            writelog("Ticket Not Start: " + sCardTkNo, "OPR");
            ShowLEDMsg("Ticket Not Start", "Ticket Not Start");
            SendMsg2Server("90", sCardTkNo + ",,,,,Ticket Not Start");
            goto Exit_Sub;
            break;
        }
        default:
        {
            writelog("Unable to find the return value from barcode ticket.", "OPR");
            break;
        }
    }

Exit_Sub:

    // Temp: Possible need to add 2 seconds delay

    if (tExit.bPayByEZPay == true)
    {
        ShowLEDMsg("You may scan^Compl Ticket", "");
    }
    return;
}

void operation::ticketOK()
{
    tExit.sPaidAmt = 0;
    if (tExit.sRedeemAmt == 0 && tExit.sRedeemNo != "") {
        tExit.iTransType = 10;
        tExit.sCardNo = tExit.sRedeemNo;
    }
    CloseExitOperation(FreeParking);
}

void operation::showFee2User(bool bPaying)
{
    std::string sPT = "";
    float sFee2Pay = 0.00f;
    std::string sUpp = "";
    std::string sLow = "";

    sFee2Pay = GfeeFormat(tExit.sFee - tExit.sRedeemAmt - tExit.sRebateAmt + tExit.sOweAmt);

    if (sFee2Pay < 0)
    {
        sFee2Pay = 0;
    }

    sPT = db::getInstance()->CalParkedTime(tExit.lParkedTime);

    if (sFee2Pay > 0)
    {
        sUpp = "Fee:$" + Common::getInstance()->SetFeeFormat(sFee2Pay);
        sLow = tExitMsg.MsgExit_XNoCard[0];
    }
    else
    {
        // "Grace Period, Free!"
        sUpp = "Fee parking";
        sLow = "Have a nice day!";
    }

    std::string sMsg = sUpp + "^" + sLow;
    
    ShowLEDMsg(sMsg, sMsg);

    std::string exit_entry_time = "00:00";
    try
    {
        if (tExit.sEntryTime != "")
        {
            exit_entry_time = Common::getInstance()->FnFormatDateTime(tExit.sEntryTime, "%Y-%m-%d %H:%M:%S", "%H:%M");
        }
    }
    catch (const std::exception& e)
    {
        writelog("Exception :" + std::string(e.what()), "OPR");
    }

    std::ostringstream ss;
    ss << tExit.sIUNo << ", E=" << exit_entry_time << "~T=" << sPT << ",";
    ss << Common::getInstance()->SetFeeFormat(sFee2Pay) << "," << ",";
    ss << tProcess.giShowType << "," << "Fee OK";
    SendMsg2Server("90", ss.str());
    //------
    tExit.sPaidAmt = GfeeFormat(sFee2Pay);
}

void operation::ReceivedEntryRecord()
{
    CE_Time pt,pd,calTime;

    tExit.sExitTime = Common::getInstance()->FnGetDateTimeFormat_yyyy_mm_dd_hh_mm_ss();

    writelog("Cal Fee Time: " + tExit.sExitTime, "OPR");
    
    tExit.sFee = CalFeeRAM(tExit.sEntryTime, tExit.sExitTime, tExit.iVehicleType);
    //--------------
    if (tExit.iRedeemTime > 0) RedeemTime2Amt();
    //-----
    tExit.sPaidAmt = GfeeFormat(tExit.sFee - tExit.sRebateAmt - tExit.sRedeemAmt + tExit.sOweAmt);

    if (tExit.sPaidAmt < 0)  tExit.sPaidAmt = 0;
    //------
    writelog("Total paid Amt: " + Common::getInstance()->SetFeeFormat(tExit.sPaidAmt), "OPR");
    //------
    showFee2User();
    //-------------
    if (tExit.sPaidAmt > 0) 
    {
        tExit.giDeductionStatus = WaitingCard;
        EnableCashcard(true);
    }
    else
    {
       CloseExitOperation(FreeParking);
    } 
}

void operation::processTnGResponse(const std::string& respCmd, const std::string& sResult)
{
    if (respCmd == "PayResult")
    {
        try
        {
            int state = 0;
            std::string orderId = "";
            int payType = 0;
            std::string cardNo = "";
            int bal = 0;
            long int payTime = 0;
            std::string stan = "";
            std::string apprCode = "";
            
            std::vector<std::string> subVector = Common::getInstance()->FnParseString(sResult, ',');
            for (unsigned int i = 0; i < subVector.size(); i++)
            {
                std::string pair = subVector[i];
                std::string param = Common::getInstance()->FnBiteString(pair, '=');
                std::string value = pair;

                if (param == "state")
                {
                    state = std::stoi(value);
                }

                if (param == "orderId")
                {
                    orderId = value;
                }

                if (param == "payType")
                {
                    payType = std::stoi(value);
                }

                if (param == "cardNo")
                {
                    cardNo = value;
                }

                if (param == "bal")
                {
                    bal = std::stoi(value);
                }

                if (param == "payTime")
                {
                    payTime = std::stol(value);
                }

                if (param == "stan")
                {
                    stan = value;
                }

                if (param == "apprCode")
                {
                    apprCode = value;
                }
            }

            // Process result

            std::ostringstream oss;
            oss << "state=" << state;
            oss << ", orderId=" << orderId;
            oss << ", payType=" << payType;
            oss << ", cardNo=" << cardNo;
            oss << ", bal=" << bal;
            oss << ", payTime=" << payTime;
            oss << ", stan=" << stan;
            oss << ", apprCode=" << apprCode;
            writelog(oss.str(), "OPR");
        }
        catch (const std::exception& ex)
        {
            std::ostringstream oss;
            oss << "Exception : " << ex.what();
            writelog(oss.str(), "OPR");
        }
    }
    else if (respCmd == "TnGCardNumResult")
    {
        try
        {
            std::string cardNo;
            std::string orderId;

            std::vector<std::string> subVector = Common::getInstance()->FnParseString(sResult, ',');
            for (unsigned int i = 0; i < subVector.size(); i++)
            {
                std::string pair = subVector[i];
                std::string param = Common::getInstance()->FnBiteString(pair, '=');
                std::string value = pair;

                if (param == "cardNo")
                {
                    cardNo = value;
                }

                if (param == "orderId")
                {
                    orderId = value;
                }
            }

            // Process result
            std::ostringstream oss;
            oss << "cardNo=" << cardNo;
            oss << ", orderId=" << orderId;
            writelog(oss.str(), "OPR");
        }
        catch (const std::exception& ex)
        {
            std::ostringstream oss;
            oss << "Exception : " << ex.what();
            writelog(oss.str(), "OPR");
        }
    }
}