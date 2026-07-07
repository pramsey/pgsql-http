
MODULE_big = http
OBJS = http.o
EXTENSION = http

EXTVERSION = $(shell grep default_version $(EXTENSION).control | \
               cut -f2 -d= | tr -d "'" | tr -d " ")

DATA = $(wildcard *.sql)

REGRESS = http
EXTRA_CLEAN =

CURL_CONFIG = curl-config
PG_CONFIG = pg_config


CFLAGS += $(shell $(CURL_CONFIG) --cflags)
LIBS += $(shell $(CURL_CONFIG) --libs)
SHLIB_LINK := $(LIBS)
PG_CFLAGS += -D HTTP_VERSION=\"$(EXTVERSION)\"

ifdef DEBUG
COPT			+= -O0 -Werror -g
endif

PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

