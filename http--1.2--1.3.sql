ALTER DOMAIN http_method DROP CONSTRAINT http_method_check;
ALTER DOMAIN http_method ADD CHECK (
    VALUE ILIKE 'get' OR
    VALUE ILIKE 'post' OR
    VALUE ILIKE 'put' OR
    VALUE ILIKE 'delete' OR
    VALUE ILIKE 'patch' OR
    VALUE ILIKE 'head'
);

CREATE OR REPLACE FUNCTION http_patch(uri VARCHAR, content VARCHAR, content_type VARCHAR)
    RETURNS http_response
    AS $$ SELECT @extschema@.http(('PATCH', $1, NULL, $3, $2)::http_request) $$
    LANGUAGE 'sql';
