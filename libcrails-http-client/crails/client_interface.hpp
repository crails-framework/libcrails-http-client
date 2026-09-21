#pragma once
#include <memory>
#include <chrono>
#include <functional>
#include <crails/http.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/any_io_executor.hpp>

namespace Crails
{
  boost::asio::any_io_executor default_client_executor();

  struct ClientInterface : public std::enable_shared_from_this<ClientInterface>
  {
    typedef boost::beast::http::request<boost::beast::http::string_body>   Request;
    typedef boost::beast::http::response<boost::beast::http::string_body>  Response;
    typedef std::function<void(const Response&, boost::beast::error_code)> AsyncCallback;
    typedef std::function<void(boost::beast::error_code)>                  ConnectCallback;
    typedef std::chrono::steady_clock                                      Clock;

    static inline Clock::duration default_timeout = std::chrono::seconds(30);

    virtual ~ClientInterface() {}

    // Blocking API
    virtual void                             connect() = 0;
    virtual void                             disconnect() = 0;
    virtual Response                         query(const Request& request) = 0;
    virtual boost::asio::awaitable<void>     co_connect() = 0;
    virtual boost::asio::awaitable<void>     co_disconnect() = 0;
    virtual boost::asio::awaitable<Response> co_query(Request request) = 0;
    void                                     async_connect(ConnectCallback);
    void                                     async_disconnect(ConnectCallback);
    void                                     async_query(const Request& request, AsyncCallback);

    bool            is_connected() const { return connected; }
    Clock::duration get_timeout() const { return timeout; }
    void            set_timeout(Clock::duration value) { timeout = value; }

  protected:
    bool            connected = false;
    Clock::duration timeout = default_timeout;
  };
}
