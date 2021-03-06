# build options:
# 1. cmd line: linking = dynamic (default), static-except-glibc (recommended release), static
# 2. cmd line: debug = false (default => -O2), true (-g)


### modify these paths to local BLAS, Boost and htslib install directories
LBLAS = -lopenblas # alternatively -llapack (just need sgemm_)
BLAS_DIR = /opt/openblas/0.2.14/lib
BOOST_INSTALL_DIR = /home/pl88/boost_1_58_0/install
HTSLIB_DIR = /groups/price/poru/external_software/htslib/htslib-1.4
### NOTE: to reduce dependencies from htslib, configure htslib with: ./configure --disable-bz2 --disable-lzma --disable-plugins --disable-libcurl --disable-gcs --disable-s3

### these paths are used only for static linking
ZLIB_STATIC_DIR = /opt/zlib-1.2.8/lib # probably unnecessary on most systems
GLIBC_STATIC_DIR = /home/pl88/glibc-static/usr/lib64


ifeq ($(strip ${linking}),)
	linking = dynamic
endif

CC = g++

ifeq (${debug},true)
	CFLAGS += -g
else
	CFLAGS += -O2
endif
ifeq (${prof},true)
	CFLAGS += -g -pg
	LFLAGS += -pg
endif

CFLAGS += -std=c++0x -msse -msse2 -fopenmp -Wall
LFLAGS += -fopenmp


# add BLAS lib path
ifneq ($(strip ${BLAS_DIR}),)
	LPATHS += -L${BLAS_DIR}
	ifeq (${linking},dynamic)
		LPATHS += -Wl,-rpath,${BLAS_DIR}
	endif
endif

# add Boost include and lib paths
ifneq ($(strip ${BOOST_INSTALL_DIR}),)
	CPATHS += -I${BOOST_INSTALL_DIR}/include
	LPATHS += -L${BOOST_INSTALL_DIR}/lib
	ifeq (${linking},dynamic)
		LPATHS += -Wl,-rpath,${BOOST_INSTALL_DIR}/lib
	endif
endif

# add htslib include and lib paths
ifneq ($(strip ${HTSLIB_DIR}),)
	CPATHS += -I${HTSLIB_DIR}
	LPATHS += -L${HTSLIB_DIR}
	ifeq (${linking},dynamic)
		LPATHS += -Wl,-rpath,${HTSLIB_DIR}
	endif
endif

# add zlib.a path for static linking on Orchestra
ifneq ($(strip ${ZLIB_STATIC_DIR}),)
	ifneq (${linking},dynamic)
		LPATHS += -L${ZLIB_STATIC_DIR}
	endif
endif

# add flags for static linking; build LAPACK/MKL component of link line
ifeq (${linking},static)
	LFLAGS += -static
	LPATHS += -L${GLIBC_STATIC_DIR} -L${ZLIB_STATIC_DIR}
else ifeq (${linking},static-except-glibc)
	LFLAGS += -static-libgcc -static-libstdc++
	LPATHS += -L${ZLIB_STATIC_DIR}
endif

# build link line (minus flags)
LLIBS = -lhts -lboost_program_options -lboost_iostreams -lz ${LBLAS}
ifeq (${linking},static-except-glibc)
	L = ${LPATHS} -Wl,-Bstatic ${LLIBS} -Wl,-Bdynamic -lpthread -lm
else
	L = ${LPATHS} ${LLIBS} -lpthread -lm
endif


T = eagle
O = DipTreePBWT.o Eagle.o EagleImpMiss.o EagleParams.o EaglePBWT.o FileUtils.o GenoData.o HapHedge.o MapInterpolater.o MemoryUtils.o NumericUtils.o StaticMultimap.o StringUtils.o SyncedVcfData.o Timer.o
OMAIN = EagleMain.o $O

.PHONY: clean

$T: ${OMAIN}
	${CC} ${LFLAGS} -o $T ${OMAIN} $L

%.o: %.cpp
	${CC} ${CFLAGS} ${CPATHS} -o $@ -c $<
EagleMain.o: Version.hpp
Eagle.o: Version.hpp

all: $T

clean:
	rm -f *.o
	rm -f $T
