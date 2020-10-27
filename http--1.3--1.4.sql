CREATE OR REPLACE FUNCTION http_list_curlopt ()
    RETURNS TABLE(curlopt text, value text)
    AS 'MODULE_PATHNAME', 'http_list_curlopt'
    LANGUAGE 'c';
