#include "touchngo_reader.h"
#include "log.h"
#include <boost/json.hpp>

TnG_Reader* TnG_Reader::TnG_Reader_;
std::mutex TnG_Reader::mutex_;

TnG_Reader::TnG_Reader()
    : ioContext_(),
    strand_(boost::asio::make_strand(ioContext_)),
    work_(ioContext_),
    logFileName_("tng")
{

}

TnG_Reader* TnG_Reader::getInstance()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (TnG_Reader_ == nullptr)
    {
        TnG_Reader_ = new TnG_Reader();
    }
    return TnG_Reader_;
}

void TnG_Reader::FnTnGReaderInit(const std::string& remoteServerHost, const std::string& remoteServerPort, const std::string& listenHost, const std::string& listenPort)
{
    Logger::getInstance()->FnCreateLogFile(logFileName_);
    
    remoteServerHost_ = remoteServerHost;
    remoteServerPort_ = remoteServerPort;
    listenHost_ = listenHost;
    listenPort_ = listenPort;

    auto const listen_address = boost::asio::ip::make_address(listenHost_);
    auto const listen_port = static_cast<unsigned short>(std::stoi(listenPort_));
    startIoContextThread();

    httpClient_ = std::make_shared<HttpClient>(ioContext_);
    httpServer_ = std::make_shared<HttpServer>(ioContext_, boost::asio::ip::tcp::endpoint{listen_address, listen_port},
                    std::bind(&TnG_Reader::serverHandleFailureCb, this, std::placeholders::_1),
                    std::bind(&TnG_Reader::serverHandleRequestCb, this, std::placeholders::_1));
    httpServer_->run();
    
    Logger::getInstance()->FnLog("Touch n Go Reader initialization completed.");
    Logger::getInstance()->FnLog("Touch n Go Reader initialization completed.", logFileName_, "TNG");
    
    std::ostringstream oss;
    oss << "Remote server host: " << remoteServerHost << ", Remote server port: " << remoteServerPort;
    oss << ", Local server host: " << listenHost << ", Local server port: " << listenPort;
    Logger::getInstance()->FnLog(oss.str());
    Logger::getInstance()->FnLog(oss.str(), logFileName_, "TNG");
}

void TnG_Reader::FnTnGReaderClose()
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "TNG");

    ioContext_.stop();
    if (ioContextThread_.joinable())
    {
        ioContextThread_.join();
    }
}

void TnG_Reader::startIoContextThread()
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "TNG");

    if (!ioContextThread_.joinable())
    {
        ioContextThread_ = std::thread([this]() { ioContext_.run(); });
    }
}

void TnG_Reader::FnTnGReader_PayRequest(int payAmt, int discountAmt, long int enterTime, long int payTime, const std::string& orderId)
{
    if (httpClient_)
    {
        boost::json::object obj;
        obj["payAmount"] = payAmt;
        obj["DiscountAmount"] = discountAmt;
        obj["EnterTime"] = enterTime;
        obj["PayTime"] = payTime;
        obj["OrderId"] = orderId;

        httpClient_->post(remoteServerHost_, remoteServerPort_, "/w4g/PayRequest", 
            boost::json::serialize(obj), 
            std::bind(&TnG_Reader::payRequestSuccessCb, this, std::placeholders::_1),
            std::bind(&TnG_Reader::payRequestFailureCb, this, std::placeholders::_1, std::placeholders::_2));
        
        std::ostringstream oss;
        oss << "Outgoing req => " << " path: /w4g/PayRequest , data: " << boost::json::serialize(obj);
        Logger::getInstance()->FnLog(oss.str(), logFileName_, "TNG");
    }
}

void TnG_Reader::payRequestSuccessCb(const boost::beast::http::response<boost::beast::http::string_body>& res)
{
    std::ostringstream oss;
    oss << __func__ << " Incoming res <= data: " << res.body();
    Logger::getInstance()->FnLog(oss.str(), logFileName_, "TNG");
}

void TnG_Reader::payRequestFailureCb(const std::string& what, const boost::beast::error_code& ec)
{
    std::ostringstream oss;
    oss << __func__ << " Error: " << what << ": " << ec.message();
    Logger::getInstance()->FnLog(oss.str(), logFileName_, "TNG");
}

void TnG_Reader::FnTnGReader_PayCancel(const std::string& orderId)
{
    if (httpClient_)
    {
        boost::json::object obj;
        obj["OrderId"] = orderId;

        httpClient_->post(remoteServerHost_, remoteServerPort_, "/w4g/PayCancel",
            boost::json::serialize(obj),
            std::bind(&TnG_Reader::payCancelSuccessCb, this, std::placeholders::_1),
            std::bind(&TnG_Reader::payCancelFailureCb, this, std::placeholders::_1, std::placeholders::_2));
        
        std::ostringstream oss;
        oss << "Outgoing req => " << " path: /w4g/PayCancel , data: " << boost::json::serialize(obj);
        Logger::getInstance()->FnLog(oss.str(), logFileName_, "TNG");
    }
}

void TnG_Reader::payCancelSuccessCb(const boost::beast::http::response<boost::beast::http::string_body>& res)
{
    std::ostringstream oss;
    oss << __func__ << " Incoming res <= data: " << res.body();
    Logger::getInstance()->FnLog(oss.str(), logFileName_, "TNG");
}

void TnG_Reader::payCancelFailureCb(const std::string& what, const boost::beast::error_code& ec)
{
    std::ostringstream oss;
    oss << __func__ << " Error: " << what << ": " << ec.message();
    Logger::getInstance()->FnLog(oss.str(), logFileName_, "TNG");
}

void TnG_Reader::FnTnGReader_EnableReader(int enable, const std::string& orderId)
{
    if (httpClient_)
    {
        boost::json::object obj;
        obj["Enable"] = enable;
        obj["OrderId"] = orderId;

        httpClient_->post(remoteServerHost_, remoteServerPort_, "/w4g/ReaderCtrl",
            boost::json::serialize(obj),
            std::bind(&TnG_Reader::enableReaderSuccessCb, this, std::placeholders::_1),
            std::bind(&TnG_Reader::enableReaderFailureCb, this, std::placeholders::_1, std::placeholders::_2));
        
        std::ostringstream oss;
        oss << "Outgoing req => " << " path: /w4g/ReaderCtrl , data: " << boost::json::serialize(obj);
        Logger::getInstance()->FnLog(oss.str(), logFileName_, "TNG");
    }
}

void TnG_Reader::enableReaderSuccessCb(const boost::beast::http::response<boost::beast::http::string_body>& res)
{
    std::ostringstream oss;
    oss << __func__ << " Incoming res <= data: " << res.body();
    Logger::getInstance()->FnLog(oss.str(), logFileName_, "TNG");
}

void TnG_Reader::enableReaderFailureCb(const std::string& what, const boost::beast::error_code& ec)
{
    std::ostringstream oss;
    oss << __func__ << " Error: " << what << ": " << ec.message();
    Logger::getInstance()->FnLog(oss.str(), logFileName_, "TNG");
}

void TnG_Reader::serverHandleFailureCb(const std::string& what)
{
    std::ostringstream oss;
    oss << __func__ << " Error: " << what;
    Logger::getInstance()->FnLog(oss.str(), logFileName_, "TNG");
}

boost::beast::http::response<boost::beast::http::string_body> TnG_Reader::serverHandleRequestCb(const boost::beast::http::request<boost::beast::http::string_body>& req)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "TNG");

    boost::beast::http::response<boost::beast::http::string_body> res{boost::beast::http::status::ok, req.version()};
    res.set(boost::beast::http::field::server, "BOOST_BEAST_VERSION_STRING");
    res.set(boost::beast::http::field::content_type, "application/json");

    if (req.method() == boost::beast::http::verb::post)
    {
        std::string_view path(req.target().data(), req.target().size());

        std::ostringstream oss;
        oss << "Incoming req => path: " << path << " , data: " << req.body();
        Logger::getInstance()->FnLog(oss.str(), logFileName_, "TNG");

        try
        {
            boost::json::value json_data = boost::json::parse(req.body());
            if (!json_data.is_object())
            {
                res.result(boost::beast::http::status::bad_request);
                res.body() = "Invalid JSON format";
            }
            else
            {
                boost::json::object& obj = json_data.as_object();
                boost::json::object response_obj;

                if (path == "/w4g/PayResult")
                {
                    std::string orderId;
                    int state = 0;

                    if (obj.contains("OrderId"))
                    {
                        orderId = boost::json::value_to<std::string>(obj["OrderId"]);
                    }

                    response_obj["State"] = state;
                    response_obj["OrderId"] = orderId;
                }
                else if (path == "/w4g/CardNoUpl")
                {
                    std::string cardNo;
                    int state = 0;

                    if (obj.contains("CardNo"))
                    {
                        cardNo = boost::json::value_to<std::string>(obj["CardNo"]);
                    }

                    response_obj["State"] = state;
                    response_obj["CardNo"] = cardNo;
                }
                else
                {
                    res.result(boost::beast::http::status::not_found);
                    response_obj["error"] = "Not Found";
                }

                res.body() = boost::json::serialize(response_obj);
            }
        }
        catch (const boost::json::system_error& e)
        {
            res.result(boost::beast::http::status::bad_request);
            res.body() = "Failed to parse Json: " + std::string(e.what());
        }
    }
    else
    {
        Logger::getInstance()->FnLog("Bad request.", logFileName_, "TNG");
        res.result(boost::beast::http::status::bad_request);
        res.body() = "Method not allowed";
    }

    std::ostringstream oss;
    oss << "Outgoing res <= " << res.body();
    Logger::getInstance()->FnLog(oss.str(), logFileName_, "TNG");
    res.prepare_payload();
    return res;
}
