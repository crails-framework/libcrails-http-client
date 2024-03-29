#include "client.hpp"
#include <crails/server.hpp>
#include <crails/logger.hpp>
#include <boost/lexical_cast.hpp>
#include <sstream>

using namespace std;
using namespace Crails;

static string log_query(const Client::Request& request)
{
  stringstream stream;

  stream << request;
  return string("Crails::Client: query ") + stream.str();
}

template<typename STREAM>
static Client::Response http_query(STREAM& stream, const Client::Request& request)
{
  Client::Response          response;
  boost::beast::flat_buffer buffer;

  logger << Logger::Debug << std::bind(log_query, request) << Logger::endl;
  boost::beast::http::write(stream, request);
  boost::beast::http::read(stream, buffer, response);
  logger << Logger::Debug << "Crails::Client: received " << response << Logger::endl;
  return response;
}

template<typename STREAM>
static void http_async_receive(STREAM& stream, std::function<void(const Client::Response&, boost::beast::error_code)> callback)
{
  auto buffer = make_shared<boost::beast::flat_buffer>();
  auto response = make_shared<Client::Response>();

  boost::beast::http::async_read(stream, *buffer, *response, [callback, buffer, response](boost::beast::error_code ec, size_t)
  {
    if (ec)
      logger << Logger::Error << "Crails::Client::http_async_receive failed: " << ec.message() << Logger::endl;
    else
      logger << Logger::Debug << "Crails::Client: received " << *response << Logger::endl;
    callback(*response, ec);
  });
}

template<typename STREAM>
static void http_async_query(STREAM& stream, const Client::Request& _request, std::function<void(const Client::Response&, boost::beast::error_code)> callback)
{
  auto request = make_shared<Client::Request>(_request);

  logger << Logger::Debug << std::bind(log_query, _request) << Logger::endl;
  boost::beast::http::async_write(stream, *request, [callback, &stream, request](boost::beast::error_code ec, size_t)
  {
    if (ec)
    {
      logger << Logger::Error << "Crails::Client::http_async_query failed: " << ec.message() << Logger::endl;
      callback(Client::Response(), ec);
    }
    else
      http_async_receive(stream, callback);
  });
}

/*
 * HTTPS Client
 */
Ssl::Client::Client(std::string_view host, unsigned short port) :
  host(host),
  port(port),
  ioc(Crails::Server::get_io_context()),
  ctx(boost::asio::ssl::context::tlsv12_client),
  stream(ioc, ctx)
{
  logger << Logger::Debug << "Ssl::Client constructor, host=" << host << ", port=" << port << Logger::endl;
  ctx.set_verify_mode(boost::asio::ssl::verify_none);
}

Ssl::Client::~Client()
{
  logger << Logger::Debug << "Ssl::~Client" << Logger::endl;
  if (connected)
    disconnect();
}

void Ssl::Client::connect()
{
  // These objects perform our I/O
  boost::asio::ip::tcp::resolver resolver(ioc);

  // Set SNI Hostname (many hosts need this to handshake successfully)
  if(!SSL_set_tlsext_host_name(stream.native_handle(), host.data()))
  {
    boost::beast::error_code ec{static_cast<int>(::ERR_get_error()), boost::asio::error::get_ssl_category()};
    throw boost::beast::system_error{ec};
  }

  // Look up the domain name
  auto const results = resolver.resolve(host, boost::lexical_cast<std::string>(port));

  // Make the connection on the IP address we get from a lookup
  boost::beast::get_lowest_layer(stream).connect(results);

  // Perform the SSL handshake
  stream.handshake(boost::asio::ssl::stream_base::client);
  connected = true;
}

void Crails::Ssl::Client::disconnect()
{
  // Gracefully close the stream
  boost::beast::error_code ec;

  logger << Logger::Debug << "Crails::Ssl::Client::disconnect host=" << host << ", port=" << port << Logger::endl;
  connected = false;
  stream.shutdown(ec);
  if (ec == boost::asio::error::eof)
  {
    // Rationale:
    // http://stackoverflow.com/questions/25587403/boost-asio-ssl-async-shutdown-always-finishes-with-an-error
    ec = {};
  }
  if (ec)
  {
    logger << Logger::Error << "Crails::Ssl::Client::disconnect: error occured " << boost::beast::system_error{ec}.what() << Logger::endl;
  }
}

Client::Response Ssl::Client::query(const Client::Request& request)
{
  return http_query(stream, request);
}

void Ssl::Client::async_query(const Client::Request& request, AsyncCallback callback)
{
  http_async_query(stream, request, callback);
}

/*
 * HTTP Client
 */
Client::Client(std::string_view host, unsigned short port) :
  host(host), port(port),
  stream(Crails::Server::get_io_context())
{
  logger << Logger::Debug << "Crails::Client: constructor, host=" << host << ", port=" << port << Logger::endl;
}

Client::~Client()
{
  logger << Logger::Debug << "Crails::~Client" << Logger::endl;
  if (connected)
    disconnect();
}

void Client::connect()
{
  boost::asio::ip::tcp::resolver resolver(Crails::Server::get_io_context());
  const auto results = resolver.resolve(host, boost::lexical_cast<std::string>(port));

  stream.connect(results);
  connected = true;
}

void Client::disconnect()
{
  try
  {
    logger << Logger::Debug << "Crails::Client::disconnect, host=" << host << ", port=" << port << Logger::endl;
    stream.socket().shutdown(boost::asio::ip::tcp::socket::shutdown_both);
    connected = false;
  }
  catch (const std::exception& e)
  {
    logger << Logger::Error << "Crails::Client::disconnect failed: " << e.what() << Logger::endl;
  }
}

Client::Response Client::query(const Request& request)
{
  return http_query(stream, request);
}

void Client::async_query(const Request& request, AsyncCallback callback)
{
  http_async_query(stream, request, callback);
}
