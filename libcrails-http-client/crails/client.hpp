#ifndef  CRAILS_HTTP_CLIENT_HPP
# define CRAILS_HTTP_CLIENT_HPP

# include <chrono>
# include <functional>
# include <memory>
# include <string>
# include <string_view>
# include <boost/asio/any_io_executor.hpp>
# include <boost/asio/associated_executor.hpp>
# include <boost/asio/async_result.hpp>
# include <boost/asio/awaitable.hpp>
# include <boost/asio/dispatch.hpp>
# include <boost/asio/io_context.hpp>
# include <boost/asio/ip/tcp.hpp>
# include <boost/asio/ssl/context.hpp>
# include <boost/asio/ssl/stream.hpp>
# include <boost/asio/strand.hpp>
# include <boost/beast/core.hpp>
# include <boost/beast/ssl.hpp>
# include <crails/http.hpp>
# include <crails/url.hpp>
# include "client_interface.hpp"

namespace Crails
{
  typedef boost::asio::strand<boost::asio::io_context::executor_type> ClientStrand;

  class Client : public ClientInterface
  {
  public:
    static std::shared_ptr<Client> create(std::string_view host, unsigned short port = 80);
    static std::shared_ptr<Client> create(std::string_view host, unsigned short port, boost::asio::io_context&);

    Client(std::string_view host, unsigned short port = 80);
    Client(std::string_view host, unsigned short port, boost::asio::io_context&);
    virtual ~Client() override;

    void                             connect() override;
    void                             disconnect() override;
    Response                         query(const Request&) override;
    boost::asio::awaitable<void>     co_connect() override;
    boost::asio::awaitable<void>     co_disconnect() override;
    boost::asio::awaitable<Response> co_query(Request) override;

  private:
    boost::asio::awaitable<void>     do_connect();
    boost::asio::awaitable<void>     do_disconnect();
    boost::asio::awaitable<Response> do_query(Request);

    const std::string              host;
    const unsigned short           port;
    ClientStrand                   strand;
    boost::asio::ip::tcp::resolver resolver;
    boost::beast::tcp_stream       stream;
  };

  namespace Ssl
  {
    class Client : public ClientInterface
    {
    public:
      static std::shared_ptr<Client> create(std::string_view host, unsigned short port = 443);
      static std::shared_ptr<Client> create(std::string_view host, unsigned short port, boost::asio::io_context&);

      Client(std::string_view host, unsigned short port = 443);
      Client(std::string_view host, unsigned short port, boost::asio::io_context&);
      virtual ~Client() override;

      void                             connect() override;
      void                             disconnect() override;
      Response                         query(const Request&) override;
      boost::asio::awaitable<void>     co_connect() override;
      boost::asio::awaitable<void>     co_disconnect() override;
      boost::asio::awaitable<Response> co_query(Request) override;

      void set_verify_peer(bool);
      void add_certificate_authority(const std::string& pem);

    private:
      boost::asio::awaitable<void>     do_connect();
      boost::asio::awaitable<Response> do_query(Request);
      boost::asio::awaitable<void>     do_disconnect();

      const std::string              host;
      const unsigned short           port;
      ClientStrand                   strand;
      boost::asio::ip::tcp::resolver resolver;
      boost::asio::ssl::context      ctx;
      boost::beast::ssl_stream<boost::beast::tcp_stream> stream;
    };
  }
}

#endif
