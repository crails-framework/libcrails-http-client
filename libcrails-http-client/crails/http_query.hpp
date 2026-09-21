#pragma once
#include <boost/asio/associated_executor.hpp>
#include <boost/asio/async_result.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <crails/url.hpp>
#include "client_interface.hpp"

namespace Crails
{
  namespace Detail
  {
    typedef std::function<void(boost::beast::error_code, const ClientInterface::Response&)> QueryCallback;

    void start_http_query(Url, ClientInterface::Request, QueryCallback);
    void log_callback_exception(std::exception_ptr);
  }

  ClientInterface::Request make_request(HttpVerb, const Url&);

  boost::asio::awaitable<ClientInterface::Response> co_http_query(Url url, ClientInterface::Request request);
  boost::asio::awaitable<ClientInterface::Response> co_http_query(Url url);
  ClientInterface::Response                         http_query(Url url, ClientInterface::Request request);
  ClientInterface::Response                         http_query(Url url);

  template<typename Token>
  auto async_http_query(Url url, ClientInterface::Request request, Token&& token)
  {
    return boost::asio::async_initiate<Token, void(boost::system::error_code, ClientInterface::Response)>(
      [](auto handler, Url url, ClientInterface::Request request)
      {
        auto shared   = std::make_shared<decltype(handler)>(std::move(handler));
        auto executor = boost::asio::get_associated_executor(*shared, default_client_executor());

        Detail::start_http_query(std::move(url), std::move(request),
          [shared, executor](boost::beast::error_code ec, const ClientInterface::Response& response)
        {
          boost::asio::dispatch(executor, [shared, ec, response]() mutable { (*shared)(ec, std::move(response)); });
        });
      },
      token, std::move(url), std::move(request)
    );
  }

  template<typename Token>
  auto async_http_query(const Url& url, Token&& token)
  {
    return async_http_query(url, make_request(HttpVerb::get, url), std::forward<Token>(token));
  }
}
