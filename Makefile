CXX = g++
CXXFLAGS = -O2 -Wall
LDFLAGS = -lboost_filesystem

UNPACKER_SRC = src/lib/unpacker.cpp
UNPACKER_TARGET = bin/unpacker

ENGINE_SRC = src/main.go
ENGINE_TARGET = bin/wallpaper-engine

all: $(UNPACKER_TARGET) $(ENGINE_TARGET)

$(UNPACKER_TARGET): $(UNPACKER_SRC)
	mkdir -p bin
	$(CXX) $(CXXFLAGS) $(UNPACKER_SRC) -o $(UNPACKER_TARGET) $(LDFLAGS)

$(ENGINE_TARGET): $(ENGINE_SRC)
	mkdir -p bin
	go build -o $(ENGINE_TARGET) ./src

run: all
	./$(ENGINE_TARGET)

clean:
	rm -rf bin tmp