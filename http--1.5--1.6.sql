
ALTER DOMAIN http_method drop CONSTRAINT http_method_check;

ALTER DOMAIN http_method add CHECK (
    VALUE ILIKE 'get' OR
    VALUE ILIKE 'post' OR
    VALUE ILIKE 'put' OR
    VALUE ILIKE 'delete' OR
    VALUE ILIKE 'patch' OR
    VALUE ILIKE 'head' OR
    VALUE ILIKE 'mkcol'
);

CREATE FUNCTION text_to_bytea(data TEXT)
    RETURNS BYTEA
    AS 'MODULE_PATHNAME', 'text_to_bytea'
    LANGUAGE 'c'
    IMMUTABLE STRICT;

CREATE FUNCTION bytea_to_text(data BYTEA)
    RETURNS TEXT
    AS 'MODULE_PATHNAME', 'bytea_to_text'
    LANGUAGE 'c'
    IMMUTABLE STRICT;
