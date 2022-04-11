
ALTER DOMAIN http_method DROP CONSTRAINT http_method_check;
ALTER DOMAIN http_method ADD CHECK (
    VALUE ILIKE 'get' OR
    VALUE ILIKE 'post' OR
    VALUE ILIKE 'put' OR
    VALUE ILIKE 'delete' OR
    VALUE ILIKE 'head'
);

CREATE OR REPLACE FUNCTION http_head(uri VARCHAR)
    RETURNS http_response
    AS $$ SELECT @extschema@.http(('HEAD', $1, NULL, NULL, NULL)::http_request) $$
    LANGUAGE 'sql';

CREATE OR REPLACE FUNCTION http_set_curlopt (curlopt VARCHAR, value VARCHAR) 
    RETURNS boolean
    AS 'MODULE_PATHNAME', 'http_set_curlopt'
    LANGUAGE 'c';

CREATE OR REPLACE FUNCTION http_reset_curlopt () 
    RETURNS boolean
    AS 'MODULE_PATHNAME', 'http_reset_curlopt'
    LANGUAGE 'c';
