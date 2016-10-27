
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
    AS $$ SELECT http(('HEAD', $1, NULL, NULL, NULL)::http_request) $$
    LANGUAGE 'sql';
