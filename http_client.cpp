#include "http_client.h"
#include "log.h"

ClientSession::ClientSession(boost::asio::io_context& ioc)
    : strand_(boost::asio::make_strand(ioc)),
    resolver_(strand_),
    stream_(strand_)
{

}

void ClientSession::startPost(const std::string& host, const std::string& port,
        const std:: string& target, const std::string& body,
        ResponseHandler cb, FailureHandler err_cb)
{
    callback_ = std::move(cb);
    failure_callback_ = std::move(err_cb);
    req_.version(11);
    req_.method(boost::beast::http::verb::post);
    req_.target(target);
    req_.set(boost::beast::http::field::host, host);
    req_.set(boost::beast::http::field::user_agent, BOOST_BEAST_VERSION_STRING);
    req_.set(boost::beast::http::field::content_type, "application/json");
    req_.body() = body;
    req_.prepare_payload();

    resolver_.async_resolve(host, port,
        boost::asio::bind_executor(strand_,
            boost::beast::bind_front_handler(&ClientSession::on_resolve, shared_from_this())
        )
    );
}

void ClientSession::on_resolve(boost::beast::error_code ec, boost::asio::ip::tcp::resolver::results_type results)
{
    if (ec)
    {
        return fail(ec, "resolve");
    }

    stream_.expires_after(timeout);
    stream_.async_connect(results,
        boost::asio::bind_executor(strand_,
            boost::beast::bind_front_handler(&ClientSession::on_connect, shared_from_this())
        )
    );
}

void ClientSession::on_connect(boost::beast::error_code ec, boost::asio::ip::tcp::resolver::results_type::endpoint_type)
{
    if (ec)
    {
        return fail(ec, "connect");
    }

    stream_.expires_after(timeout);
    boost::beast::http::async_write(stream_, req_,
        boost::asio::bind_executor(strand_,
            boost::beast::bind_front_handler(&ClientSession::on_write, shared_from_this())
        )
    );
}

void ClientSession::on_write(boost::beast::error_code ec, std::size_t)
{
    if (ec)
    {
        return fail(ec, "write");
    }

    stream_.expires_after(timeout);
    boost::beast::http::async_read(stream_, buffer_, res_,
        boost::asio::bind_executor(strand_,
            boost::beast::bind_front_handler(&ClientSession::on_read, shared_from_this())
        )
    );
}

void ClientSession::on_read(boost::beast::error_code ec, std::size_t)
{
    if (ec)
    {
        return fail(ec, "read");
    }

    if (callback_)
    {
        callback_(res_);
    }

    boost::beast::error_code shutdown_ec;
    stream_.socket().shutdown(boost::asio::ip::tcp::socket::shutdown_both, shutdown_ec);
}

void ClientSession::fail(boost::beast::error_code ec, const char* what)
{
    if (failure_callback_)
    {
        failure_callback_(what, ec);
    }
}


// Class HttpClient
HttpClient::HttpClient(boost::asio::io_context& ioc)
    : ioc_(ioc)
{

}

void HttpClient::post(const std::string& host, const std::string& port,
    const std:: string& target, const std::string& body,
    ClientSession::ResponseHandler cb, ClientSession::FailureHandler err_cb)
{
    auto session = std::make_shared<ClientSession>(ioc_);
    session->startPost(host, port, target, body, std::move(cb), std::move(err_cb));
}