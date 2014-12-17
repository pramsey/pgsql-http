/***********************************************************************
 *
 * Project:  PgSQL HTTP
 * Purpose:  Main file.
 *
 ***********************************************************************
 * Copyright 2014 Paul Ramsey <pramsey@cleverelephant.ca>
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

/* PostgreSQL */
#include <postgres.h>
#include <fmgr.h>
#include <funcapi.h>
#include <access/htup.h>
#include <access/htup_details.h>
#include <lib/stringinfo.h>
#include <utils/builtins.h>
#include <utils/typcache.h>

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


/* Function signatures */
void _PG_init(void);
void _PG_fini(void);
static size_t http_writeback(void *contents, size_t size, size_t nmemb, void *userp);
static size_t http_readback(void *buffer, size_t size, size_t nitems, void *instream);

/* Startup */
void _PG_init(void)
{
	curl_global_init(CURL_GLOBAL_ALL);
}

/* Tear-down */
void _PG_fini(void)
{
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


/**
* Uses regex to find the value of a header. Very limited pattern right now, only
* searches for an alphanumeric string after the header name. Should be extended to
* search out to the end of the header line (\n) and optionally also to remove " marks.
*/
#if 0
static char*
header_value(const char* header_str, const char* header_name)
{
	const char *regex_template = "%s: \\([[:alnum:]+/-]\\{1,\\}\\)";
	regex_t regex;
	char regex_err_buf[128];
	char regex_buf[256];
	regmatch_t pmatch[2];
	int reti;
	char *return_str;
	
	/* Prepare our regex string */
	reti = snprintf(regex_buf, sizeof(regex_buf), regex_template, header_name);
	if ( reti < 0 )
		ereport(ERROR, (errmsg("Could not prepare regex string")));

	/* Compile the regular expression */
	reti = regcomp(&regex, regex_buf, REG_ICASE);
	if ( reti )
		ereport(ERROR, (errmsg("Could not compile regex")));

	/* Execute regular expression */
	reti = regexec(&regex, header_str, 2, pmatch, 0);
	if ( ! reti )
	{
		/* Got a match */
		int so = pmatch[1].rm_so;
		int eo = pmatch[1].rm_eo;
		return_str = palloc(eo-so+1);
		memcpy(return_str, header_str + so, eo-so);
		return_str[eo-so] = '\0';
		regfree(&regex);
		return return_str;
	}
	else if( reti == REG_NOMATCH )
	{
		ereport(ERROR, (errmsg("Could not find %s header", header_name)));
	}
	else
	{
		regerror(reti, &regex, regex_err_buf, sizeof(regex_err_buf));
		ereport(ERROR, (errmsg("Regex match failed: %s\n", regex_err_buf)));
	}

	/* Free compiled regular expression if you want to use the regex_t again */
	regfree(&regex);
	return_str = palloc(1);
	return_str[0] = '\0';
	return return_str;
}
#endif
	
/* Utility macro to try a setopt and catch an error */
#define CURL_SETOPT(handle, opt, value) do { \
	err = curl_easy_setopt((handle), (opt), (value)); \
	if ( err != CURLE_OK ) \
	{ \
		elog(ERROR, "CURL error: %s", curl_easy_strerror(err)); \
		PG_RETURN_NULL(); \
	} \
	} while (0);



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
	
	const char *uri;
	http_method method;
	
	/* Processing */
	CURL *http_handle = NULL;
	CURLcode err; 
	char *http_error_buffer = NULL;
	struct curl_slist *headers = NULL;
	StringInfoData si_data;
	StringInfoData si_headers;
	StringInfoData si_read;
	
	int http_return;
	long status;
	char *content_type = NULL;

	/* Output */
	HeapTuple tuple_out;

	/* We cannot handle a null request */
	if ( ! PG_ARGISNULL(0) )
		rec = PG_GETARG_HEAPTUPLEHEADER(0);
	else
		elog(ERROR, "An http_request must be provided");
	
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
    values = (Datum *) palloc(ncolumns * sizeof(Datum));
    nulls = (bool *) palloc(ncolumns * sizeof(bool));

    /* Break down the tuple into a values list */
    heap_deform_tuple(&tuple, tup_desc, values, nulls);
	
	/* Read the URI */
	if ( nulls[REQ_URI] )
		elog(ERROR, "http_request.uri is NULL");
	uri = TextDatumGetCString(values[REQ_URI]);

	/* Read the method */
	if ( nulls[REQ_METHOD] )
		elog(ERROR, "http_request.method is NULL");
	method = request_type(TextDatumGetCString(values[REQ_METHOD]));

	/* Initialize CURL */
	if ( ! (http_handle = curl_easy_init()) )
		ereport(ERROR, (errmsg("Unable to initialize CURL")));

	/* Set the target URL */
	CURL_SETOPT(http_handle, CURLOPT_URL, uri);

	/* Set the user agent */
	CURL_SETOPT(http_handle, CURLOPT_USERAGENT, PG_VERSION_STR);
	
	/* Keep sockets from being held open */
	CURL_SETOPT(http_handle, CURLOPT_FORBID_REUSE, 1);	

	/* Set up the error buffer */
	http_error_buffer = palloc(CURL_ERROR_SIZE);
	CURL_SETOPT(http_handle, CURLOPT_ERRORBUFFER, http_error_buffer);
	
	/* Set up the write-back function */
	CURL_SETOPT(http_handle, CURLOPT_WRITEFUNCTION, http_writeback);
	
	/* Set up the write-back buffer */
	initStringInfo(&si_data);
	initStringInfo(&si_headers);		
	CURL_SETOPT(http_handle, CURLOPT_WRITEDATA, (void*)(&si_data));
	CURL_SETOPT(http_handle, CURLOPT_WRITEHEADER, (void*)(&si_headers));
	
	/* Set up the HTTP timeout */
	CURL_SETOPT(http_handle, CURLOPT_TIMEOUT, 5);
	CURL_SETOPT(http_handle, CURLOPT_CONNECTTIMEOUT, 1);

	/* Set up the HTTP content encoding to gzip */
	/*curl_easy_setopt(http_handle, CURLOPT_ACCEPT_ENCODING, HTTP_ENCODING);*/

	/* Follow redirects, as many as 5 */
	CURL_SETOPT(http_handle, CURLOPT_FOLLOWLOCATION, 1);
	CURL_SETOPT(http_handle, CURLOPT_MAXREDIRS, 5);	
	
	/* Add a close option to the headers to avoid open network sockets */
	headers = curl_slist_append(headers, "Connection: close");
	CURL_SETOPT(http_handle, CURLOPT_HTTPHEADER, headers);
	
	/* Let our charset preference be known */
	headers = curl_slist_append(headers, "charsets: utf-8");
	
	/* TODO: actually handle optional headers here */

	/* Specific handling for methods that send a content payload */
	if ( method == HTTP_POST || method == HTTP_PUT )
	{
		text *content_text;
		size_t content_size;
		char *content_type;
		char buffer[1024];
		size_t buffersz = sizeof(buffer);

		/* Read the content type */
		if ( nulls[REQ_CONTENT_TYPE] )
			elog(ERROR, "http_request.content_type is NULL");
		content_type = text_to_cstring(DatumGetTextP(values[REQ_CONTENT_TYPE]));

		/* Add content type to the headers */
		snprintf(buffer, buffersz, "Content-Type: %s", content_type);
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
			CURL_SETOPT(http_handle, CURLOPT_POST, 1);
			CURL_SETOPT(http_handle, CURLOPT_POSTFIELDS, text_to_cstring(content_text));
		}
		else if ( method == HTTP_PUT )
		{
			initStringInfo(&si_read);
			appendBinaryStringInfo(&si_read, VARDATA(content_text), content_size);
			CURL_SETOPT(http_handle, CURLOPT_UPLOAD, 1);
			CURL_SETOPT(http_handle, CURLOPT_READFUNCTION, http_readback);
			CURL_SETOPT(http_handle, CURLOPT_READDATA, si_read.data);
			CURL_SETOPT(http_handle, CURLOPT_INFILESIZE, content_size);
		}
		else 
		{
			/* Never get here */
		}
	}
	else if ( method == HTTP_DELETE )
	{
		elog(ERROR, "http_request.method == DELETE not yet implemented");
	}

	/* Run it! */ 
	http_return = curl_easy_perform(http_handle);
	elog(DEBUG2, "pgsql-http: queried %s", uri);

	/* Clean up some input things we don't need anymore */
	ReleaseTupleDesc(tup_desc);
	pfree(values);
	pfree(nulls);

	/* Write out an error on failure */
	if ( http_return )
	{
		curl_easy_cleanup(http_handle);
		curl_slist_free_all(headers);
		ereport(ERROR, (errmsg("CURL: %s", http_error_buffer)));
	}

	/* Read the metadata from the handle directly */
	if ( (CURLE_OK != curl_easy_getinfo(http_handle, CURLINFO_RESPONSE_CODE, &status)) ||
	     (CURLE_OK != curl_easy_getinfo(http_handle, CURLINFO_CONTENT_TYPE, &content_type)) )
	{
		curl_easy_cleanup(http_handle);
		curl_slist_free_all(headers);
		ereport(ERROR, (errmsg("CURL: Error in curl_easy_getinfo")));
	}
	
	/* Make sure the content type is not null */
	if ( ! content_type )
		content_type = "";
	
	/* Prepare our return object */
	tup_desc = RelationNameGetTupleDesc("http_response");
	ncolumns = tup_desc->natts;
	values = palloc(sizeof(Datum)*ncolumns);
	nulls = palloc(sizeof(bool)*ncolumns);

	/* Status code */
	values[0] = Int64GetDatum(status);
	nulls[0] = false;
	/* Content type */
	values[1] = CStringGetTextDatum(content_type);
	nulls[1] = false;
	/* Headers array */
	values[2] = (Datum)0;
	nulls[2] = true;
	/* Content */
	values[3] = PointerGetDatum(cstring_to_text_with_len(si_data.data, si_data.len));
	nulls[3] = false;

	tuple_out = heap_form_tuple(tup_desc, values, nulls);
		
	/* Clean up */
	ReleaseTupleDesc(tup_desc);
	curl_easy_cleanup(http_handle);
	curl_slist_free_all(headers);
	pfree(http_error_buffer);
	pfree(si_headers.data);
	pfree(si_data.data);

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


Datum urlencode(PG_FUNCTION_ARGS);
PG_FUNCTION_INFO_V1(urlencode);
Datum urlencode(PG_FUNCTION_ARGS)
{
	text *txt = PG_GETARG_TEXT_P(0); /* Declare strict, so no test for NULL input */
	size_t txt_size = VARSIZE(txt) - VARHDRSZ;
	char *str_in, *str_out, *ptr;
	int i, rv;
	
	/* Point into the string */
	str_in = (char*)txt + VARHDRSZ;
	
	/* Prepare the output string */
	str_out = palloc(txt_size * 4);
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
		ptr+= 3;
	}
	*ptr = '\0';
	
	PG_RETURN_TEXT_P(cstring_to_text(str_out));
}

