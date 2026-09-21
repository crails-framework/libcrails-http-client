#ifndef  CRAILS_QUERY_CONTROLLER_HPP
# define CRAILS_QUERY_CONTROLLER_HPP

# include <type_traits>
# include <boost/asio/awaitable.hpp>
# include <crails/url.hpp>
# include <crails/controller.hpp>
# include <crails/context.hpp>
# include "../client.hpp"

namespace Crails
{
  template<typename SUPER = Crails::Controller>
  class QueryController : public SUPER
  {
    static_assert(
      std::is_base_of_v<Crails::CoroutineController, SUPER>,
      "Crails::QueryController needs Crails::CoroutineController as its base"
    );
  public:
    QueryController(Context& context) : SUPER(context)
    {
    }

  protected:
    static boost::asio::awaitable<Client::Response> co_http_query(Url url)
    {
      return Crails::co_http_query(std::move(url));
    }

    static boost::asio::awaitable<Client::Response> co_http_query(Url url, Client::Request request)
    {
      return Crails::co_http_query(std::move(url), std::move(request));
    }

    void async_http_query(const Url& url, ClientInterface::AsyncCallback callback)
    {
      async_http_query(url, make_request(HttpVerb::get, url), callback);
    }

    void async_http_query(const Url& url, Client::Request request, ClientInterface::AsyncCallback callback)
    {
      this->co_spawn([url, request = std::move(request), callback]() mutable -> boost::asio::awaitable<void>
      {
        boost::beast::error_code ec;
        Client::Response         response;

        try { response = co_await Crails::co_http_query(std::move(url), std::move(request)); }
        catch (const boost::system::system_error& error) { ec = error.code(); }
        callback(response, ec);
      });
    }

    Client::Response http_query(const Url& url)
    {
      return Crails::http_query(url, make_request(HttpVerb::get, url));
    }

    Client::Response http_query(const Url& url, Client::Request request)
    {
      return Crails::http_query(url, std::move(request));
    }
  };
}

#endif
