
MODULE_big = http
OBJS = http.o
EXTENSION = http

DATA = \
	http--1.4.sql \
	http--1.3--1.4.sql \
	http--1.2--1.3.sql \
	http--1.1--1.2.sql \
	http--1.0--1.1.sql

REGRESS = http
EXTRA_CLEAN =

CURL_CONFIG = curl-config
PG_CONFIG = pg_config

CFLAGS += $(shell $(CURL_CONFIG) --cflags)
LIBS += $(shell $(CURL_CONFIG) --libs)
SHLIB_LINK := $(LIBS)

ifdef DEBUG
COPT			+= -O0
endif

PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

