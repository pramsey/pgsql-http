CREATE EXTENSION http;

set http.timeout_msec = 10000;
SELECT http_set_curlopt('CURLOPT_TIMEOUT', '10');

-- Status code
SELECT status
FROM http_get('https://httpbin.org/status/202');

-- Headers
SELECT lower(field) AS field, value
FROM (
	SELECT (unnest(headers)).*
	FROM http_get('https://httpbin.org/response-headers?Abcde=abcde')
) a
WHERE field ILIKE 'Abcde';

-- GET
SELECT status,
content::json->'args'->>'foo' AS args,
content::json->'url' AS url,
content::json->'method' AS method
FROM http_get('https://httpbin.org/anything?foo=bar');

-- GET with data
SELECT status,
content::json->'args'->>'this' AS args,
content::json->'url' AS url,
content::json->'method' AS method
FROM http_get('https://httpbin.org/anything', jsonb_build_object('this', 'that'));

-- GET with data
SELECT status,
content::json->'args' as args,
content::json->>'data' as data,
content::json->'url' as url,
content::json->'method' as method
from http(('GET', 'https://httpbin.org/anything', NULL, 'application/json', '{"search": "toto"}'));

-- DELETE
SELECT status,
content::json->'args'->>'foo' AS args,
content::json->'url' AS url,
content::json->'method' AS method
FROM http_delete('https://httpbin.org/anything?foo=bar');

-- DELETE with payload
SELECT status,
content::json->'args'->>'foo' AS args,
content::json->'url' AS url,
content::json->'method' AS method,
content::json->'data' AS data
FROM http_delete('https://httpbin.org/anything?foo=bar', 'payload', 'text/plain');

-- PUT
SELECT status,
content::json->'data' AS data,
content::json->'args'->>'foo' AS args,
content::json->'url' AS url,
content::json->'method' AS method
FROM http_put('https://httpbin.org/anything?foo=bar','payload','text/plain');

-- PATCH
SELECT status,
content::json->'data' AS data,
content::json->'args'->>'foo' AS args,
content::json->'url' AS url,
content::json->'method' AS method
FROM http_patch('https://httpbin.org/anything?foo=bar','{"this":"that"}','application/json');

-- POST
SELECT status,
content::json->'data' AS data,
content::json->'args'->>'foo' AS args,
content::json->'url' AS url,
content::json->'method' AS method
FROM http_post('https://httpbin.org/anything?foo=bar','payload','text/plain');

-- POST with json data
SELECT status,
content::json->'form'->>'this' AS args,
content::json->'url' AS url,
content::json->'method' AS method
FROM http_post('https://httpbin.org/anything', jsonb_build_object('this', 'that'));

-- POST with data
SELECT status,
content::json->'form'->>'key1' AS key1,
content::json->'form'->>'key2' AS key2,
content::json->'url' AS url,
content::json->'method' AS method
FROM http_post('https://httpbin.org/anything', 'key1=value1&key2=value2','application/x-www-form-urlencoded');

-- HEAD
SELECT lower(field) AS field, value
FROM (
	SELECT (unnest(headers)).*
	FROM http_head('https://httpbin.org/response-headers?Abcde=abcde')
) a
WHERE field ILIKE 'Abcde';

-- Follow redirect
SELECT status,
content::json->'args' AS args,
content::json->'url' AS url
FROM http_get('https://httpbingo.org/redirect-to?url=https%3A%2F%2Fhttpbingo%2Eorg%2Fget%3Ffoo%3Dbar');

-- Request image
WITH
  http AS (
    SELECT * FROM http_get('https://httpbin.org/image/png')
  ),
  headers AS (
    SELECT (unnest(headers)).* FROM http
  )
SELECT
  http.content_type,
  length(textsend(http.content)) AS length_binary,
  headers.value AS length_headers
FROM http, headers
WHERE field ILIKE 'Content-Length';

-- Alter options and and reset them and throw errors
SELECT http_set_curlopt('CURLOPT_PROXY', '127.0.0.1');
-- Error because proxy is not there
DO $$
BEGIN
    SELECT status FROM http_get('https://httpbin.org/status/555');
EXCEPTION
    WHEN OTHERS THEN
        RAISE WARNING 'Failed to connect';
END;
$$;
-- Still an error
DO $$
BEGIN
    SELECT status FROM http_get('https://httpbin.org/status/555');
EXCEPTION
    WHEN OTHERS THEN
        RAISE WARNING 'Failed to connect';
END;
$$;
-- Reset options
SELECT http_reset_curlopt();
-- Now it should work
SELECT status FROM http_get('https://httpbin.org/status/555');

-- Alter the default timeout and then run a query that is longer than
-- the default (5s), but shorter than the new timeout
SELECT http_set_curlopt('CURLOPT_TIMEOUT_MS', '10000');
SELECT status FROM http_get('http://httpstat.us/200?sleep=7000');
