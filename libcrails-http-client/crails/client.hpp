#ifndef  CRAILS_HTTP_CLIENT_HPP
# define CRAILS_HTTP_CLIENT_HPP

# include <memory>
# include <string>
# include <string_view>
# include <boost/asio/ip/tcp.hpp>
# include <boost/asio/ssl/stream.hpp>
# include <boost/beast/core.hpp>
# include <boost/beast/ssl.hpp>
# include <crails/http.hpp>

namespace Crails
{
  struct ClientInterface
  {
    typedef boost::beast::http::request<boost::beast::http::string_body>  Request;
    typedef boost::beast::http::response<boost::beast::http::string_body> Response;
    typedef std::function<void(const Response&, boost::beast::error_code)>
      AsyncCallback;

    virtual ~ClientInterface() {}
    virtual void connect() = 0;
    virtual void disconnect() = 0;
    virtual Response query(const Request& request) = 0;
    virtual void async_query(const Request& request, AsyncCallback) = 0;

  protected:
    bool connected = false;
  };

  class Client : public ClientInterface, std::enable_shared_from_this<Client>
  {
    typedef boost::asio::ip::tcp::resolver Resolver;
  public:
    Client(std::string_view host, unsigned short port = 80);
    virtual ~Client() override;

    Response query(const Request&) override;
    void     async_query(const Request&, AsyncCallback) override;
    void     connect() override;
    void     disconnect() override;

  private:
    const std::string_view    host;
    const unsigned short      port;
    boost::beast::tcp_stream  stream;
  };

  namespace Ssl
  {
    class Client : public ClientInterface, public std::enable_shared_from_this<Client>
    {
    public:
      Client(std::string_view host, unsigned short port = 443);
      virtual ~Client() override;

      Response query(const Request&) override;
      void     async_query(const Request&, AsyncCallback) override;
      void     connect() override;
      void     disconnect() override;

    private:
      const std::string_view host;
      const unsigned short   port;
      boost::asio::io_context& ioc;
      boost::asio::ssl::context ctx;
      boost::beast::ssl_stream<boost::beast::tcp_stream> stream;
    };
  }
}

#endif
