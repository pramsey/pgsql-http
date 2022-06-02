CREATE OR REPLACE FUNCTION http_list_curlopt ()
    RETURNS TABLE(curlopt text, value text)
    AS 'MODULE_PATHNAME', 'http_list_curlopt'
    LANGUAGE 'c';

CREATE OR REPLACE FUNCTION urlencode(string BYTEA)
    RETURNS TEXT
    AS 'MODULE_PATHNAME'
    LANGUAGE 'c'
    IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION urlencode(data JSONB)
    RETURNS TEXT
    AS 'MODULE_PATHNAME'
    LANGUAGE 'c'
    IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION http_get(uri VARCHAR, data JSONB)
    RETURNS http_response
    AS $$ SELECT @extschema@.http(('GET', $1 || '?' || @extschema@.urlencode($2), NULL, NULL, NULL)::http_request) $$
    LANGUAGE 'sql';

CREATE OR REPLACE FUNCTION http_post(uri VARCHAR, data JSONB)
    RETURNS http_response
    AS $$ SELECT @extschema@.http(('POST', $1, NULL, 'application/x-www-form-urlencoded', @extschema@.urlencode($2))::http_request) $$
    LANGUAGE 'sql';
