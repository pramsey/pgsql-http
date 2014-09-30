/***********************************************************************
 *
 * Project:  PgSQL HTTP
 * Purpose:  Main file.
 *
 ***********************************************************************
 * Copyright 2012 Paul Ramsey <pramsey@opengeo.org>
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
#define HTTP_VERSION "1.0"
#define HTTP_ENCODING "gzip"

/* System */
#include <regex.h>
#include <string.h>
#include <stdlib.h>

/* PostgreSQL */
#include <postgres.h>
#include <fmgr.h>
#include <funcapi.h>
#include <utils/builtins.h>

/* CURL */
#include <curl/curl.h>

/* Internal */
#include "stringbuffer.h"

/* Set up PgSQL */
PG_MODULE_MAGIC;

/* Startup */
void _PG_init(void);
void _PG_init(void)
{
	curl_global_init(CURL_GLOBAL_ALL);
}

/* Tear-down */
void _PG_fini(void);
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
	stringbuffer_t *sb = (stringbuffer_t*)userp;
	stringbuffer_write(sb, (char*)contents, realsize);
	return realsize;
}

/**
* Uses regex to find the value of a header. Very limited pattern right now, only
* searches for an alphanumeric string after the header name. Should be extended to
* search out to the end of the header line (\n) and optionally also to remove " marks.
*/
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

/**
* Our only function, currently only does a get. Could take in parameters
* if we wanted to get fancy and do HTTP URL encoding. Might be better to
* take in a JSON or HSTORE object in PgSQL 9.2+
*/
Datum http_get(PG_FUNCTION_ARGS);
PG_FUNCTION_INFO_V1(http_get);
Datum http_get(PG_FUNCTION_ARGS)
{
	/* Input */
	text *url = NULL;
	text *params = NULL;
	
	/* Processing */
	CURL *http_handle = NULL; 
	char *http_error_buffer = NULL;
	struct curl_slist *headers = NULL;
	stringbuffer_t *sb_data = stringbuffer_create();
	stringbuffer_t *sb_headers = stringbuffer_create();
	int http_return;
	long status;
	char *content_type = NULL;
	char status_str[128];

	/* Output */
	char **values;
	AttInMetadata *attinmeta;
	HeapTuple tuple;
	Datum result;

	/* We cannot get a null URL */
	if ( ! PG_ARGISNULL(0) )
		url = PG_GETARG_TEXT_P(0);
	else
		ereport(ERROR, (errmsg("A URL must be provided")));

	/* Load the parameters, if there are any */
	if ( ! PG_ARGISNULL(1) )
		params = PG_GETARG_TEXT_P(1);

	/* Initialize CURL */
	if ( ! (http_handle = curl_easy_init()) )
		ereport(ERROR, (errmsg("Unable to initialize CURL")));

	/* Set the user agent */
	curl_easy_setopt(http_handle, CURLOPT_USERAGENT, PG_VERSION_STR);
	
	/* Set the target URL */
	curl_easy_setopt(http_handle, CURLOPT_URL, text_to_cstring(url));

	/* Set up the error buffer */
	http_error_buffer = palloc(CURL_ERROR_SIZE);
	curl_easy_setopt(http_handle, CURLOPT_ERRORBUFFER, http_error_buffer);
	
	/* Set up the write-back function */
	curl_easy_setopt(http_handle, CURLOPT_WRITEFUNCTION, http_writeback);
	
	/* Set up the write-back buffer */
	curl_easy_setopt(http_handle, CURLOPT_WRITEDATA, (void*)sb_data);
	curl_easy_setopt(http_handle, CURLOPT_WRITEHEADER, (void*)sb_headers);
	
	/* Set up the HTTP timeout */
	curl_easy_setopt(http_handle, CURLOPT_TIMEOUT, 5);
	curl_easy_setopt(http_handle, CURLOPT_CONNECTTIMEOUT, 1);

	/* Set up the HTTP content encoding to gzip */
	/* curl_easy_setopt(http_handle, CURLOPT_ACCEPT_ENCODING, HTTP_ENCODING); */

	/* Follow redirects, as many as 5 */
	curl_easy_setopt(http_handle, CURLOPT_FOLLOWLOCATION, 1);
	curl_easy_setopt(http_handle, CURLOPT_MAXREDIRS, 5);
	
	/* Run it! */ 
	http_return = curl_easy_perform(http_handle);
//	elog(NOTICE, "Queried %s", text_to_cstring(url));

	/* Write out an error on failure */
	if ( http_return )
	{
		curl_easy_cleanup(http_handle);
		ereport(ERROR, (errmsg("CURL: %s", http_error_buffer)));
	}

	/* Read the metadata from the handle directly */
	if ( (CURLE_OK != curl_easy_getinfo(http_handle, CURLINFO_RESPONSE_CODE, &status)) ||
	     (CURLE_OK != curl_easy_getinfo(http_handle, CURLINFO_CONTENT_TYPE, &content_type)) )
	{
		curl_easy_cleanup(http_handle);
		ereport(ERROR, (errmsg("CURL: Error in curl_easy_getinfo")));
	}
	
	/* Print the status code out */
	snprintf(status_str, sizeof(status_str), "%ld", status);
	
	/* Make sure the content type is not null */
	if ( ! content_type )
		content_type = "";
	
	/* Prepare our return object */
	values = palloc(sizeof(char*) * 4);
	values[0] = status_str;
	values[1] = content_type;
	values[2] = (char*)stringbuffer_getstring(sb_headers);
	values[3] = (char*)stringbuffer_getstring(sb_data);

	/* Flip cstring values into a PgSQL tuple */
	attinmeta = TupleDescGetAttInMetadata(RelationNameGetTupleDesc("http_response"));
	tuple = BuildTupleFromCStrings(attinmeta, values);
	result = HeapTupleGetDatum(tuple);
	
	/* Clean up */
	curl_easy_cleanup(http_handle);
	pfree(http_error_buffer);
	stringbuffer_destroy(sb_headers);
	stringbuffer_destroy(sb_data);

	/* Return */
	PG_RETURN_DATUM(result);
}

Datum http_post(PG_FUNCTION_ARGS);
PG_FUNCTION_INFO_V1(http_post);
Datum http_post(PG_FUNCTION_ARGS)
{
	/* Input */
	text *url = NULL;
	text *data = NULL;
	text *params = NULL;
	text *text_contenttype = NULL;
	
	/* Processing */
	CURL *http_handle = NULL; 
	char *http_error_buffer = NULL;
	stringbuffer_t *sb_data = stringbuffer_create();
	stringbuffer_t *sb_headers = stringbuffer_create();
    stringbuffer_t *sb_contenttype = stringbuffer_create();
	int http_return;
	long status;
	char *content_type = NULL;
	char status_str[128];
	struct curl_slist *headers = NULL;

	/* Output */
	char **values;
	AttInMetadata *attinmeta;
	HeapTuple tuple;
	Datum result;
	/* We cannot get a null URL */
	if ( ! PG_ARGISNULL(0) )
		url = PG_GETARG_TEXT_P(0);
	else
		ereport(ERROR, (errmsg("A URL must be provided")));

	if ( ! PG_ARGISNULL(2) )
		data = PG_GETARG_TEXT_P(2);
	else
		ereport(ERROR, (errmsg("A DATA must be provided")));

	if ( ! PG_ARGISNULL(3) )
		text_contenttype = PG_GETARG_TEXT_P(3);
	else
		ereport(ERROR, (errmsg("content type must be provided")));
	/* Load the parameters, if there are any */
	if ( ! PG_ARGISNULL(1) )
		params = PG_GETARG_TEXT_P(1);

	/* Initialize CURL */
	if ( ! (http_handle = curl_easy_init()) )
		ereport(ERROR, (errmsg("Unable to initialize CURL")));

    stringbuffer_append(sb_contenttype, "Content-Type: ");
	stringbuffer_append(sb_contenttype, text_to_cstring(text_contenttype));
	headers = curl_slist_append(headers, stringbuffer_getstring(sb_contenttype));
    
    stringbuffer_clear(sb_contenttype);

    stringbuffer_append(sb_contenttype, "Accept: ");
	stringbuffer_append(sb_contenttype, text_to_cstring(text_contenttype));
	headers = curl_slist_append(headers, stringbuffer_getstring(sb_contenttype));
	headers = curl_slist_append(headers, "Connection: close");
    stringbuffer_destroy(sb_contenttype);
 

	headers = curl_slist_append(headers, "charsets: utf-8");
    curl_easy_setopt(http_handle, CURLOPT_HTTPHEADER, headers); 
	/* Set the user agent */
	curl_easy_setopt(http_handle, CURLOPT_USERAGENT, PG_VERSION_STR);

	curl_easy_setopt(http_handle, CURLOPT_FORBID_REUSE, 1);
	
	/* Set the target URL */
	curl_easy_setopt(http_handle, CURLOPT_URL, text_to_cstring(url));

	/* Set up the error buffer */
	http_error_buffer = palloc(CURL_ERROR_SIZE);
	curl_easy_setopt(http_handle, CURLOPT_ERRORBUFFER, http_error_buffer);
	
	
	/* Set up the write-back function */
	curl_easy_setopt(http_handle, CURLOPT_WRITEFUNCTION, http_writeback);
	
	/* Set up the write-back buffer */
	curl_easy_setopt(http_handle, CURLOPT_WRITEDATA, (void*)sb_data);
	curl_easy_setopt(http_handle, CURLOPT_WRITEHEADER, (void*)sb_headers);
	
	/* Set up the HTTP timeout */
	curl_easy_setopt(http_handle, CURLOPT_TIMEOUT, 5);
	curl_easy_setopt(http_handle, CURLOPT_CONNECTTIMEOUT, 1);

	/* Set up the HTTP content encoding to gzip */
	/*curl_easy_setopt(http_handle, CURLOPT_ACCEPT_ENCODING, HTTP_ENCODING);*/

	/* Follow redirects, as many as 5 */
	curl_easy_setopt(http_handle, CURLOPT_FOLLOWLOCATION, 1);
	curl_easy_setopt(http_handle, CURLOPT_MAXREDIRS, 5);

	curl_easy_setopt(http_handle, CURLOPT_POST, 1);
	curl_easy_setopt(http_handle, CURLOPT_POSTFIELDS, text_to_cstring(data));
	
	/* Run it! */ 
	http_return = curl_easy_perform(http_handle);
//	elog(NOTICE, "Queried %s", text_to_cstring(url));

	/* Write out an error on failure */
	if ( http_return )
	{
		curl_easy_cleanup(http_handle);
		ereport(ERROR, (errmsg("CURL: %s", http_error_buffer)));
	}

	/* Read the metadata from the handle directly */
	if ( (CURLE_OK != curl_easy_getinfo(http_handle, CURLINFO_RESPONSE_CODE, &status)) ||
	     (CURLE_OK != curl_easy_getinfo(http_handle, CURLINFO_CONTENT_TYPE, &content_type)) )
	{
		curl_easy_cleanup(http_handle);
		ereport(ERROR, (errmsg("CURL: Error in curl_easy_getinfo")));
	}
	
	/* Print the status code out */
	snprintf(status_str, sizeof(status_str), "%ld", status);
	
	/* Make sure the content type is not null */
	if ( ! content_type )
		content_type = "";
	
	/* Prepare our return object */
	values = palloc(sizeof(char*) * 4);
	values[0] = status_str;
	values[1] = content_type;
	values[2] = (char*)stringbuffer_getstring(sb_headers);
	values[3] = (char*)stringbuffer_getstring(sb_data);

	/* Flip cstring values into a PgSQL tuple */
	attinmeta = TupleDescGetAttInMetadata(RelationNameGetTupleDesc("http_response"));
	tuple = BuildTupleFromCStrings(attinmeta, values);
	result = HeapTupleGetDatum(tuple);
	
	/* Clean up */
	curl_easy_cleanup(http_handle);
	pfree(http_error_buffer);
	stringbuffer_destroy(sb_headers);
	stringbuffer_destroy(sb_data);

	/* Return */
	PG_RETURN_DATUM(result);
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

