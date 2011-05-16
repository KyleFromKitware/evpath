# Macros used for standard compiling/linking tools
    CC = /usr/local/cuda/bin/nvcc
    AR = ar

# Macros for various targets/libraries
    TARGETS  = libpd5.so pd5test
    SRCS    = 
    EXTLIBS =  
    OBJS    = ${SRCS:.c=.o}
    DEPS    = ${SRCS:.c=.d}



# Macros for flags and required libraries
    CCFLAGS  = -std=gnu99 -O2 -Wall -ggdb 
    LDFLAGS  = $(foreach LIB, $(EXTLIBS), -L $(dir $(LIB)) )
    LIBS     = -lrt -lpthread -lcrypto -lm
    INCLUDES = -I src/ $(foreach LIB, $(EXTLIBS), -I $(dir $(LIB)) )
    CCFLAGS += $(INCLUDES) 

# Phony targets
    .PHONY: all clean force
    all:: ${TARGETS} 

# Call makefile in directory of each external lib
    ${EXTLIBS}: force
	gmake -C $(dir $@)

    pd5test: deviceQuery.cpp pd5test.cpp
	/usr/local/cuda/bin/nvcc -o $@ $^ -L./ -lpd5

    libpd5.so: cuda_pd5.cpp cpu_pd5.cpp gpu_pd5.cu
	/usr/local/cuda/bin/nvcc -shared -o $@ -Xcompiler '-fPIC' $^ 


    ${DEPS}: %.d: %.c src/server.h makefile
	${CC} ${CCFLAGS} -MM $< > $@ 

    clean::
	echo $(DEPS)
	echo $(OBJS)
	echo $(TARGETS) 
	rm -f $(foreach LIB, $(EXTLIBS), $(dir $(LIB))*~ $(dir $(LIB))*.d $(dir $(LIB))*.o ${LIB} ) 
	rm -f src/*~ src/*.d src/*.o ${TARGETS} 
