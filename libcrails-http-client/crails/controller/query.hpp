#ifndef  CRAILS_QUERY_CONTROLLER_HPP
# define CRAILS_QUERY_CONTROLLER_HPP

# include <crails/url.hpp>
# include <crails/controller.hpp>
# include "../client.hpp"

namespace Crails
{
  template<typename SUPER = Crails::Controller>
  class QueryController : public SUPER
  {
    Context& _context;
  public:
    QueryController(Context& context) : SUPER(context), _context(context)
    {
    }

  protected:
    HttpResponse http_query(const Url& url)
    {
      return http_query(url, {HttpVerb::get, '/' + url.target, 11});
    }

    HttpResponse http_query(const Url& url, Crails::HttpRequest request)
    {
      request.set(Crails::HttpHeader::host, url.host);
      if (url.ssl)
        return make_http_query<Ssl::Client>(url, request);
      return make_http_query<Client>(url, request);
    }

    void async_http_query(const Url& url, ClientInterface::AsyncCallback callback)
    {
      async_http_query(url, {HttpVerb::get, '/' + url.target, 11}, callback);
    }

    void async_http_query(const Url& url, Crails::HttpRequest request, ClientInterface::AsyncCallback callback)
    {
      request.set(Crails::HttpHeader::host, url.host);
      if (url.ssl)
        make_async_http_query<Ssl::Client>(url, request, callback);
      else
        make_async_http_query<Client>(url, request, callback);
    }

  private:
    template<typename CLIENT_TYPE>
    HttpResponse make_http_query(const Crails::Url& url, const Crails::HttpRequest& request)
    {
      CLIENT_TYPE client(url.host, url.port);

      client.connect();
      return client.query(request);
    }

    template<typename CLIENT_TYPE>
    void make_async_http_query(const Crails::Url& url, const Crails::HttpRequest& request, ClientInterface::AsyncCallback callback)
    {
      auto client = std::make_shared<CLIENT_TYPE>(url.host, url.port);
      auto self = SUPER::shared_from_this();

      client->connect();
      client->async_query(request, [this, client, self, callback](const HttpResponse& response, boost::beast::error_code ec)
      {
        _context.protect([client, &response, ec, callback]()
        {
          callback(response, ec);
          client->disconnect();
        });
      });
    }
  };
}

#endif
