-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION http" to load this file. \quit

CREATE TYPE http_response AS (
	status INTEGER,
	content_type VARCHAR,
	headers VARCHAR,
	content VARCHAR
);

CREATE OR REPLACE FUNCTION http_get(url VARCHAR, params VARCHAR DEFAULT NULL)
	RETURNS http_response
	AS 'MODULE_PATHNAME'
	LANGUAGE C;

CREATE OR REPLACE FUNCTION urlencode(string VARCHAR)
	RETURNS text
	AS 'MODULE_PATHNAME'
	LANGUAGE C
	IMMUTABLE STRICT;
