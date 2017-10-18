CREATE EXTENSION http;

SELECT http_set_curlopt('CURLOPT_TIMEOUT', '60');
SELECT http_set_curlopt('CURLOPT_TCP_KEEPALIVE', '60');


# Status code
SELECT status 
FROM http_get('http://httpbin.org/status/202');

# Headers
SELECT * 
FROM (
	SELECT (unnest(headers)).* 
	FROM http_get('http://httpbin.org/response-headers?Abcde=abcde')
) a 
WHERE field = 'Abcde';

-- GET
SELECT status, 
content::json->'args' AS args, 
content::json->'url' AS url, 
content::json->'method' AS method 
FROM http_get('http://httpbin.org/anything?foo=bar');

-- DELETE
SELECT status, 
content::json->'args' AS args, 
content::json->'url' AS url, 
content::json->'method' AS method 
FROM http_delete('http://httpbin.org/anything?foo=bar');

-- PUT
SELECT status, 
content::json->'data' AS data, 
content::json->'args' AS args, 
content::json->'url' AS url, 
content::json->'method' AS method 
FROM http_put('http://httpbin.org/anything?foo=bar','payload','text/plain');

-- POST
SELECT status, 
content::json->'data' AS data, 
content::json->'args' AS args, 
content::json->'url' AS url, 
content::json->'method' AS method 
FROM http_post('http://httpbin.org/anything?foo=bar','payload','text/plain');

-- HEAD
SELECT * 
FROM (
	SELECT (unnest(headers)).* 
	FROM http_head('http://httpbin.org/response-headers?Abcde=abcde')
) a
WHERE field = 'Abcde';

-- Follow redirect
SELECT status, 
content::json->'args' AS args, 
content::json->'url' AS url 
FROM http_get('http://httpbin.org/redirect-to?url=http%3A%2F%2Fhttpbin%2Eorg%2Fget%3Ffoo%3Dbar');

