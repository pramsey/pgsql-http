CREATE OR REPLACE FUNCTION urlencode(data JSONB)
    RETURNS TEXT
    AS 'MODULE_PATHNAME'
    LANGUAGE 'c'
    IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION http_get(uri VARCHAR, data JSONB)
    RETURNS http_response
    AS $$ SELECT http(('GET', $1 || '?' || urlencode($2), NULL, NULL, NULL)::http_request) $$
    LANGUAGE 'sql';

CREATE OR REPLACE FUNCTION http_post(uri VARCHAR, data JSONB)
    RETURNS http_response
    AS $$ SELECT http(('POST', $1, NULL, 'application/x-www-form-urlencoded', urlencode($2))::http_request) $$
    LANGUAGE 'sql';
