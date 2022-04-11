
CREATE OR REPLACE FUNCTION http_delete(uri VARCHAR, content VARCHAR, content_type VARCHAR)
    RETURNS http_response
    AS $$ SELECT @extschema@.http(('DELETE', $1, NULL, $3, $2)::@extschema@.http_request) $$
    LANGUAGE 'sql';
