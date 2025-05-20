#pragma once

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/asio.hpp>
#include <boost/asio/strand.hpp>
#include <iostream>
#include <memory>
#include <string>
#include <functional>

// Class ClientSession
class ClientSession : public std::enable_shared_from_this<ClientSession>
{

public:
    using ResponseHandler = std::function<void(const boost::beast::http::response<boost::beast::http::string_body>&)>;
    using FailureHandler = std::function<void(const std::string& what, const boost::beast::error_code& ec)>;
    
    ClientSession(boost::asio::io_context& ioc);
    void startPost(const std::string& host, const std::string& port,
                const std:: string& target, const std::string& body,
                ResponseHandler cb, FailureHandler err_cb);

private:
    boost::asio::strand<boost::asio::io_context::executor_type> strand_;
    boost::asio::ip::tcp::resolver resolver_;
    boost::beast::tcp_stream stream_;
    boost::beast::flat_buffer buffer_;
    boost::beast::http::request<boost::beast::http::string_body> req_;
    boost::beast::http::response<boost::beast::http::string_body> res_;
    ResponseHandler callback_;
    FailureHandler failure_callback_;
    static constexpr std::chrono::seconds timeout{30};

    void on_resolve(boost::beast::error_code ec, boost::asio::ip::tcp::resolver::results_type results);
    void on_connect(boost::beast::error_code ec, boost::asio::ip::tcp::resolver::results_type::endpoint_type);
    void on_write(boost::beast::error_code ec, std::size_t);
    void on_read(boost::beast::error_code ec, std::size_t);
    void fail(boost::beast::error_code ec, const char* what);
};



// Class HttpClient
class HttpClient
{
public:
    HttpClient(boost::asio::io_context& ioc);
    void post(const std::string& host, const std::string& port,
                const std:: string& target, const std::string& body,
                ClientSession::ResponseHandler cb, ClientSession::FailureHandler err_cb);

private:
    boost::asio::io_context& ioc_;
};
