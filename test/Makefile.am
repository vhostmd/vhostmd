
INCLUDES = \
    -I../libmetrics

noinst_PROGRAMS = test_static test_dyn

test_static_SOURCES = main.c
test_static_LDADD = ../libmetrics/libmetrics.la $(LIBXML_LIBS) -lxenstore

test_static_DEPENDENCIES = \
    ../libmetrics/libmetrics.h \
    ../libmetrics/libmetrics.la

test_dyn_SOURCES = main.c
test_dyn_CFLAGS = -DUSE_DL_OPEN
test_dyn_LDADD = ../libmetrics/libmetrics.la $(LIBXML_LIBS) -lxenstore

test_dyn_DEPENDENCIES = \
    ../libmetrics/libmetrics.h \
    ../libmetrics/libmetrics.la

valgrind:
	$(MAKE) CHECKER='valgrind --quiet --leak-check=full --suppressions=$(srcdir)/.valgrind.supp' tests

tests: $(noinst_PROGRAMS)
	@(echo '## regression tests')
	@($(CHECKER) ./test_static)
	@($(CHECKER) ./test_dyn)

