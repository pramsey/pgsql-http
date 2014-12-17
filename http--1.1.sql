-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION http" to load this file. \quit

CREATE DOMAIN http_method AS text
CHECK (
    VALUE ILIKE 'get' OR
    VALUE ILIKE 'post' OR
    VALUE ILIKE 'put' OR
    VALUE ILIKE 'delete'
);

CREATE DOMAIN content_type AS text
CHECK (
    VALUE ~ '^\S+\/\S+'
);

CREATE TYPE http_header AS (
    field VARCHAR,
    value VARCHAR
);

CREATE TYPE http_response AS (
	status INTEGER,
	content_type VARCHAR,
	headers http_header[],
	content VARCHAR
);

CREATE TYPE http_request AS (
	method http_method,
    uri VARCHAR,
	headers http_header[],
	content_type VARCHAR,
	content VARCHAR
);

CREATE OR REPLACE FUNCTION http_header (field VARCHAR, value VARCHAR) 
    RETURNS http_header
    AS $$ SELECT field, value $$ 
    LANGUAGE 'sql';

CREATE OR REPLACE FUNCTION http_request(method http_method, uri VARCHAR, headers http_header[] DEFAULT NULL, content_type content_type DEFAULT NULL, content VARCHAR DEFAULT NULL)
    RETURNS http_request
    AS $$ SELECT method, uri, headers, content_type, content $$
    LANGUAGE 'sql'; 

CREATE OR REPLACE FUNCTION http(request http_request)
    RETURNS http_response
    AS 'MODULE_PATHNAME', 'http_request'
    LANGUAGE 'c';

CREATE OR REPLACE FUNCTION http_get(uri VARCHAR)
	RETURNS http_response
	AS $$ SELECT http(('GET', uri, NULL, NULL, NULL)::http_request) $$
    LANGUAGE 'sql';

CREATE OR REPLACE FUNCTION http_post(uri VARCHAR, content VARCHAR, content_type VARCHAR)
	RETURNS http_response
	AS $$ SELECT http(('POST', uri, NULL, content_type, content)::http_request) $$
    LANGUAGE 'sql';

CREATE OR REPLACE FUNCTION http_put(uri VARCHAR, content VARCHAR, content_type VARCHAR)
	RETURNS http_response
	AS $$ SELECT http(('PUT', uri, NULL, content_type, content)::http_request) $$
    LANGUAGE 'sql';

CREATE OR REPLACE FUNCTION http_delete(uri VARCHAR)
	RETURNS http_response
	AS $$ SELECT http(('DELETE', uri, NULL, NULL, NULL)::http_request) $$
    LANGUAGE 'sql';

CREATE OR REPLACE FUNCTION urlencode(string VARCHAR)
	RETURNS TEXT
	AS 'MODULE_PATHNAME'
	LANGUAGE 'c'
	IMMUTABLE STRICT;

