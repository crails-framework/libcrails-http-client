#include "http_query.hpp"
#include "client.hpp"
#include <crails/logger.hpp>
#include <crails/server.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/asio/detached.hpp>

using namespace std;
using namespace Crails;

static string port_string(unsigned short port)
{
  return to_string(static_cast<unsigned int>(port));
}

static string host_header(const Url& url)
{
  const unsigned short default_port = url.ssl ? 443 : 80;

  if (url.port == default_port)
    return url.host;
  return url.host + ':' + port_string(url.port);
}

namespace Crails
{
  boost::asio::any_io_executor default_client_executor()
  {
    return Crails::Server::get_io_context().get_executor();
  }

  ClientInterface::Request make_request(HttpVerb verb, const Url& url)
  {
    ClientInterface::Request request{verb, '/' + url.target, 11};

    request.set(HttpHeader::host, host_header(url));
    return request;
  }
}

namespace Crails::Detail
{
  void start_http_query(Url url, ClientInterface::Request request, QueryCallback callback)
  {
    boost::asio::co_spawn(default_client_executor(),
      [url = std::move(url), request = std::move(request), callback]() mutable -> boost::asio::awaitable<void>
    {
      boost::system::error_code    ec;
      ClientInterface::Response    response;

      try { response = co_await co_http_query(std::move(url), std::move(request)); }
      catch (...) { ec = std::make_error_code(std::errc::network_unreachable); }
      callback(ec, response);
    }, log_callback_exception);
  }
}

static boost::asio::awaitable<ClientInterface::Response> co_http_query_on(boost::asio::io_context& ioc, Url url, ClientInterface::Request request, bool close_in_background)
{
  std::shared_ptr<ClientInterface> client;

  if (url.host.empty())
    throw boost::system::system_error(boost::asio::error::invalid_argument, "Crails::co_http_query: invalid url");
  if (url.ssl)
    client = Ssl::Client::create(url.host, url.port, ioc);
  else
    client = Crails::Client::create(url.host, url.port, ioc);
  if (request.find(HttpHeader::host) == request.end())
    request.set(HttpHeader::host, host_header(url));
  co_await client->co_connect();
  auto response = co_await client->co_query(std::move(request));
  if (close_in_background)
  { // The caller has its answer already: the connection can close in the background.
    boost::asio::co_spawn(ioc.get_executor(), [client]() -> boost::asio::awaitable<void>
    {
      try { co_await client->co_disconnect(); }
      catch (...) { logger << Logger::Debug << "Crails::co_http_query: closing the connection failed" << Logger::endl; }
    }, boost::asio::detached);
  }
  co_return response;
}

boost::asio::awaitable<ClientInterface::Response> Crails::co_http_query(Url url, ClientInterface::Request request)
{
  co_return co_await co_http_query_on(Crails::Server::get_io_context(), std::move(url), std::move(request), true);
}

boost::asio::awaitable<ClientInterface::Response> Crails::co_http_query(Url url)
{
  auto request = make_request(HttpVerb::get, url);

  co_return co_await co_http_query(std::move(url), std::move(request));
}

ClientInterface::Response Crails::http_query(Url url, ClientInterface::Request request)
{
  boost::asio::io_context ioc(1);
  auto future = boost::asio::co_spawn(ioc, co_http_query_on(ioc, std::move(url), std::move(request), false), boost::asio::use_future);

  ioc.run();
  return future.get();
}

ClientInterface::Response Crails::http_query(Url url)
{
  auto request = make_request(HttpVerb::get, url);

  return http_query(std::move(url), std::move(request));
}
