#pragma once

#include <iostream>
#include <string>
#include <thread>
#include <mutex>
#include <memory>
#include "boost/asio.hpp"
#include "boost/beast/core.hpp"
#include "boost/beast/http.hpp"
#include "boost/beast/version.hpp"
#include "boost/asio/strand.hpp"
#include "http_client.h"
#include "http_server.h"


// Touch N Go Reader Class
class TnG_Reader
{

public:

    static TnG_Reader* getInstance();
    void FnTnGReaderInit(const std::string& clientHost, const std::string& clientPort, const std::string& serverHost, const std::string& serverPort);
    void FnTnGReader_PayRequest(int payAmt, int discountAmt, long int enterTime, long int payTime, const std::string& orderId);
    void FnTnGReader_PayCancel(const std::string& orderId);
    void FnTnGReader_EnableReader(int enable, const std::string& orderId);
    void FnTnGReaderClose();

    /**
     * Singleton TnG_Reader should not be cloneable. 
     */
    TnG_Reader(TnG_Reader& tng) = delete;

    /**
     * Singleton TnG_Reader should not be assignable.
     */
    void operator=(const TnG_Reader&) = delete;

private:
    static TnG_Reader* TnG_Reader_;
    static std::mutex mutex_;
    boost::asio::io_context ioContext_;
    boost::asio::io_context::work work_;
    boost::asio::strand<boost::asio::io_context::executor_type> strand_;
    std::atomic<bool> initialized_;
    std::thread ioContextThread_;
    std::string logFileName_;
    std::string remoteServerHost_;
    std::string remoteServerPort_;
    std::string listenHost_;
    std::string listenPort_;
    std::shared_ptr<HttpClient> httpClient_;
    std::shared_ptr<HttpServer> httpServer_;
    TnG_Reader();
    void startIoContextThread();
    void payRequestSuccessCb(const boost::beast::http::response<boost::beast::http::string_body>& res);
    void payRequestFailureCb(const std::string& what, const boost::beast::error_code& ec);
    void payCancelSuccessCb(const boost::beast::http::response<boost::beast::http::string_body>& res);
    void payCancelFailureCb(const std::string& what, const boost::beast::error_code& ec);
    void enableReaderSuccessCb(const boost::beast::http::response<boost::beast::http::string_body>& res);
    void enableReaderFailureCb(const std::string& what, const boost::beast::error_code& ec);
    void serverHandleFailureCb(const std::string& what);
    boost::beast::http::response<boost::beast::http::string_body> serverHandleRequestCb(const boost::beast::http::request<boost::beast::http::string_body>& req);
};