AM_CPPFLAGS = -I. -Iblas

noinst_LTLIBRARIES = liblinear.la
noinst_HEADERS = linear.h tron.h

liblinear_la_SOURCES = \
linear.cpp \
tron.cpp

if HAVE_BLAS
else
liblinear_la_SOURCES += \
blas/blas.h \
blas/blasp.h \
blas/daxpy.c \
blas/ddot.c \
blas/dnrm2.c \
blas/dscal.c
endif

liblinear_la_LIBADD = \
	$(BLAS_LDFLAGS)

noinst_PROGRAMS = train predict

noinst_DATA = .gitignore

.gitignore: Makefile.am
	cat $(top_builddir)/.gitignore > .gitignore
	echo "*.model" >> .gitignore
	for bin in $(PROGRAMS); do \
	  echo $$bin >> .gitignore; \
	done; \
	for bin in $(SCRIPTS); do \
	  echo $$bin >> .gitignore; \
	done

train_SOURCES = train.cpp
train_LDADD = liblinear.la

predict_SOURCES = predict.cpp
predict_LDADD = liblinear.la

EXTRA_DIST = \
COPYRIGHT \
README \
README.cicada \
Makefile.orig \
heart_scale

