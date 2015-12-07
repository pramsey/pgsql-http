/***********************************************************************
 *
 * Project:  PgSQL HTTP
 * Purpose:  Main file.
 *
 ***********************************************************************
 * Copyright 2015 Paul Ramsey <pramsey@cleverelephant.ca>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 ***********************************************************************/

/* Constants */
#define HTTP_VERSION "1.1"
#define HTTP_ENCODING "gzip"

/* System */
#include <regex.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>	/* INT_MAX */

/* PostgreSQL */
#include <postgres.h>
#include <fmgr.h>
#include <funcapi.h>
#include <access/htup.h>
#include <catalog/namespace.h>
#include <lib/stringinfo.h>
#include <utils/array.h>
#include <utils/builtins.h>
#include <utils/lsyscache.h>
#include <utils/syscache.h>
#include <utils/typcache.h>
#include "utils/guc.h"

#if PG_VERSION_NUM >= 90300
#include <access/htup_details.h>
#endif

/* CURL */
#include <curl/curl.h>

/* Set up PgSQL */
PG_MODULE_MAGIC;


/* HTTP request methods we support */
typedef enum {
	HTTP_GET,
	HTTP_POST,
	HTTP_DELETE,
	HTTP_PUT
} http_method;

/* Components (and postitions) of the http_request tuple type */
enum {
	REQ_METHOD = 0,
	REQ_URI = 1,
	REQ_HEADERS = 2,
	REQ_CONTENT_TYPE = 3,
	REQ_CONTENT = 4
} http_request_type;

/* Components (and postitions) of the http_response tuple type */
enum {
	RESP_STATUS = 0,
	RESP_CONTENT_TYPE = 1,
	RESP_HEADERS = 2,
	RESP_CONTENT = 3
} http_response_type;

/* Components (and postitions) of the http_header tuple type */
enum {
	HEADER_FIELD = 0,
	HEADER_VALUE = 1
} http_header_type;



/* Function signatures */
void _PG_init(void);
void _PG_fini(void);
static size_t http_writeback(void *contents, size_t size, size_t nmemb, void *userp);
static size_t http_readback(void *buffer, size_t size, size_t nitems, void *instream);

static bool g_use_keepalive;
static int g_timeout_msec;

static CURL * g_http_handle = NULL;

/* Startup */
void _PG_init(void)
{
	DefineCustomBoolVariable("http.keepalive",
							 "reuse existing connections with keepalive",
							 NULL,
							 &g_use_keepalive,
							 false,
							 PGC_USERSET,
							 GUC_NOT_IN_SAMPLE,
							 NULL,
							 NULL,
							 NULL);
	
	DefineCustomIntVariable("http.timeout_msec",
							"request completion timeout in milliseconds",
							NULL,
							&g_timeout_msec,
							5000,
							0,
							INT_MAX,
							PGC_USERSET,
							GUC_NOT_IN_SAMPLE | GUC_UNIT_MS,
							NULL,
							NULL,
							NULL);
	
	curl_global_init(CURL_GLOBAL_ALL);
}

/* Tear-down */
void _PG_fini(void)
{
	if ( g_http_handle )
	{
		curl_easy_cleanup(g_http_handle);
		g_http_handle = NULL;
	}

	curl_global_cleanup();
	elog(NOTICE, "Goodbye from HTTP %s", HTTP_VERSION);
}

/**
* This function is passed into CURL as the CURLOPT_WRITEFUNCTION,
* this allows the  return values to be held in memory, in our case in a string.
*/
static size_t
http_writeback(void *contents, size_t size, size_t nmemb, void *userp)
{
	size_t realsize = size * nmemb;
	StringInfo si = (StringInfo)userp;
	appendBinaryStringInfo(si, (const char*)contents, (int)realsize);
	return realsize;
}

/**
* This function is passed into CURL as the CURLOPT_READFUNCTION,
* this allows the PUT operation to read the data it needs.
*/
static size_t
http_readback(void *buffer, size_t size, size_t nitems, void *instream)
{
	size_t realsize = size * nitems;
	StringInfo si = (StringInfo)instream;
	memcpy(buffer, si->data + si->cursor, realsize);
	si->cursor += realsize;
	return realsize;
}

static void
http_error(CURLcode err, char *error_buffer) 
{
	if ( strlen(error_buffer) > 0 )
		ereport(ERROR, (errmsg("%s", error_buffer)));
	else
		ereport(ERROR, (errmsg("%s", curl_easy_strerror(err))));
}

/* Utility macro to try a setopt and catch an error */
#define CURL_SETOPT(handle, opt, value) do { \
	err = curl_easy_setopt((handle), (opt), (value)); \
	if ( err != CURLE_OK ) \
	{ \
		http_error(err, http_error_buffer); \
		PG_RETURN_NULL(); \
	} \
	} while (0);


/**
*  Convert a request type string into the appropriate enumeration value.
*/
static http_method
request_type(const char *method)
{
	if ( strcasecmp(method, "GET") == 0 )
		return HTTP_GET;
	else if ( strcasecmp(method, "POST") == 0 )
		return HTTP_POST;
	else if ( strcasecmp(method, "PUT") == 0 )
		return HTTP_PUT;
	else if ( strcasecmp(method, "DELETE") == 0 )
		return HTTP_DELETE;
	else
		return HTTP_GET;
}

/**
* Given a field name and value, output a http_header tuple.
*/
static Datum
header_tuple(TupleDesc header_tuple_desc, const char *field, const char *value)
{
	HeapTuple header_tuple;
	int ncolumns;
	Datum *header_values;
	bool *header_nulls;

	/* Prepare our return object */
	ncolumns = header_tuple_desc->natts;
	header_values = palloc0(sizeof(Datum)*ncolumns);
	header_nulls = palloc0(sizeof(bool)*ncolumns);

	header_values[HEADER_FIELD] = CStringGetTextDatum(field);
	header_nulls[HEADER_FIELD] = false;
	header_values[HEADER_VALUE] = CStringGetTextDatum(value);
	header_nulls[HEADER_VALUE] = false;

	/* Build up a tuple from values/nulls lists */
	header_tuple = heap_form_tuple(header_tuple_desc, header_values, header_nulls);
	return HeapTupleGetDatum(header_tuple);
}

/**
* Quick and dirty, remove all \r from a StringInfo.
*/
static void
string_info_remove_cr(StringInfo si)
{
	int i = 0, j = 0;
	while ( si->data[i] )
	{
		if ( si->data[i] != '\r' )
			si->data[j++] = si->data[i++];
		else
			i++;
	}
	si->data[j] = '\0';
	si->len -= i-j;
	return;
}

/**
* Add an array of http_header tuples into a Curl string list.
*/
static struct curl_slist *
header_array_to_slist(ArrayType *array, struct curl_slist *headers)
{
	ArrayIterator iterator;
	Datum value;
	bool isnull;

#if PG_VERSION_NUM >= 90500
	iterator = array_create_iterator(array, 0, NULL);
#else
	iterator = array_create_iterator(array, 0);
#endif

	while( array_iterate(iterator, &value, &isnull) )
	{
		HeapTupleHeader rec;
		HeapTupleData tuple;
		Oid tup_type;
		int32 tup_typmod, ncolumns;
		TupleDesc tup_desc;
		size_t tup_len;
		Datum *values;
		bool *nulls;

		/* Skip null array items */
		if ( isnull )
			continue;
		
		rec = DatumGetHeapTupleHeader(value);
		tup_type = HeapTupleHeaderGetTypeId(rec);
		tup_typmod = HeapTupleHeaderGetTypMod(rec);
		tup_len = HeapTupleHeaderGetDatumLength(rec);
		tup_desc = lookup_rowtype_tupdesc(tup_type, tup_typmod);
		ncolumns = tup_desc->natts;

		/* Prepare for values / nulls to hold the data */
		values = (Datum *) palloc0(ncolumns * sizeof(Datum));
		nulls = (bool *) palloc0(ncolumns * sizeof(bool));

		/* Build a temporary HeapTuple control structure */
		tuple.t_len = tup_len;
		ItemPointerSetInvalid(&(tuple.t_self));
		tuple.t_tableOid = InvalidOid;
		tuple.t_data = rec;

		/* Break down the tuple into values/nulls lists */
		heap_deform_tuple(&tuple, tup_desc, values, nulls);

		/* Convert the data into a header */
		/* TODO: Ensure the header list is unique? Or leave that to the */
		/* server to deal with. */
		if ( ! nulls[HEADER_FIELD] )
		{
			char buffer[1024];
			char *header_val;
			char *header_fld = TextDatumGetCString(values[HEADER_FIELD]);

			/* Don't process "content-type" in the optional headers */
			if ( strlen(header_fld) <= 0 || strncasecmp(header_fld, "Content-Type", 12) == 0 )
			{
				elog(NOTICE, "'Content-Type' is not supported as an optional header");
				continue;
			}

			if ( nulls[HEADER_VALUE] )
				header_val = pstrdup("");
			else
				header_val = TextDatumGetCString(values[HEADER_VALUE]);

			snprintf(buffer, sizeof(buffer), "%s: %s", header_fld, header_val);
			elog(DEBUG2, "pgsql-http: optional request header '%s'", buffer);
			headers = curl_slist_append(headers, buffer);
			pfree(header_fld);
			pfree(header_val);
		}

		/* Free all the temporary structures */
		ReleaseTupleDesc(tup_desc);
		pfree(values);
		pfree(nulls);
	}
	array_free_iterator(iterator);

	return headers;
}

/**
* Convert a string of headers separated by newlines/CRs into an
* array of http_header tuples.
*/
static ArrayType *
header_string_to_array(StringInfo si)
{
	/* Array building */
	int arr_nelems = 0;
	int arr_elems_size = 8;
	Datum *arr_elems = palloc0(arr_elems_size*sizeof(Datum));
	Oid elem_type;
	int16 elem_len;
	bool elem_byval;
	char elem_align;

	/* Header handling */
	TupleDesc header_tuple_desc;

	/* Regex support */
	const char *regex_pattern = "^([^ \t\r\n\v\f]+): ?([^ \t\r\n\v\f]+.*)$";
	regex_t regex;
	regmatch_t pmatch[3];
	int reti;
	static int rvsz = 256;
	char rv1[rvsz];
	char rv2[rvsz];

	/* Compile the regular expression */
	reti = regcomp(&regex, regex_pattern, REG_ICASE | REG_EXTENDED | REG_NEWLINE );
	if ( reti )
		elog(ERROR, "Unable to compile regex pattern '%s'", regex_pattern);

	/* Prepare tuple building metadata */
	header_tuple_desc = RelationNameGetTupleDesc("http_header");

	/* Prepare array building metadata */
	elem_type = header_tuple_desc->tdtypeid;		
	get_typlenbyvalalign(elem_type, &elem_len, &elem_byval, &elem_align);

	/* Loop through string, matching regex pattern */
	si->cursor = 0;
	while ( ! regexec(&regex, si->data+si->cursor, 3, pmatch, 0) )
	{
		/* Read the regex match results */
		int eo0 = pmatch[0].rm_eo;
		int so1 = pmatch[1].rm_so;
		int eo1 = pmatch[1].rm_eo;
		int so2 = pmatch[2].rm_so;
		int eo2 = pmatch[2].rm_eo;

		/* Copy the matched portions out of the string */
		memcpy(rv1, si->data+si->cursor+so1, eo1-so1 < rvsz ? eo1-so1 : rvsz);
		rv1[eo1-so1] = '\0';
		memcpy(rv2, si->data+si->cursor+so2, eo2-so2 < rvsz ? eo2-so2 : rvsz);
		rv2[eo2-so2] = '\0';

		/* Move forward for next match */
		si->cursor += eo0;

		/* Increase elements array size if necessary */
		if ( arr_nelems >= arr_elems_size )
		{
			arr_elems_size *= 2;
			arr_elems = repalloc(arr_elems, arr_elems_size*sizeof(Datum));
		}
		arr_elems[arr_nelems] = header_tuple(header_tuple_desc, rv1, rv2);
		arr_nelems++;
	}

	ReleaseTupleDesc(header_tuple_desc);
	return construct_array(arr_elems, arr_nelems, elem_type, elem_len, elem_byval, elem_align);
}

/**
* Master HTTP request function, takes in an http_request tuple and outputs
* an http_response tuple.
*/
Datum http_request(PG_FUNCTION_ARGS);
PG_FUNCTION_INFO_V1(http_request);
Datum http_request(PG_FUNCTION_ARGS)
{
	/* Input */
	HeapTupleHeader rec;
	HeapTupleData tuple;
	Oid tup_type;
	int32 tup_typmod;
	TupleDesc tup_desc;
	int ncolumns;
	Datum *values;
	bool *nulls;

	char *uri;
	char *method_str;
	http_method method;

	/* Processing */
	CURLcode err;
	char http_error_buffer[CURL_ERROR_SIZE];
	
	struct curl_slist *headers = NULL;
	StringInfoData si_data;
	StringInfoData si_headers;
	StringInfoData si_read;

	int http_return;
	long status;
	char *content_type = NULL;

	/* Output */
	HeapTuple tuple_out;


	/* Version check */
	curl_version_info_data *version_info = curl_version_info(CURLVERSION_NOW);
	elog(DEBUG2, "pgsql-http: curl version %s", version_info->version);
	elog(DEBUG2, "pgsql-http: curl version number 0x%x", version_info->version_num);
	elog(DEBUG2, "pgsql-http: ssl version %s", version_info->ssl_version);

	/* 0x071400 == 7.20.0, each hex digit is for one version level (0xMMmmpp) */
	if ( version_info->version_num < 0x071400 )
	{
		elog(ERROR, "pgsql-http requires Curl version 0.7.20 or higher");
		PG_RETURN_NULL();
	}


	/* We cannot handle a null request */
	if ( ! PG_ARGISNULL(0) )
		rec = PG_GETARG_HEAPTUPLEHEADER(0);
	else
	{
		elog(ERROR, "An http_request must be provided");
		PG_RETURN_NULL();
	}

	/*************************************************************************
	* Build and run a curl request from the http_request argument
	*************************************************************************/

	/* Zero out static memory */
	memset(http_error_buffer, 0, sizeof(http_error_buffer));

	/* Extract type info from the tuple itself */
	tup_type = HeapTupleHeaderGetTypeId(rec);
	tup_typmod = HeapTupleHeaderGetTypMod(rec);
	tup_desc = lookup_rowtype_tupdesc(tup_type, tup_typmod);
	ncolumns = tup_desc->natts;

	/* Build a temporary HeapTuple control structure */
	tuple.t_len = HeapTupleHeaderGetDatumLength(rec);
	ItemPointerSetInvalid(&(tuple.t_self));
	tuple.t_tableOid = InvalidOid;
	tuple.t_data = rec;

	/* Prepare for values / nulls */
	values = (Datum *) palloc0(ncolumns * sizeof(Datum));
	nulls = (bool *) palloc0(ncolumns * sizeof(bool));

	/* Break down the tuple into values/nulls lists */
	heap_deform_tuple(&tuple, tup_desc, values, nulls);

	/* Read the URI */
	if ( nulls[REQ_URI] )
		elog(ERROR, "http_request.uri is NULL");
	uri = TextDatumGetCString(values[REQ_URI]);

	/* Read the method */
	if ( nulls[REQ_METHOD] )
		elog(ERROR, "http_request.method is NULL");
	method_str = TextDatumGetCString(values[REQ_METHOD]);
	method = request_type(method_str);
	elog(DEBUG2, "pgsql-http: method '%s'", method_str);
	pfree(method_str);

	/* Initialize the global handle if needed */
	if (!g_http_handle)
		g_http_handle = curl_easy_init();

	if ( !g_http_handle )
		ereport(ERROR, (errmsg("Unable to initialize CURL")));

	/* Set the target URL */
	CURL_SETOPT(g_http_handle, CURLOPT_URL, uri);

	/* Set the user agent */
	CURL_SETOPT(g_http_handle, CURLOPT_USERAGENT, PG_VERSION_STR);

	if ( g_use_keepalive )
	{
		/* Keep sockets held open */
		CURL_SETOPT(g_http_handle, CURLOPT_FORBID_REUSE, 0);
	}
	else
	{
		/* Keep sockets from being held open */
		CURL_SETOPT(g_http_handle, CURLOPT_FORBID_REUSE, 1);
	}

	/* Set up the error buffer */
	CURL_SETOPT(g_http_handle, CURLOPT_ERRORBUFFER, http_error_buffer);

	/* Set up the write-back function */
	CURL_SETOPT(g_http_handle, CURLOPT_WRITEFUNCTION, http_writeback);

	/* Set up the write-back buffer */
	initStringInfo(&si_data);
	initStringInfo(&si_headers);
	CURL_SETOPT(g_http_handle, CURLOPT_WRITEDATA, (void*)(&si_data));
	CURL_SETOPT(g_http_handle, CURLOPT_WRITEHEADER, (void*)(&si_headers));

	/* Set up the HTTP timeout */
	CURL_SETOPT(g_http_handle, CURLOPT_TIMEOUT_MS, g_timeout_msec);
	CURL_SETOPT(g_http_handle, CURLOPT_CONNECTTIMEOUT, 1);

	/* Set the HTTP content encoding to gzip */
	/*curl_easy_setopt(g_http_handle, CURLOPT_ACCEPT_ENCODING, HTTP_ENCODING);*/

	/* Follow redirects, as many as 5 */
	CURL_SETOPT(g_http_handle, CURLOPT_FOLLOWLOCATION, 1);
	CURL_SETOPT(g_http_handle, CURLOPT_MAXREDIRS, 5);

	if ( g_use_keepalive )
	{
		/* Add a keep alive option to the headers to reuse network sockets */
		headers = curl_slist_append(headers, "Connection: Keep-Alive");
	}
	else
	{
		/* Add a close option to the headers to avoid open network sockets */
		headers = curl_slist_append(headers, "Connection: close");
	}

	/* Let our charset preference be known */
	headers = curl_slist_append(headers, "Charsets: utf-8");

	/* Handle optional headers */
	if ( ! nulls[REQ_HEADERS] )
	{
		ArrayType *array = DatumGetArrayTypeP(values[REQ_HEADERS]);
		headers = header_array_to_slist(array, headers);
	}

	/* Specific handling for methods that send a content payload */
	if ( method == HTTP_POST || method == HTTP_PUT )
	{
		text *content_text;
		size_t content_size;
		char *content_type;
		char buffer[1024];

		/* Read the content type */
		if ( nulls[REQ_CONTENT_TYPE] )
			elog(ERROR, "http_request.content_type is NULL");
		content_type = TextDatumGetCString(values[REQ_CONTENT_TYPE]);

		/* Add content type to the headers */
		snprintf(buffer, sizeof(buffer), "Content-Type: %s", content_type);
		headers = curl_slist_append(headers, buffer);
		pfree(content_type);

		/* Read the content */
		if ( nulls[REQ_CONTENT] )
			elog(ERROR, "http_request.content is NULL");
		content_text = DatumGetTextP(values[REQ_CONTENT]);
		content_size = VARSIZE(content_text) - VARHDRSZ;

		if ( method == HTTP_POST )
		{
			/* Add the content to the payload */
			CURL_SETOPT(g_http_handle, CURLOPT_POST, 1);
			CURL_SETOPT(g_http_handle, CURLOPT_POSTFIELDS, text_to_cstring(content_text));
		}
		else if ( method == HTTP_PUT )
		{
			initStringInfo(&si_read);
			appendBinaryStringInfo(&si_read, VARDATA(content_text), content_size);
			CURL_SETOPT(g_http_handle, CURLOPT_UPLOAD, 1);
			CURL_SETOPT(g_http_handle, CURLOPT_READFUNCTION, http_readback);
			CURL_SETOPT(g_http_handle, CURLOPT_READDATA, &si_read);
			CURL_SETOPT(g_http_handle, CURLOPT_INFILESIZE, content_size);
		}
		else
		{
			/* Never get here */
			elog(ERROR, "illegal HTTP method");
		}
	}
	else if ( method == HTTP_DELETE )
	{
		CURL_SETOPT(g_http_handle, CURLOPT_CUSTOMREQUEST, "DELETE");
	}

	/* Set the headers */
	CURL_SETOPT(g_http_handle, CURLOPT_HTTPHEADER, headers);

	/* Run it! */
	http_return = curl_easy_perform(g_http_handle);
	elog(DEBUG2, "pgsql-http: queried '%s'", uri);

	/* Clean up some input things we don't need anymore */
	ReleaseTupleDesc(tup_desc);
	pfree(values);
	pfree(nulls);

	/*************************************************************************
	* Create an http_response object from the curl results
	*************************************************************************/

	/* Write out an error on failure */
	if ( http_return != CURLE_OK )
	{
		curl_slist_free_all(headers);
		curl_easy_cleanup(g_http_handle);
		g_http_handle = NULL;
		http_error(http_return, http_error_buffer);
	}

	/* Read the metadata from the handle directly */
	if ( (CURLE_OK != curl_easy_getinfo(g_http_handle, CURLINFO_RESPONSE_CODE, &status)) ||
	     (CURLE_OK != curl_easy_getinfo(g_http_handle, CURLINFO_CONTENT_TYPE, &content_type)) )
	{
		curl_slist_free_all(headers);
		curl_easy_cleanup(g_http_handle);
		g_http_handle = NULL;
		ereport(ERROR, (errmsg("CURL: Error in curl_easy_getinfo")));
	}

	/* Prepare our return object */
	tup_desc = RelationNameGetTupleDesc("http_response");
	ncolumns = tup_desc->natts;
	values = palloc0(sizeof(Datum)*ncolumns);
	nulls = palloc0(sizeof(bool)*ncolumns);

	/* Status code */
	values[RESP_STATUS] = Int64GetDatum(status);
	nulls[RESP_STATUS] = false;

	/* Content type */
	if ( content_type )
	{
		values[RESP_CONTENT_TYPE] = CStringGetTextDatum(content_type);
		nulls[RESP_CONTENT_TYPE] = false;
	}
	else
	{
		values[RESP_CONTENT_TYPE] = (Datum)0;
		nulls[RESP_CONTENT_TYPE] = true;
	}

	/* Headers array */
	if ( si_headers.len )
	{
		/* Strip the carriage-returns, because who cares? */
		string_info_remove_cr(&si_headers);
		// elog(NOTICE, "header string: '%s'", si_headers.data);
		values[RESP_HEADERS] = PointerGetDatum(header_string_to_array(&si_headers));
		nulls[RESP_HEADERS] = false;
	}
	else
	{
		values[RESP_HEADERS] = (Datum)0;
		nulls[RESP_HEADERS] = true;
	}

	/* Content */
	if ( si_data.len )
	{
		values[RESP_CONTENT] = PointerGetDatum(cstring_to_text_with_len(si_data.data, si_data.len));
		nulls[RESP_CONTENT] = false;
	}
	else
	{
		values[RESP_CONTENT] = (Datum)0;
		nulls[RESP_CONTENT] = true;
	}

	/* Build up a tuple from values/nulls lists */
	tuple_out = heap_form_tuple(tup_desc, values, nulls);

	/* Clean up */
	ReleaseTupleDesc(tup_desc);
	if ( !g_use_keepalive )
	{
		curl_easy_cleanup(g_http_handle);
		g_http_handle = NULL;
	}
	curl_slist_free_all(headers);
	pfree(si_headers.data);
	pfree(si_data.data);
	pfree(values);
	pfree(nulls);

	/* Return */
	PG_RETURN_DATUM(HeapTupleGetDatum(tuple_out));
}




/* URL Encode Escape Chars */
/* 48-57 (0-9) 65-90 (A-Z) 97-122 (a-z) 95 (_) 45 (-) */

static int chars_to_not_encode[] = {
	0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,1,0,0,1,1,
	1,1,1,1,1,1,1,1,0,0,
	0,0,0,0,0,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,
	1,0,0,0,0,1,0,1,1,1,
	1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,
	1,1,1,0,0,0,0,0
};



/**
* Utility function for users building URL encoded requests, applies
* standard URL encoding to an input string.
*/
Datum urlencode(PG_FUNCTION_ARGS);
PG_FUNCTION_INFO_V1(urlencode);
Datum urlencode(PG_FUNCTION_ARGS)
{
	text *txt = PG_GETARG_TEXT_P(0); /* Declare strict, so no test for NULL input */
	size_t txt_size = VARSIZE(txt) - VARHDRSZ;
	char *str_in, *str_out, *ptr;
	int i, rv;

	/* Point into the string */
	str_in = VARDATA(txt);

	/* Prepare the output string */
	str_out = palloc0(txt_size * 4);
	ptr = str_out;

	for ( i = 0; i < txt_size; i++ )
	{

		/* Break on NULL */
		if ( str_in[i] == '\0' )
			break;

		/* Replace ' ' with '+' */
		if ( str_in[i] == ' ' )
		{
			*ptr = '+';
			ptr++;
			continue;
		}

		/* Pass basic characters through */
		if ( str_in[i] < 127 && chars_to_not_encode[(int)(str_in[i])] )
		{
			*ptr = str_in[i];
			ptr++;
			continue;
		}

		/* Encode the remaining chars */
		rv = snprintf(ptr, 4, "%%%02X", str_in[i]);
		if ( rv < 0 )
			PG_RETURN_NULL();

		/* Move pointer forward */
		ptr += 3;
	}
	*ptr = '\0';

	PG_RETURN_TEXT_P(cstring_to_text(str_out));
}

// Local Variables:
// mode: C++
// tab-width: 4
// c-basic-offset: 4
// indent-tabs-mode: t
// End:
