CXX      ?= g++
CXXSTD   ?= -std=c++23
CXXFLAGS ?= $(CXXSTD) -Wall -Wextra -Wpedantic -O2 -Iinclude -Ithird_party
LDFLAGS  ?=
# FFmpeg: prefer pkg-config when available; fall back to bare libs + include path.
FFMPEG_CFLAGS := $(shell pkg-config --cflags libavformat libavcodec libavutil libswscale 2>/dev/null)
FFMPEG_LIBS   := $(shell pkg-config --libs libavformat libavcodec libavutil libswscale 2>/dev/null)
ifeq ($(FFMPEG_LIBS),)
  FFMPEG_CFLAGS := -I/usr/include/ffmpeg
  FFMPEG_LIBS   := -lavformat -lavcodec -lavutil -lswscale
endif
CXXFLAGS += $(FFMPEG_CFLAGS)
LDFLAGS  += $(FFMPEG_LIBS) -lm -pthread

SRC = \
	src/ascii_mapper.cpp \
	src/image_source.cpp \
	src/video_source.cpp \
	src/presenter.cpp \
	src/tone_map.cpp \
	src/engine.cpp \
	src/main.cpp

OBJ = $(SRC:.cpp=.o)

.PHONY: all debug clean run-image run-video test

all: zola

zola: $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

src/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

debug: clean
	$(MAKE) CXXFLAGS="$(CXXSTD) -Wall -Wextra -Wpedantic -O0 -g -DZOLA_DEBUG -Iinclude -Ithird_party $(FFMPEG_CFLAGS)" all

clean:
	rm -f $(OBJ) zola tone_map_test color_test tests/tone_map_test.o tests/color_test.o

run-image: zola
	./zola image $(IMG)

run-video: zola
	./zola play $(VID)

# Pure-logic tests (no TTY / FFmpeg decode required).
test: tone_map_test color_test
	./tone_map_test
	./color_test

tone_map_test: tests/tone_map_test.o src/tone_map.o
	$(CXX) $(CXXFLAGS) -o $@ $^ -lm

color_test: tests/color_test.o src/ascii_mapper.o src/presenter.o
	$(CXX) $(CXXFLAGS) -o $@ $^ -lm

tests/%.o: tests/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<
