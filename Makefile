# compiler names:
STRIP   := $(CROSS_COMPILE)strip
CC	:= $(CROSS_COMPILE)gcc
CPP	:= $(CROSS_COMPILE)g++
CXX	:= $(CPP)
LD	:= $(CROSS_COMPILE)g++
AR      := $(CROSS_COMPILE)ar
RANLIB  := $(CROSS_COMPILE)gcc-ranlib
NM      := $(CROSS_COMPILE)nm

LOCAL_MODULE := libaplayer.a
LOCAL_SRC_FILES := aplayer.cpp \
		   wav_file.cpp \
		   pcm_utils.c
		   
LOCAL_OBJ_FILES := $(patsubst %.cpp,%.o,$(LOCAL_SRC_FILES))
LOCAL_OBJ_FILES := $(patsubst %.c,%.o,$(LOCAL_OBJ_FILES))

LOCAL_C_INCLUDES:= 

#-Werror
LOCAL_CPPFLAGS := -pthread -Wall -fPIC -g -O0
LOCAL_LDFLAGS := -pthread -lasound

TEST_MODULE := aplayer
TEST_SRC_FILES := main.cpp
TEST_OBJ_FILES := $(patsubst %.cpp,%.o,$(TEST_SRC_FILES))

.PHONY: clean test

$(LOCAL_MODULE): $(LOCAL_OBJ_FILES)
	$(AR) -rcso $(LOCAL_MODULE) $^

test: $(TEST_OBJ_FILES) $(LOCAL_MODULE)
	$(CXX) -o $(TEST_MODULE) $^ $(LOCAL_LDFLAGS)

%.o : %.cpp
	$(CXX) -c $(CPPFLAGS) $(LOCAL_CPPFLAGS) $(LOCAL_C_INCLUDES) $< -o $@

%.o : %.c
	$(CC) -c $(CFLAGS) $(LOCAL_CPPFLAGS) $(LOCAL_C_INCLUDES) $< -o $@

clean:
	@rm $(LOCAL_OBJ_FILES) $(TEST_OBJ_FILES)
	@rm $(LOCAL_MODULE) $(TEST_MODULE)

