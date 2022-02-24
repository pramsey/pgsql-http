# PostgreSQL HTTP Client

[![CI](https://github.com/pramsey/pgsql-http/workflows/CI/badge.svg?branch=master)](https://github.com/pramsey/pgsql-http/actions?query=branch%3Amaster)

## Motivation

Wouldn't it be nice to be able to write a trigger that called a web service? Either to get back a result, or to poke that service into refreshing itself against the new state of the database?

This extension is for that.

## Examples

```sql
SELECT urlencode('my special string''s & things?');
```
```
              urlencode
-------------------------------------
 my+special+string%27s+%26+things%3F
(1 row)
```
```sql
SELECT urlencode(jsonb_build_object('name','Colin & James','rate','50%'));
```
```
              urlencode
-------------------------------------
 name=Colin+%26+James&rate=50%25
(1 row)
```
```sql
SELECT content FROM http_get('http://httpbin.org/ip');
```
```
           content
-----------------------------
 {"origin":"24.69.186.43"}                          +
(1 row)
```
```sql
SELECT content::json->'headers'->>'Authorization' FROM http((
          'GET',
           'http://httpbin.org/headers',
           ARRAY[http_header('Authorization','Bearer eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9')],
           NULL,
           NULL
        )::http_request)
```
```
                   content
----------------------------------------------
 Bearer eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9
(1 row)
```
```sql
SELECT status, content_type FROM http_get('http://httpbin.org/');
```
```
 status |       content_type
--------+--------------------------
    200 | text/html; charset=utf-8
(1 row)
```
```sql
SELECT (unnest(headers)).* FROM http_get('http://httpbin.org/');
```
```
              field               |             value
----------------------------------+-------------------------------
 Connection                       | close
 Server                           | meinheld/0.6.1
 Date                             | Tue, 09 Jan 2018 18:40:30 GMT
 Content-Type                     | text/html; charset=utf-8
 Content-Length                   | 13011
 Access-Control-Allow-Origin      | *
 Access-Control-Allow-Credentials | true
 X-Powered-By                     | Flask
 X-Processed-Time                 | 0.0208520889282
 Via                              | 1.1 vegur
```
```sql
SELECT status, content_type, content::json->>'data' AS data
  FROM http_put('http://httpbin.org/put', 'some text', 'text/plain');
```
```
 status |   content_type   |   data
--------+------------------+-----------
    200 | application/json | some text
```
```sql
SELECT status, content_type, content::json->>'data' AS data
  FROM http_patch('http://httpbin.org/patch', '{"this":"that"}', 'application/json');
```
```
 status |   content_type   |      data
--------+------------------+------------------
    200 | application/json | '{"this":"that"}'
```
```sql
SELECT status, content_type, content::json->>'url' AS url
  FROM http_delete('http://httpbin.org/delete');
```
```
 status |   content_type   |            url
--------+------------------+---------------------------
    200 | application/json | http://httpbin.org/delete
```

As a shortcut to send data to a GET request, pass a JSONB data argument.

```sql
SELECT status, content::json->'args' AS args
  FROM http_get('http://httpbin.org/get',
                jsonb_build_object('myvar','myval','foo','bar'));
```

To POST to a URL using a data payload instead of parameters embedded in the URL, encode the data in a JSONB as a data payload.

```sql
SELECT status, content::json->'form' AS form
  FROM http_post('http://httpbin.org/post',
                 jsonb_build_object('myvar','myval','foo','bar'));
```

To access binary content, you must coerce the content from the default `varchar` representation to a `bytea` representation using the `textsend` function. Using the default `varchar::bytea` cast will not work, as the cast will stop the first time it hits a zero-valued byte (common in binary data).

```sql
WITH
  http AS (
    SELECT * FROM http_get('http://httpbin.org/image/png')
  ),
  headers AS (
    SELECT (unnest(headers)).* FROM http
  )
SELECT
  http.content_type,
  length(textsend(http.content)) AS length_binary,
  headers.value AS length_headers
FROM http, headers
WHERE field = 'Content-Length';
```
```
 content_type | length_binary | length_headers
--------------+---------------+----------------
 image/png    |          8090 | 8090
```
To access only the headers you can do a HEAD-Request. This will not follow redirections.

```sql
SELECT
    http.status,
    headers.value AS location
FROM
    http_head('http://google.com') AS http
    LEFT OUTER JOIN LATERAL (SELECT value
        FROM unnest(http.headers)
        WHERE field = 'Location') AS headers
        ON true;
```
```
 status |                         location
--------+-----------------------------------------------------------
    302 | http://www.google.ch/?gfe_rd=cr&ei=ACESWLy_KuvI8zeghL64Ag
```

## Concepts

Every HTTP call is a made up of an `http_request` and an `http_response`.

         Composite type "public.http_request"
        Column    |       Type        | Modifiers
    --------------+-------------------+-----------
     method       | http_method       |
     uri          | character varying |
     headers      | http_header[]     |
     content_type | character varying |
     content      | character varying |

        Composite type "public.http_response"
        Column    |       Type        | Modifiers
    --------------+-------------------+-----------
     status       | integer           |
     content_type | character varying |
     headers      | http_header[]     |
     content      | character varying |

The utility functions, `http_get()`, `http_post()`, `http_put()`, `http_delete()` and `http_head()` are just wrappers around a master function, `http(http_request)` that returns `http_response`.

The `headers` field for requests and response is a PostgreSQL array of type `http_header` which is just a simple tuple.

      Composite type "public.http_header"
     Column |       Type        | Modifiers
    --------+-------------------+-----------
     field  | character varying |
     value  | character varying |

As seen in the examples, you can unspool the array of `http_header` tuples into a result set using the PostgreSQL `unnest()` function on the array. From there you select out the particular header you are interested in.

## Functions

* `http_header(field VARCHAR, value VARCHAR)` returns `http_header`
* `http(request http_request)` returns `http_response`
* `http_get(uri VARCHAR)` returns `http_response`
* `http_post(uri VARCHAR, content VARCHAR, content_type VARCHAR)` returns `http_response`
* `http_put(uri VARCHAR, content VARCHAR, content_type VARCHAR)` returns `http_response`
* `http_patch(uri VARCHAR, content VARCHAR, content_type VARCHAR)` returns `http_response`
* `http_delete(uri VARCHAR)` returns `http_response`
* `http_head(uri VARCHAR)` returns `http_response`
* `http_set_curlopt(curlopt VARCHAR, value varchar)` returns `boolean`
* `http_reset_curlopt()` returns `boolean`
* `http_list_curlopt()` returns `setof(curlopt text, value text)`
* `urlencode(string VARCHAR)` returns `text`

## CURL Options

Select [CURL options](https://curl.haxx.se/libcurl/c/curl_easy_setopt.html) are available to set using the `http_set_curlopt(curlopt VARCHAR, value varchar)` function.

* [CURLOPT_DNS_SERVERS](https://curl.haxx.se/libcurl/c/CURLOPT_DNS_SERVERS.html)
* [CURLOPT_PROXY](https://curl.haxx.se/libcurl/c/CURLOPT_PROXY.html)
* [CURLOPT_PRE_PROXY](https://curl.haxx.se/libcurl/c/CURLOPT_PRE_PROXY.html)
* [CURLOPT_PROXYPORT](https://curl.haxx.se/libcurl/c/CURLOPT_PROXYPORT.html)
* [CURLOPT_PROXYUSERPWD](https://curl.haxx.se/libcurl/c/CURLOPT_PROXYUSERPWD.html)
* [CURLOPT_PROXYUSERNAME](https://curl.haxx.se/libcurl/c/CURLOPT_PROXYUSERNAME.html)
* [CURLOPT_PROXYPASSWORD](https://curl.haxx.se/libcurl/c/CURLOPT_PROXYPASSWORD.html)
* [CURLOPT_PROXY_TLSAUTH_USERNAME](https://curl.haxx.se/libcurl/c/CURLOPT_PROXY_TLSAUTH_USERNAME.html)
* [CURLOPT_PROXY_TLSAUTH_PASSWORD](https://curl.haxx.se/libcurl/c/CURLOPT_PROXY_TLSAUTH_PASSWORD.html)
* [CURLOPT_PROXY_TLSAUTH_TYPE](https://curl.haxx.se/libcurl/c/CURLOPT_PROXY_TLSAUTH_TYPE.html)
* [CURLOPT_TLSAUTH_USERNAME](https://curl.haxx.se/libcurl/c/CURLOPT_TLSAUTH_USERNAME.html)
* [CURLOPT_TLSAUTH_PASSWORD](https://curl.haxx.se/libcurl/c/CURLOPT_TLSAUTH_PASSWORD.html)
* [CURLOPT_TLSAUTH_TYPE](https://curl.haxx.se/libcurl/c/CURLOPT_TLSAUTH_TYPE.html)
* [CURLOPT_SSL_VERIFYHOST](https://curl.haxx.se/libcurl/c/CURLOPT_SSL_VERIFYHOST.html)
* [CURLOPT_SSL_VERIFYPEER](https://curl.haxx.se/libcurl/c/CURLOPT_SSL_VERIFYPEER.html)
* [CURLOPT_SSLCERT](https://curl.haxx.se/libcurl/c/CURLOPT_SSLCERT.html)
* [CURLOPT_SSLKEY](https://curl.haxx.se/libcurl/c/CURLOPT_SSLKEY.html)
* [CURLOPT_SSLCERTTYPE](https://curl.haxx.se/libcurl/c/CURLOPT_SSLCERTTYPE.html)
* [CURLOPT_CAINFO](https://curl.haxx.se/libcurl/c/CURLOPT_CAINFO.html)
* [CURLOPT_TIMEOUT](https://curl.haxx.se/libcurl/c/CURLOPT_TIMEOUT.html)
* [CURLOPT_TIMEOUT_MS](https://curl.haxx.se/libcurl/c/CURLOPT_TIMEOUT_MS.html)
* [CURLOPT_TCP_KEEPALIVE](https://curl.haxx.se/libcurl/c/CURLOPT_TCP_KEEPALIVE.html)
* [CURLOPT_TCP_KEEPIDLE](https://curl.haxx.se/libcurl/c/CURLOPT_TCP_KEEPIDLE.html)
* [CURLOPT_CONNECTTIMEOUT](https://curl.haxx.se/libcurl/c/CURLOPT_CONNECTTIMEOUT.html)
* [CURLOPT_USERAGENT](https://curl.haxx.se/libcurl/c/CURLOPT_USERAGENT.html)

For example,

```sql
-- Set the PROXYPORT option
SELECT http_set_curlopt('CURLOPT_PROXYPORT', '12345');

-- List all currently set options
SELECT * FROM http_list_curlopt();
```

Will set the proxy port option for the lifetime of the database connection. You can reset all CURL options to their defaults using the `http_reset_curlopt()` function.

Using this extension as a background automated process without supervision (e.g as a trigger) may have unintended consequences for other servers.
It is considered a best practice to share contact information with your requests,
so that administrators can reach you in case your HTTP calls get out of control.

Certain API policies (e.g. [Wikimedia User-Agent policy](https://meta.wikimedia.org/wiki/User-Agent_policy)) may even require sharing specific contact information
with each request. Others may disallow (via `robots.txt`) certain agents they don't recognize.

For such cases you can set the `CURLOPT_USERAGENT` option

```sql
SELECT http_set_curlopt('CURLOPT_USERAGENT',
                        'Examplebot/2.1 (+http://www.example.com/bot.html) Contact abuse@example.com');

SELECT status, content::json ->> 'user-agent' FROM http_get('http://httpbin.org/user-agent');
```
```
 status |                         user_agent
--------+-----------------------------------------------------------
    200 | Examplebot/2.1 (+http://www.example.com/bot.html) Contact abuse@example.com
```

## Keep-Alive & Timeouts

*The `http_reset_curlopt()` approach described above is recommended. The global variables below will be deprecated and removed over time.*

By default each request uses a fresh connection and assures that the connection is closed when the request is done.  This behavior reduces the chance of consuming system resources (sockets) as the extension runs over extended periods of time.

High-performance applications may wish to enable keep-alive and connection persistence to reduce latency and enhance throughput.  The following GUC variable changes the behavior of the http extension to maintain connections as long as possible:

    http.keepalive = 'on'

By default a 5 second timeout is set for the completion of a request.  If a different timeout is desired the following GUC variable can be used to set it in milliseconds:

    http.timeout_msec = 200

## Installation

### UNIX

If you have PostgreSQL (>= 9.3) devel packages and CURL devel packages installed (>= 0.7.20), you should have `pg_config` and `curl-config` on your path, so you should be able to just run `make` (or `gmake`), then `make install`, then in your database `CREATE EXTENSION http`.

If you already installed a previous version and you just want to upgrade, then `ALTER EXTENSION http UPDATE`.

### Windows

There is a build available at [postgresonline](http://www.postgresonline.com/journal/archives/371-http-extension.html), not maintained by me.

## Why This is a Bad Idea

- "What happens if the web page takes a long time to return?" Your SQL call will just wait there until it does. Make sure your web service fails fast. Or (dangerous in a different way) run your query within [pg_background](https://github.com/vibhorkum/pg_background).
- "What if the web page returns junk?" Your SQL call will have to test for junk before doing anything with the payload.
- "What if the web page never returns?" Set a short timeout, or send a cancel to the request, or just wait forever.
- "What if a user queries a page they shouldn't?" Restrict function access, or just don't install a footgun like this extension where users can access it.

## To Do

- The new http://www.postgresql.org/docs/9.3/static/bgworker.html background worker support could be used to set up an HTTP request queue, so that pgsql-http can register a request and callback and then return immediately.
- Inevitably some web server will return gzip content (Content-Encoding) without being asked for it. Handling that gracefully would be good.

