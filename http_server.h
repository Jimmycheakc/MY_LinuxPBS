#pragma once

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/strand.hpp>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <vector>


using FailCallback = std::function<void(const std::string&)>;
using RequestHandler = std::function<boost::beast::http::response<boost::beast::http::string_body>(
    const boost::beast::http::request<boost::beast::http::string_body>&)>;


// Class Session
class Session : public std::enable_shared_from_this<Session>
{
public:
    Session(boost::asio::ip::tcp::socket socket, boost::asio::strand<boost::asio::io_context::executor_type> strand, FailCallback on_fail, RequestHandler on_request);

    void run();

private:
    boost::asio::ip::tcp::socket socket_;
    boost::asio::strand<boost::asio::io_context::executor_type> strand_;
    boost::beast::flat_buffer buffer_;
    boost::beast::http::request<boost::beast::http::string_body> req_;
    FailCallback fail_callback_;
    RequestHandler request_handler_;

    void do_read();
    void do_write(boost::beast::http::response<boost::beast::http::string_body> res);
};



// Class HttpServer
class HttpServer : public std::enable_shared_from_this<HttpServer>
{
public:
    HttpServer(boost::asio::io_context& ioc, boost::asio::ip::tcp::endpoint endpoint, FailCallback on_fail, RequestHandler on_request);
    void run();

private:
    boost::asio::io_context& ioc_;
    boost::asio::strand<boost::asio::io_context::executor_type> strand_;
    boost::asio::ip::tcp::acceptor acceptor_;
    FailCallback fail_callback_;
    RequestHandler request_handler_;
    
    void do_accept();
};
