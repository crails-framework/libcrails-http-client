#include "client.hpp"
#include <crails/server.hpp>
#include <crails/logger.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/asio/ssl/host_name_verification.hpp>
#include <boost/asio/ssl/error.hpp>
#include <boost/asio/buffer.hpp>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <sstream>

using namespace std;
using namespace Crails;

static bool is_secret_header(boost::beast::http::field name)
{
  using boost::beast::http::field;

  switch (name)
  {
  case field::authorization:
  case field::proxy_authorization:
  case field::cookie:
  case field::set_cookie:
    return true;
  default:
    break ;
  }
  return false;
}

template<typename MESSAGE>
static void describe_headers(ostream& stream, const MESSAGE& message)
{
  for (const auto& field : message)
  {
    stream << "\n  " << field.name_string() << ": ";
    if (is_secret_header(field.name()))
      stream << "[redacted]";
    else
      stream << field.value();
  }
}

static string log_query(const Client::Request& request)
{
  stringstream stream;

  stream << "Crails::Client: query " << request.method_string() << ' ' << request.target();
  describe_headers(stream, request);
  stream << "\n" << request.body();
  return stream.str();
}

static string log_response(const shared_ptr<Client::Response>& response)
{
  stringstream stream;

  stream << "Crails::Client: received " << response->result_int() << ' ' << response->reason();
  describe_headers(stream, *response);
  stream << "\n" << response->body();
  return stream.str();
}

static boost::asio::ssl::context make_ssl_context()
{
  boost::asio::ssl::context ctx(boost::asio::ssl::context::tls_client);

  SSL_CTX_set_min_proto_version(ctx.native_handle(), TLS1_2_VERSION);
  ctx.set_default_verify_paths();
  return ctx;
}

static bool is_ip_address(const string_view host)
{
  boost::system::error_code ec;

  boost::asio::ip::make_address(host, ec);
  return !ec;
}

static string port_string(unsigned short port)
{
  return to_string(static_cast<unsigned int>(port));
}

template<typename T>
static boost::asio::awaitable<T> on_strand(const ClientStrand& strand, boost::asio::awaitable<T> operation)
{
  co_return co_await boost::asio::co_spawn(strand, std::move(operation), boost::asio::use_awaitable);
}

/*
 * Shared
 */
template<typename STREAM>
static Client::Response http_query(STREAM& stream, const Client::Request& request)
{
  Client::Response          response;
  boost::beast::flat_buffer buffer;

  logger << Logger::Debug << std::bind(log_query, request) << Logger::endl;
  boost::beast::http::write(stream, request);
  boost::beast::http::read(stream, buffer, response);
  logger << Logger::Debug << std::bind(log_response, make_shared<Client::Response>(response)) << Logger::endl;
  return response;
}

template<typename STREAM>
static boost::asio::awaitable<Client::Response> http_co_query(ClientInterface& client, STREAM& stream, Client::Request request)
{
  Client::Response          response;
  boost::beast::flat_buffer buffer;

  request.prepare_payload();
  logger << Logger::Debug << std::bind(log_query, request) << Logger::endl;
  boost::beast::get_lowest_layer(stream).expires_after(client.get_timeout());
  co_await boost::beast::http::async_write(stream, request, boost::asio::use_awaitable);
  boost::beast::get_lowest_layer(stream).expires_after(client.get_timeout());
  co_await boost::beast::http::async_read(stream, buffer, response, boost::asio::use_awaitable);
  logger << Logger::Debug << std::bind(log_response, make_shared<Client::Response>(response)) << Logger::endl;
  co_return response;
}

/*
 * HTTPS Client
 */
Ssl::Client::Client(std::string_view host, unsigned short port) :
  Client(host, port, Crails::Server::get_io_context())
{
}

Ssl::Client::Client(std::string_view host, unsigned short port, boost::asio::io_context& ioc) :
  host(host),
  port(port),
  strand(boost::asio::make_strand(ioc)),
  resolver(strand),
  ctx(make_ssl_context()),
  stream(strand, ctx)
{
  logger << Logger::Debug << "Ssl::Client constructor, host=" << host << ", port=" << port << Logger::endl;
  // Set SNI Hostname. It isn't valid for IP addresses.
  if (!is_ip_address(host) && !SSL_set_tlsext_host_name(stream.native_handle(), this->host.c_str()))
  {
    boost::beast::error_code ec{static_cast<int>(::ERR_get_error()), boost::asio::error::get_ssl_category()};
    throw boost::beast::system_error{ec};
  }
  set_verify_peer(true);
}

std::shared_ptr<Ssl::Client> Ssl::Client::create(std::string_view host, unsigned short port)
{
  return std::make_shared<Ssl::Client>(host, port);
}

std::shared_ptr<Ssl::Client> Ssl::Client::create(std::string_view host, unsigned short port, boost::asio::io_context& ioc)
{
  return std::make_shared<Ssl::Client>(host, port, ioc);
}

Ssl::Client::~Client()
{
  logger << Logger::Debug << "Ssl::~Client" << Logger::endl;
  if (connected)
  {
    boost::beast::error_code ignored;

    SSL_shutdown(stream.native_handle()); // sends close_notify, does not wait for the peer's
    boost::beast::get_lowest_layer(stream).socket().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ignored);
    boost::beast::get_lowest_layer(stream).socket().close(ignored);
  }
}

void Ssl::Client::set_verify_peer(bool verify)
{
  if (verify)
  {
    stream.set_verify_mode(boost::asio::ssl::verify_peer);
    stream.set_verify_callback(boost::asio::ssl::host_name_verification(host));
  }
  else
    stream.set_verify_mode(boost::asio::ssl::verify_none);
}

void Ssl::Client::add_certificate_authority(const std::string& pem)
{
  ctx.add_certificate_authority(boost::asio::buffer(pem.data(), pem.size()));
}

void Ssl::Client::connect()
{
  auto const results = resolver.resolve(host, port_string(port));

  boost::beast::get_lowest_layer(stream).connect(results);
  stream.handshake(boost::asio::ssl::stream_base::client);
  connected = true;
}

boost::asio::awaitable<void> Ssl::Client::do_connect()
{
  auto results = co_await resolver.async_resolve(host, port_string(port), boost::asio::use_awaitable);

  boost::beast::get_lowest_layer(stream).expires_after(get_timeout());
  co_await boost::beast::get_lowest_layer(stream).async_connect(results, boost::asio::use_awaitable);
  boost::beast::get_lowest_layer(stream).expires_after(get_timeout());
  co_await stream.async_handshake(boost::asio::ssl::stream_base::client, boost::asio::use_awaitable);
  connected = true;
}

static bool is_clean_ssl_shutdown(boost::beast::error_code& ec)
{
  // Rationale:
  // http://stackoverflow.com/questions/25587403/boost-asio-ssl-async-shutdown-always-finishes-with-an-error
  if (ec == boost::asio::error::eof || ec == boost::asio::ssl::error::stream_truncated)
    ec = {};
  return !ec;
}

void Crails::Ssl::Client::disconnect()
{
  // Gracefully close the stream
  boost::beast::error_code ec;

  logger << Logger::Debug << "Crails::Ssl::Client::disconnect host=" << host << ", port=" << port << Logger::endl;
  connected = false;
  stream.shutdown(ec);
  if (!is_clean_ssl_shutdown(ec))
    logger << Logger::Error << "Crails::Ssl::Client::disconnect: error occured " << boost::beast::system_error{ec}.what() << Logger::endl;
  boost::beast::error_code ignored;
  boost::beast::get_lowest_layer(stream).socket().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ignored);
  boost::beast::get_lowest_layer(stream).socket().close(ignored);
}

boost::asio::awaitable<void> Ssl::Client::do_disconnect()
{
  boost::beast::error_code ec, ignored;

  if (!connected)
    co_return ;
  connected = false;
  boost::beast::get_lowest_layer(stream).expires_after(get_timeout());
  try
  {
    co_await stream.async_shutdown(boost::asio::use_awaitable);
  }
  catch (const boost::system::system_error& error)
  {
    ec = error.code();
  }
  if (!is_clean_ssl_shutdown(ec))
    logger << Logger::Debug << "Crails::Ssl::Client::co_disconnect: " << ec.message() << Logger::endl;
  boost::beast::get_lowest_layer(stream).socket().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ignored);
  boost::beast::get_lowest_layer(stream).socket().close(ignored);
}

Client::Response Ssl::Client::query(const Client::Request& request)
{
  return ::http_query(stream, request);
}

boost::asio::awaitable<Client::Response> Ssl::Client::do_query(Client::Request request)
{
  co_return co_await http_co_query(*this, stream, std::move(request));
}

boost::asio::awaitable<void> Ssl::Client::co_connect()
{
  co_await on_strand(strand, do_connect());
}

boost::asio::awaitable<void> Ssl::Client::co_disconnect()
{
  co_await on_strand(strand, do_disconnect());
}

boost::asio::awaitable<Client::Response> Ssl::Client::co_query(Client::Request request)
{
  co_return co_await on_strand(strand, do_query(std::move(request)));
}

/*
 * HTTP Client
 */
Client::Client(std::string_view host, unsigned short port) :
  Client(host, port, Crails::Server::get_io_context())
{
}

Client::Client(std::string_view host, unsigned short port, boost::asio::io_context& ioc) :
  host(host), port(port),
  strand(boost::asio::make_strand(ioc)),
  resolver(strand),
  stream(strand)
{
  logger << Logger::Debug << "Crails::Client: constructor, host=" << this->host << ", port=" << port << Logger::endl;
}

std::shared_ptr<Client> Client::create(std::string_view host, unsigned short port)
{
  return std::make_shared<Client>(host, port);
}

std::shared_ptr<Client> Client::create(std::string_view host, unsigned short port, boost::asio::io_context& ioc)
{
  return std::make_shared<Client>(host, port, ioc);
}

Client::~Client()
{
  logger << Logger::Debug << "Crails::~Client" << Logger::endl;
  if (connected)
    disconnect();
}

void Client::connect()
{
  const auto results = resolver.resolve(host, port_string(port));

  stream.connect(results);
  connected = true;
}

boost::asio::awaitable<void> Client::do_connect()
{
  auto results = co_await resolver.async_resolve(host, port_string(port), boost::asio::use_awaitable);

  stream.expires_after(get_timeout());
  co_await stream.async_connect(results, boost::asio::use_awaitable);
  connected = true;
}

void Client::disconnect()
{
  try
  {
    logger << Logger::Debug << "Crails::Client::disconnect, host=" << host << ", port=" << port << Logger::endl;
    connected = false;
    stream.socket().shutdown(boost::asio::ip::tcp::socket::shutdown_both);
  }
  catch (const std::exception& e)
  {
    logger << Logger::Error << "Crails::Client::disconnect failed: " << e.what() << Logger::endl;
  }
}

boost::asio::awaitable<void> Client::do_disconnect()
{
  if (connected)
  {
    boost::beast::error_code ec;

    connected = false;
    stream.socket().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
    stream.socket().close(ec);
  }
  co_return ;
}

Client::Response Client::query(const Request& request)
{
  return ::http_query(stream, request);
}

boost::asio::awaitable<Client::Response> Client::do_query(Request request)
{
  co_return co_await http_co_query(*this, stream, std::move(request));
}

boost::asio::awaitable<void> Client::co_connect()
{
  co_await on_strand(strand, do_connect());
}

boost::asio::awaitable<void> Client::co_disconnect()
{
  co_await on_strand(strand, do_disconnect());
}

boost::asio::awaitable<Client::Response> Client::co_query(Request request)
{
  co_return co_await on_strand(strand, do_query(std::move(request)));
}
