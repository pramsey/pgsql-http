# PostgreSQL HTTP Client

[![CI](https://github.com/pramsey/pgsql-http/workflows/CI/badge.svg)](https://github.com/pramsey/pgsql-http/actions)

## Motivation

Wouldn't it be nice to be able to write a trigger that called a web service? Either to get back a result, or to poke that service into refreshing itself against the new state of the database?

This extension is for that.

## Examples

URL encode a string.

```sql
SELECT urlencode('my special string''s & things?');
```
```
              urlencode
-------------------------------------
 my+special+string%27s+%26+things%3F
(1 row)
```

URL encode a JSON associative array.

```sql
SELECT urlencode(jsonb_build_object('name','Colin & James','rate','50%'));
```
```
              urlencode
-------------------------------------
 name=Colin+%26+James&rate=50%25
(1 row)
```

Run a GET request and see the content.

```sql
SELECT content
  FROM http_get('http://httpbun.com/ip');
```
```
           content
-----------------------------
 {"origin":"24.69.186.43"}
(1 row)
```

Run a GET request with an Authorization header.

```sql
SELECT content::json->'headers'->>'Authorization'
  FROM http((
          'GET',
           'http://httpbun.com/headers',
           http_headers('Authorization','Bearer eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9'),
           NULL,
           NULL
        )::http_request);
```
```
                   content
----------------------------------------------
 Bearer eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9
(1 row)
```

Read the `status` and `content_type` fields out of a `http_response` object.

```sql
SELECT status, content_type
  FROM http_get('http://httpbun.com/');
```
```
 status |       content_type
--------+--------------------------
    200 | text/html; charset=utf-8
(1 row)
```

Show all the `http_header` in an `http_response` object.

```sql
SELECT (unnest(headers)).*
  FROM http_get('http://httpbun.com/');
```
```
      field       |                      value
------------------+--------------------------------------------------
 Server           | nginx
 Date             | Wed, 26 Jul 2023 19:52:51 GMT
 Content-Type     | text/html
 Content-Length   | 162
 Connection       | close
 Location         | https://httpbun.org
 server           | nginx
 date             | Wed, 26 Jul 2023 19:52:51 GMT
 content-type     | text/html
 x-powered-by     | httpbun/3c0dc05883dd9212ac38b04705037d50b02f2596
 content-encoding | gzip
```

Use the PUT command to send a simple text document to a server.

```sql
SELECT status, content_type, content::json->>'data' AS data
  FROM http_put('http://httpbun.com/put', 'some text', 'text/plain');
```
```
 status |   content_type   |   data
--------+------------------+-----------
    200 | application/json | some text
```

Use the PATCH command to send a simple JSON document to a server.

```sql
SELECT status, content_type, content::json->>'data' AS data
  FROM http_patch('http://httpbun.com/patch', '{"this":"that"}', 'application/json');
```
```
 status |   content_type   |      data
--------+------------------+------------------
    200 | application/json | '{"this":"that"}'
```

Use the DELETE command to request resource deletion.

```sql
SELECT status, content_type, content::json->>'url' AS url
  FROM http_delete('http://httpbun.com/delete');
```
```
 status |   content_type   |            url
--------+------------------+---------------------------
    200 | application/json | http://httpbun.com/delete
```

As a shortcut to send data to a GET request, pass a JSONB data argument.

```sql
SELECT status, content::json->'args' AS args
  FROM http_get('http://httpbun.com/get',
                jsonb_build_object('myvar','myval','foo','bar'));
```

To POST to a URL using a data payload instead of parameters embedded in the URL, encode the data in a JSONB as a data payload.

```sql
SELECT status, content::json->'form' AS form
  FROM http_post('http://httpbun.com/post',
                 jsonb_build_object('myvar','myval','foo','bar'));
```

To access binary content, you must coerce the content from the default `varchar` representation to a `bytea` representation using the `text_to_bytea()` function, or the `textsend()` function. Using the default `varchar::bytea` cast will **not work**, as the cast will stop the first time it hits a zero-valued byte (common in binary data).

```sql
WITH
  http AS (
    SELECT * FROM http_get('https://httpbingo.org/image/png')
  ),
  headers AS (
    SELECT (unnest(headers)).* FROM http
  )
SELECT
  http.content_type,
  length(text_to_bytea(http.content)) AS length_binary
FROM http, headers
WHERE field ilike 'Content-Type';
```
```
 content_type | length_binary
--------------+---------------
 image/png    |          8090
```
Similarly, when using POST to send `bytea` binary content to a service, use the `bytea_to_text` function to prepare the content.

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
 status |        location
--------+------------------------
    301 | http://www.google.com/
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
* `http_headers(field VARCHAR, value VARCHAR, ...)` returns `http_header[]`
* `http(request http_request)` returns `http_response`
* `http_get(uri VARCHAR)` returns `http_response`
* `http_get(uri VARCHAR, data JSONB)` returns `http_response`
* `http_post(uri VARCHAR, content VARCHAR, content_type VARCHAR)` returns `http_response`
* `http_post(uri VARCHAR, data JSONB)` returns `http_response`
* `http_put(uri VARCHAR, content VARCHAR, content_type VARCHAR)` returns `http_response`
* `http_patch(uri VARCHAR, content VARCHAR, content_type VARCHAR)` returns `http_response`
* `http_delete(uri VARCHAR, content VARCHAR, content_type VARCHAR))` returns `http_response`
* `http_head(uri VARCHAR)` returns `http_response`
* `http_set_curlopt(curlopt VARCHAR, value varchar)` returns `boolean`
* `http_reset_curlopt()` returns `boolean`
* `http_list_curlopt()` returns `setof(curlopt text, value text)`
* `urlencode(string VARCHAR)` returns `text`
* `urlencode(data JSONB)` returns `text`

## CURL Options

Select [CURL options](https://curl.se/libcurl/c/curl_easy_setopt.html) are available to set using the SQL `SET` command and the appropriate option name.

* [CURLOPT_CAINFO](https://curl.se/libcurl/c/CURLOPT_CAINFO.html)
* [CURLOPT_CONNECTTIMEOUT](https://curl.se/libcurl/c/CURLOPT_CONNECTTIMEOUT.html)
* [CURLOPT_CONNECTTIMEOUT_MS](https://curl.se/libcurl/c/CURLOPT_CONNECTTIMEOUT_MS.html)
* [CURLOPT_DNS_SERVERS](https://curl.se/libcurl/c/CURLOPT_DNS_SERVERS.html)
* [CURLOPT_PRE_PROXY](https://curl.se/libcurl/c/CURLOPT_PRE_PROXY.html)
* [CURLOPT_PROXY](https://curl.se/libcurl/c/CURLOPT_PROXY.html)
* [CURLOPT_PROXYPASSWORD](https://curl.se/libcurl/c/CURLOPT_PROXYPASSWORD.html)
* [CURLOPT_PROXYPORT](https://curl.se/libcurl/c/CURLOPT_PROXYPORT.html)
* [CURLOPT_PROXYUSERPWD](https://curl.se/libcurl/c/CURLOPT_PROXYUSERPWD.html)
* [CURLOPT_PROXYUSERNAME](https://curl.se/libcurl/c/CURLOPT_PROXYUSERNAME.html)
* [CURLOPT_PROXY_TLSAUTH_USERNAME](https://curl.se/libcurl/c/CURLOPT_PROXY_TLSAUTH_USERNAME.html)
* [CURLOPT_PROXY_TLSAUTH_PASSWORD](https://curl.se/libcurl/c/CURLOPT_PROXY_TLSAUTH_PASSWORD.html)
* [CURLOPT_PROXY_TLSAUTH_TYPE](https://curl.se/libcurl/c/CURLOPT_PROXY_TLSAUTH_TYPE.html)
* [CURLOPT_SSL_VERIFYHOST](https://curl.se/libcurl/c/CURLOPT_SSL_VERIFYHOST.html)
* [CURLOPT_SSL_VERIFYPEER](https://curl.se/libcurl/c/CURLOPT_SSL_VERIFYPEER.html)
* [CURLOPT_SSLCERT](https://curl.se/libcurl/c/CURLOPT_SSLCERT.html)
* [CURLOPT_SSLCERT_BLOB](https://curl.se/libcurl/c/CURLOPT_SSLCERT_BLOB.html)
* [CURLOPT_SSLCERTTYPE](https://curl.se/libcurl/c/CURLOPT_SSLCERTTYPE.html)
* [CURLOPT_SSLKEY](https://curl.se/libcurl/c/CURLOPT_SSLKEY.html)
* [CURLOPT_SSLKEY_BLOB](https://curl.se/libcurl/c/CURLOPT_SSLKEY_BLOB.html)
* [CURLOPT_TCP_KEEPALIVE](https://curl.se/libcurl/c/CURLOPT_TCP_KEEPALIVE.html)
* [CURLOPT_TCP_KEEPIDLE](https://curl.se/libcurl/c/CURLOPT_TCP_KEEPIDLE.html)
* [CURLOPT_TIMEOUT](https://curl.se/libcurl/c/CURLOPT_TIMEOUT.html)
* [CURLOPT_TIMEOUT_MS](https://curl.se/libcurl/c/CURLOPT_TIMEOUT_MS.html)
* [CURLOPT_TLSAUTH_USERNAME](https://curl.se/libcurl/c/CURLOPT_TLSAUTH_USERNAME.html)
* [CURLOPT_TLSAUTH_PASSWORD](https://curl.se/libcurl/c/CURLOPT_TLSAUTH_PASSWORD.html)
* [CURLOPT_TLSAUTH_TYPE](https://curl.se/libcurl/c/CURLOPT_TLSAUTH_TYPE.html)
* [CURLOPT_USERAGENT](https://curl.se/libcurl/c/CURLOPT_USERAGENT.html)
* [CURLOPT_USERPWD](https://curl.se/libcurl/c/CURLOPT_USERPWD.html)

For example,

```sql
-- Set the curlopt_proxyport option
SET http.curlopt_proxyport = '12345';

-- View the curlopt_proxyport option
SHOW http.curlopt_proxyport;

-- View *all* currently set options
SELECT * FROM http_list_curlopt();
```

Will set the proxy port option for the lifetime of the database connection. You can reset all CURL options to their defaults using the `http_reset_curlopt()` function.

You can permanently set the CURL options for a database or role, using the `ALTER DATABASE` and `ALTER ROLE` commands.

```sql
-- Applies to all roles in the database
ALTER DATABASE mydb SET http.curlopt_tlsauth_password = 'secret';

-- Applies to just one role in the database
ALTER ROLE myapp IN mydb SET http.curlopt_tlsauth_password = 'secret';
```

## User Agents

Using this extension as a background automated process without supervision (e.g as a trigger) may have unintended consequences for other servers. It is considered a best practice to share contact information with your requests, so that administrators can reach you in case your HTTP calls get out of control.

Certain API policies (e.g. [Wikimedia User-Agent policy](https://foundation.wikimedia.org/wiki/Policy:Wikimedia_Foundation_User-Agent_Policy)) may even require sharing specific contact information with each request. Others may disallow (via `robots.txt`) certain agents they don't recognize.

For such cases you can set the `CURLOPT_USERAGENT` option

```sql
SET http.curlopt_useragent = 'PgBot/2.1 (+http://pgbot.com/bot.html) Contact abuse@pgbot.com';

SELECT status, content::json->'headers'->>'User-Agent'
  FROM http_get('http://httpbun.com/headers');
```
```
 status |                         user_agent
--------+-----------------------------------------------------------
    200 | PgBot/2.1 (+http://pgbot.com/bot.html) Contact abuse@pgbot.com
```

## Keep-Alive & Timeouts

By default each request uses a fresh connection and assures that the connection is closed when the request is done.  This behavior reduces the chance of consuming system resources (sockets) as the extension runs over extended periods of time.

High-performance applications may wish to enable keep-alive and connection persistence to reduce latency and enhance throughput.  The following GUC variable changes the behavior of the http extension to maintain connections as long as possible:

```sql
SET http.curlopt_tcp_keepalive = 1;
```

By default a 5 second timeout is set for the completion of a request.  If a different timeout is desired the following GUC variable can be used to set it in milliseconds:

```sql
SET http.curlopt_timeout_msec = 200;
```

## Installation

### Debian / Ubuntu apt.postgresql.org
Replace 17 with your version of PostgreSQL
```
apt install postgresql-17-http
```

### Compile from Source

#### General Unix

If you have PostgreSQL devel packages and CURL devel packages installed, you should have `pg_config` and `curl-config` on your path, so you should be able to just run `make` (or `gmake`), then `make install`, then in your database `CREATE EXTENSION http`.

If you already installed a previous version and you just want to upgrade, then `ALTER EXTENSION http UPDATE`.

#### Debian / Ubuntu / APT

Refer to https://wiki.postgresql.org/wiki/Apt for pulling packages from apt.postgresql.org repository.

```bash
sudo apt install \
  postgresql-server-dev-14 \
  libcurl4-openssl-dev \
  make \
  g++

make
sudo make install
```

If there several PostgreSQL installations available, you might need to edit the Makefile before running `make`:

```
#PG_CONFIG = pg_config
PG_CONFIG = /usr/lib/postgresql/14/bin/pg_config
```

### Windows

There is a build available at [postgresonline](http://www.postgresonline.com/journal/archives/371-http-extension.html), not maintained by me.

### Testing

The integration tests are run with `make install && make installcheck` and expect to find a running instance of [httpbin](http://httpbin.org) at port 9080. The easiest way to get that is:

```
docker run -p 9080:80 kennethreitz/httpbin
```

## Why This is a Bad Idea

- "What happens if the web page takes a long time to return?" Your SQL call will just wait there until it does. Make sure your web service fails fast. Or (dangerous in a different way) run your query within [pg_background](https://github.com/vibhorkum/pg_background) or on a schedule with [pg_cron](https://github.com/citusdata/pg_cron).
- "What if the web page returns junk?" Your SQL call will have to test for junk before doing anything with the payload.
- "What if the web page never returns?" Set a short timeout, or send a cancel to the request, or just wait forever.
- "What if a user queries a page they shouldn't?" Restrict function access, or just don't install a footgun like this extension where users can access it.

## To Do

- The [background worker](https://www.postgresql.org/docs/current/bgworker.html) support could be used to set up an HTTP request queue, so that pgsql-http can register a request and callback and then return immediately.

