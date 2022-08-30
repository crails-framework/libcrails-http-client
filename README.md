# libcrails-http-client

An HTTP client for your Crails server, based on Boost.Beast.

## Usage

Here's an example of how you could use the HTTP client to make a route in your Crails application
work as a proxy:

```
#include <crails/router.hpp>
#include <crails/client.hpp>

void Crails::Router::initialize()
{
  match("GET", "/proxy", [](Crails::Context& context, std::function<void()> callback)
  {
    auto shared_context = context.shared_from_this(); // Keep this around as the query will be asynchronous
    auto http_client = std::make_shared<Crails::Ssl::Client>("duckduckgo.com", 443); // parameters are hostname and port
    Crails::HttpRequest request{Crails::HttpVerb::get, "/", 11};

    request.set(Crails::HttpHeader::location, "duckduckgo.com");
    http_client.async_query(request, [shared_context, http_client, callback](
      const Crails::HttpResponse& response,
      boost::beast::error_code)
    {
      shared_context->response.get_raw_response() = response;
      shared_context->response.send();
      callback();
    });
  });
}
```

Note that we are using `Crails::Ssl::Client` to perform the query. If you want to send request without using
ssl, you should use `Crails::Client` instead. The APIs are identical.

The following are just alias to Beast types and enums. Check the corresponding documentation from boost to learn more about those:

- `Crails::HttpResponse` maps to `boost::beast::http::request`
- `Crails::HttpRequest` maps to `boost::beast::http::response`
- `Crails::HttpStatus` maps to `boost::beast::http::status`
- `Crails::HttpHeader` maps to `boost::beast::http::field`
- `Crails::HttpVerb` maps to `boost::beast::http::verb`

### Synchronous queries

In some cases, you may find it more convenient to use a non-asynchronous API.

```
#include <crails/client.hpp>

int main(int argc, char** argv)
{
  Crails::Ssl::Client client("duckduckgo.com", 443);
  Crails::HttpRequest request{Crails::HttpVerb::get, "/", 11};
  Crails::HttpResponse response = client.query(request);

  return response.result() == 200 ? 0 : -1;
}
```
