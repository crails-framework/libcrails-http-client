#include "client_interface.hpp"
#include <crails/logger.hpp>
#include <boost/asio/co_spawn.hpp>

using namespace std;
using namespace Crails;

static boost::system::error_code current_error()
{
  try { throw ; }
  catch (const boost::system::system_error& error) { return error.code(); }
  catch (const std::exception& error)
  {
    logger << Logger::Error << "Crails::Client: unexpected exception: " << error.what() << Logger::endl;
  }
  catch (...)
  {
    logger << Logger::Error << "Crails::Client: unexpected exception" << Logger::endl;
  }
  return boost::asio::error::fault;
}

namespace Crails::Detail
{
  void log_callback_exception(std::exception_ptr error)
  {
    if (!error)
      return ;
    try { std::rethrow_exception(error); }
    catch (const std::exception& e) { logger << Logger::Error << "Crails::Client: exception escaped a callback: " << e.what() << Logger::endl; }
    catch (...) { logger << Logger::Error << "Crails::Client: exception escaped a callback" << Logger::endl; }
  }
}

void ClientInterface::async_connect(ConnectCallback callback)
{
  boost::asio::co_spawn(default_client_executor(),
    [self = shared_from_this(), callback]() -> boost::asio::awaitable<void>
  {
    boost::system::error_code ec;

    try { co_await self->co_connect(); }
    catch (...) { ec = current_error(); }
    callback(ec);
  }, Detail::log_callback_exception);
}

void ClientInterface::async_query(const Request& request, AsyncCallback callback)
{
  boost::asio::co_spawn(default_client_executor(),
    [self = shared_from_this(), request, callback]() -> boost::asio::awaitable<void>
  {
    boost::system::error_code ec;
    Response                  response;

    try { response = co_await self->co_query(request); }
    catch (...) { ec = current_error(); }
    callback(response, ec);
  }, Detail::log_callback_exception);
}

void ClientInterface::async_disconnect(ConnectCallback callback)
{
  boost::asio::co_spawn(default_client_executor(),
    [self = shared_from_this(), callback]() -> boost::asio::awaitable<void>
  {
    boost::system::error_code ec;

    try { co_await self->co_disconnect(); }
    catch (...) { ec = current_error(); }
    callback(ec);
  }, Detail::log_callback_exception);
}
