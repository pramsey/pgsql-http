CREATE OR REPLACE FUNCTION http_list_curlopt ()
    RETURNS TABLE(curlopt text, value text)
    AS 'MODULE_PATHNAME', 'http_list_curlopt'
    LANGUAGE 'c';

CREATE OR REPLACE FUNCTION urlencode(string BYTEA)
    RETURNS TEXT
    AS 'MODULE_PATHNAME'
    LANGUAGE 'c'
    IMMUTABLE STRICT;
