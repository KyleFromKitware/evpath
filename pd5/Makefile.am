AUTOMAKE_OPTIONS = no-dependencies foreign

.cu.o:
	$(NVCC) -o $@ -c {:content:}lt; $(NVCCFLAGS)

.cu.lo:
        $(LIBTOOL) --tag=CC --mode=compile $(NVCC) --compile $(NVCC_FLAGS) -o $@ $^ -static

lib_LTLIBRARIES=libpd5.la
libpd5_la_SOURCES=cuda_pd5.cpp cpu_pd5.cpp gpu_pd5.cu
libpd5_la_LDADD=@LIBS@ -lrt -lpthread -lcrypto -lm
#libpd5_la_CFLAGS=

bin_PROGRAMS=pd5test
pd5test_LDADD= -lrt -lpthread -lcrypto -lm -lpd5
pd5test_SOURCES= deviceQuery.cpp pd5test.cpp