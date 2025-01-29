CREATE EXTENSION http;
SET http.server_host = 'http://localhost:9080';
set http.timeout_msec = 10000;
SELECT http_set_curlopt('CURLOPT_TIMEOUT', '10');
-- if local server not up use global one
DO language plpgsql $$
BEGIN
  BEGIN
    PERFORM http_get(current_setting('http.server_host') || '/status/202');
  EXCEPTION WHEN OTHERS THEN
    SET http.server_host = 'http://httpbin.org';
  END;
END;
$$;

-- Status code
SELECT status
FROM http_get(current_setting('http.server_host') || '/status/202');

-- Headers
SELECT lower(field) AS field, value
FROM (
	SELECT (unnest(headers)).*
	FROM http_get(current_setting('http.server_host') || '/response-headers?Abcde=abcde')
) a
WHERE field ILIKE 'Abcde';

-- GET
SELECT status,
content::json->'args'->>'foo' AS args,
content::json->>'method' AS method
FROM http_get(current_setting('http.server_host') || '/anything?foo=bar');

-- GET with data
SELECT status,
content::json->'args'->>'this' AS args,
replace(content::json->>'url',current_setting('http.server_host'),'') AS path,
content::json->>'method' AS method
FROM http_get(current_setting('http.server_host') || '/anything', jsonb_build_object('this', 'that'));

-- GET with data
SELECT status,
content::json->>'args' as args,
(content::json)->>'data' as data,
content::json->>'method' as method
FROM http(('GET', current_setting('http.server_host') || '/anything', NULL, 'application/json', '{"search": "toto"}'));

-- DELETE
SELECT status,
content::json->'args'->>'foo' AS args,
replace(content::json->>'url',current_setting('http.server_host'),'') AS path,
content::json->>'method' AS method
FROM http_delete(current_setting('http.server_host') || '/anything?foo=bar');

-- DELETE with payload
SELECT status,
content::json->'args'->>'foo' AS args,
replace(content::json->>'url',current_setting('http.server_host'),'') AS path,
content::json->>'method' AS method,
content::json->>'data' AS data
FROM http_delete(current_setting('http.server_host') || '/anything?foo=bar', 'payload', 'text/plain');

-- PUT
SELECT status,
content::json->>'data' AS data,
content::json->'args'->>'foo' AS args,
replace(content::json->>'url', current_setting('http.server_host'),'') AS path,
content::json->>'method' AS method
FROM http_put(current_setting('http.server_host') || '/anything?foo=bar','payload','text/plain');

-- PATCH
SELECT status,
content::json->>'data' AS data,
content::json->'args'->>'foo' AS args,
replace(content::json->>'url', current_setting('http.server_host'),'') AS path,
content::json->>'method' AS method
FROM http_patch(current_setting('http.server_host') || '/anything?foo=bar','{"this":"that"}','application/json');

-- POST
SELECT status,
content::json->>'data' AS data,
content::json->'args'->>'foo' AS args,
replace(content::json->>'url', current_setting('http.server_host'),'') AS path,
content::json->>'method' AS method
FROM http_post(current_setting('http.server_host') || '/anything?foo=bar','payload','text/plain');

-- POST with json data
SELECT status,
content::json->'form'->>'this' AS args,
replace(content::json->>'url', current_setting('http.server_host'),'') AS path,
content::json->>'method' AS method
FROM http_post(current_setting('http.server_host') || '/anything', jsonb_build_object('this', 'that'));

-- POST with data
SELECT status,
content::json->'form'->>'key1' AS key1,
content::json->'form'->>'key2' AS key2,
replace(content::json->>'url', current_setting('http.server_host'),'') AS path,
content::json->>'method' AS method
FROM http_post(current_setting('http.server_host') || '/anything', 'key1=value1&key2=value2','application/x-www-form-urlencoded');

-- HEAD
SELECT lower(field) AS field, value
FROM (
	SELECT (unnest(headers)).*
	FROM http_head(current_setting('http.server_host') || '/response-headers?Abcde=abcde')
) a
WHERE field ILIKE 'Abcde';

-- Follow redirect
SELECT status,
replace((content::json)->>'url', current_setting('http.server_host'),'') AS path
FROM http_get(current_setting('http.server_host') || '/redirect-to?url=get');

-- Request image
WITH
  http AS (
    SELECT * FROM http_get(current_setting('http.server_host') || '/image/png')
  ),
  headers AS (
    SELECT (unnest(headers)).* FROM http
  )
SELECT
  http.content_type,
  length(text_to_bytea(http.content)) AS length_binary
FROM http, headers
WHERE field ilike 'Content-Type';

-- Alter options and and reset them and throw errors
SELECT http_set_curlopt('CURLOPT_PROXY', '127.0.0.1');
-- Error because proxy is not there
DO $$
BEGIN
    SELECT status FROM http_get(current_setting('http.server_host') || '/status/555');
EXCEPTION
    WHEN OTHERS THEN
        RAISE WARNING 'Failed to connect';
END;
$$;
-- Still an error
DO $$
BEGIN
    SELECT status FROM http_get(current_setting('http.server_host') || '/status/555');
EXCEPTION
    WHEN OTHERS THEN
        RAISE WARNING 'Failed to connect';
END;
$$;
-- Reset options
SELECT http_reset_curlopt();
-- Now it should work
SELECT status FROM http_get(current_setting('http.server_host') || '/status/555');

-- Alter the default timeout and then run a query that is longer than
-- the default (5s), but shorter than the new timeout
SELECT http_set_curlopt('CURLOPT_TIMEOUT_MS', '10000');
SELECT status FROM http_get(current_setting('http.server_host') || '/delay/7');

-- Test new GUC feature
SET http.CURLOPT_TIMEOUT_MS = '10';
-- should fail
-- Still an error
DO $$
BEGIN
    SELECT status FROM http_get(current_setting('http.server_host') || '/delay/7');
EXCEPTION
    WHEN OTHERS THEN
        RAISE WARNING 'Failed to connect';
END;
$$;

SET http.CURLOPT_TIMEOUT_MS = '10000';
--should pass
SELECT status FROM http_get(current_setting('http.server_host') || '/delay/7');

-- SET to bogus file
SET http.CURLOPT_CAINFO = '/path/to/somebundle.crt';

-- should fail
DO $$
BEGIN
   SELECT status FROM http_get('https://postgis.net');
EXCEPTION
    WHEN OTHERS THEN
        RAISE WARNING 'Invalid cert file';
END;
$$;

-- set to ignore cert
SET http.CURLOPT_SSL_VERIFYPEER = '0';

-- should pass
SELECT status FROM http_get('https://postgis.net');

SHOW http.CURLOPT_CAINFO;

-- reset it
RESET http.CURLOPT_CAINFO;

SELECT status FROM http_get(current_setting('http.server_host') || '/delay/7');

-- Check that statement interruption works
SET statement_timeout = 200;
CREATE TEMPORARY TABLE timer AS
  SELECT now() AS start;
SELECT *
  FROM http_get(current_setting('http.server_host') || '/delay/7');
SELECT round(extract(epoch FROM now() - start) * 10) AS m
  FROM timer;
DROP TABLE timer;
