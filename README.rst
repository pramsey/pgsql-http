======================
PostgreSQL HTTP Client
======================
:Info: See `github <http://github.com/pramsey/pgsql-http>`_ for the latest source.
:Author: Paul Ramsey <pramsey@opengeo.org>

Motivation
==========

Wouldn't it be nice to be able to write a trigger that called a web service? Either to get back a result, or to poke that service into refreshing itself against the new state of the database?

This extension is for that. 

Example
=======

::

  SELECT http_get('http://localhost');

                                      http_get                                   
    ------------------------------------------------------------------------------
     (200,text/html,"HTTP/1.1 200 OK\r                                           +
     Date: Wed, 18 Apr 2012 22:18:03 GMT\r                                       +
     Server: Apache/2.2.21 (Unix) mod_ssl/2.2.21 OpenSSL/0.9.8r DAV/2 PHP/5.3.8\r+
     Content-Location: index.html.en\r                                           +
     Vary: negotiate\r                                                           +
     TCN: choice\r                                                               +
     Last-Modified: Thu, 09 Dec 2010 04:40:42 GMT\r                              +
     ETag: ""140c8-2c-496f2d71b8680""\r                                          +
     Accept-Ranges: bytes\r                                                      +
     Content-Length: 44\r                                                        +
     Content-Type: text/html\r                                                   +
     Content-Language: en\r                                                      +
     \r                                                                          +
     ","<html><body><h1>It works!</h1></body></html>")
    (1 row)


  SELECT status, content_type, content FROM http_get('http://localhost');

     status | content_type |                   content                    
    --------+--------------+----------------------------------------------
        200 | text/html    | <html><body><h1>It works!</h1></body></html>
    (1 row)


  SELECT headers FROM http_get('http://localhost');

                                       headers                                    
    ------------------------------------------------------------------------------
     HTTP/1.1 200 OK\r                                                           +
     Date: Wed, 18 Apr 2012 22:29:29 GMT\r                                       +
     Server: Apache/2.2.21 (Unix) mod_ssl/2.2.21 OpenSSL/0.9.8r DAV/2 PHP/5.3.8\r+
     Content-Location: index.html.en\r                                           +
     Vary: negotiate\r                                                           +
     TCN: choice\r                                                               +
     Last-Modified: Thu, 09 Dec 2010 04:40:42 GMT\r                              +
     ETag: "140c8-2c-496f2d71b8680"\r                                            +
     Accept-Ranges: bytes\r                                                      +
     Content-Length: 44\r                                                        +
     Content-Type: text/html\r                                                   +
     Content-Language: en\r                                                      +
     \r                                                                          +
    (1 row)

Installation
============

UNIX
----

If you have PostgreSQL devel packages and CURL devel packages installed, you should have ``pg_config`` and ``curl-config`` on your path, so you should be able to just run ``make``, then ``make install``, then in your database ``CREATE EXTENSION http``.

Windows
-------

Sorry, no story here yet.

Why This is a Bad Idea
======================

- "What happens if the web page takes a long time to return?" Your SQL call will just wait there until it does. Make sure your web service fails fast.
- "What if the web page returns junk?" Your SQL call will have to test for junk before doing anything with the payload.
- "What if the web page never returns?" I've found this code can really hang a back-end hard. The curl timeout settings need more testing and tweaking for faster failure and timeout.

Operation
=========

The extension is just a wrapping around CURL, which provides us the headers and the content body of the result. We get the status code and the content type by running a regex on the headers. All the information is that stuffed into a compound type for return, with slots for:

- status, the HTTP status code
- content_type, the mime-type of the response
- headers, the full text of the response headers
- content, the full text of the content

To Do
=====

- There is currently only one function, and no support for parameters or anything like that. Support for other HTTP verbs is an obvious enhancement. 
- Some kind of support for parameters and encoding that doesn't involve hand-balling text is another (but without an associative array type to hold the parameters, seems messy (hello, PgSQL 9.2)).
- Inevitably some web server will return gzip content (Content-Encoding) without being asked for it. Handling that gracefully would be good.

