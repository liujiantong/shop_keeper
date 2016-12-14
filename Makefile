#
# http://www.gnu.org/software/make/manual/make.html
#
CPP:=g++
CC:=gcc

ifndef PLATFORM
	PLATFORM:=__MAC_OSX__
endif

#CFLAGS:=-O3 -DLOGGER_ENABLE -DSK_RELEASE_VERSION -D$(PLATFORM)
CFLAGS:=-ggdb -DLOGGER_ENABLE -D$(PLATFORM)
# CFLAGS:=-Wall -ggdb -DLOGGER_ENABLE

FFMPEG_INC:=$(shell pkg-config --cflags libavformat libavcodec libswscale libavutil libswresample)
FMPEG_LIBS:=$(shell pkg-config --libs libavformat libavcodec libavfilter libswscale libavutil libswresample libavdevice)

OPENCV_INC:=$(shell pkg-config --cflags opencv)
ifdef __LINUX__
	OPENCV_LIBS:=`pkg-config --libs opencv`
else
	OPENCV_LIBS:=`pkg-config --libs opencv|sed 's/\/anaconda/Users\/lycaojh\/opencv-2\.4\.9\/build/g;'`
endif

ZMQ_LIBS:=-lczmq -lzmq
SQLITE_LIB:=-lsqlite3

INCLUDES:=$(FFMPEG_INC) $(OPENCV_INC)
LIBS:=$(FMPEG_LIBS) $(OPENCV_LIBS) $(ZMQ_LIBS) -lm

EXE:=cv_viewer sk_viewer sk_main


.c.o:
	$(CC) -c $(CFLAGS) $(INCLUDES) $<
.cpp.o:
	$(CPP) -c $(CFLAGS) $(INCLUDES) $<

#
# $< is the first dependency in the dependency list
# $@ is the target name
# $^ is the dependency list
#
all: $(EXE) picort.o

tags: *.cpp *.hpp *.c *.h
	rm -rf $(EXE) .tags*
	ctags -R --languages=C,C++ -f .tags .
#	ctags -R -f .tags *.cpp *.hpp *.c *.h

sk_main: sk_main.o sk_video_surv.o picort.o sk_blob_tracker.o logger.o sk_stats.o
	$(CPP) $(CFLAGS) $^ $(LIBS) $(SQLITE_LIB) -o $@

sk_viewer: sk_viewer.o
	$(CC) $(CFLAGS) $< $(LIBS) -o $@

cv_viewer: cv_viewer.o
	$(CC) $(CFLAGS) $< $(LIBS) -o $@

picort.o:
	$(CC) -c picort.c

stop_main: stop_main.o
	$(CC) $(CFLAGS) $< $(LIBS) -o $@

test_pico: test_pico.c picort.o
	$(CC) $(CFLAGS) -lm $(OPENCV_INC) $^ -O3 -DQCUTOFF=3.0 -DMINSIZE=60 -o $@ $(OPENCV_LIBS)

test_opencv: test_opencv.cpp
	$(CPP) $(CFLAGS) $(OPENCV_INC) $< $(LIBS) -o $@

test_logger: test_logger.o logger.o
	$(CC) $(CFLAGS) $^ -o $@

test_zrex: test_zrex.o
	$(CC) $(CFLAGS) $^ $(ZMQ_LIBS) -o $@

clean:
	rm -rf $(EXE) .tags* *.o *.dSYM ipc_video/ logs/ shop_keeper.db

.PHONY: clean all
