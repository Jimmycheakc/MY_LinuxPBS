#include "http_server.h"

// Class Session
Session::Session(boost::asio::ip::tcp::socket socket, boost::asio::strand<boost::asio::io_context::executor_type> strand, FailCallback on_fail, RequestHandler on_request)
    : socket_(std::move(socket)),
    strand_(std::move(strand))
{
    request_handler_ = std::move(on_request);
    fail_callback_ = std::move(on_fail);
}

void Session::run()
{
    do_read();
}

void Session::do_read()
{
    auto self = shared_from_this();
    boost::beast::http::async_read(socket_, buffer_, req_,
        boost::asio::bind_executor(strand_,
            [this, self](boost::beast::error_code ec, std::size_t) {
                if (ec)
                {
                    if (fail_callback_)
                    {
                        fail_callback_("Read failed: " + ec.message());
                    }
                    return;
                }
                auto res = request_handler_(req_);
                do_write(std::move(res));
            }
        )
    );
}

void Session::do_write(boost::beast::http::response<boost::beast::http::string_body> res)
{
    auto self = shared_from_this();
    auto sp = std::make_shared<boost::beast::http::response<boost::beast::http::string_body>>(std::move(res));
    boost::beast::http::async_write(socket_, *sp,
        boost::asio::bind_executor(strand_,
            [this, self, sp](boost::beast::error_code ec, std::size_t)
            {
                socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_send, ec);
                if (ec && fail_callback_)
                {
                    fail_callback_("Write failed: " + ec.message());
                }
            }
        )
    );
}



// Class HttpServer
HttpServer::HttpServer(boost::asio::io_context& ioc, boost::asio::ip::tcp::endpoint endpoint, FailCallback on_fail, RequestHandler on_request)
    : ioc_(ioc),
    acceptor_(boost::asio::make_strand(ioc)),
    strand_(boost::asio::make_strand(ioc))
{
    request_handler_ = std::move(on_request);
    fail_callback_ = std::move(on_fail);
    boost::beast::error_code ec;

    // Open the acceptor
    acceptor_.open(endpoint.protocol(), ec);
    if (ec)
    {
        if (fail_callback_)
        {
            fail_callback_("Open error: " + ec.message());
        }
        return;
    }

    // Allow address reuse
    acceptor_.set_option(boost::asio::socket_base::reuse_address(true), ec);
    if (ec)
    {
        if (fail_callback_)
        {
            fail_callback_("Set option error: " + ec.message());
        }
        return;
    }

    // Bind to server address
    acceptor_.bind(endpoint, ec);
    if (ec)
    {
        if (fail_callback_)
        {
            fail_callback_("Bind error: " + ec.message());
        }
        return;
    }

    // Start listening for connections
    acceptor_.listen(boost::asio::socket_base::max_listen_connections, ec);
    if (ec)
    {
        if (fail_callback_)
        {
            fail_callback_("Listen error: " + ec.message());
        }
        return;
    }
}

void HttpServer::run()
{
    do_accept();
}

void HttpServer::do_accept()
{
    acceptor_.async_accept(boost::asio::make_strand(ioc_), 
        [this](boost::beast::error_code ec, boost::asio::ip::tcp::socket socket)
        {
            if (!ec)
            {
                std::make_shared<Session>(std::move(socket), strand_, fail_callback_, request_handler_)->run();
            }
            else if (fail_callback_)
            {
                fail_callback_("Accept error: " + ec.message());
            }
            do_accept();
        }
    );
}