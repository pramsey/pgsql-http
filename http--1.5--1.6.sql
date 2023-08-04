
ALTER DOMAIN http_method DROP CONSTRAINT IF EXISTS http_method_check;

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
