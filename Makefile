ENGINE_SRC = src/*.go
ENGINE_TARGET = bin/wallpaper-engine

build:
	mkdir -p bin
	go build -o $(ENGINE_TARGET) $(ENGINE_SRC)

dev: clean build
	$(ENGINE_TARGET)

clean:
	rm -rf bin tmp debug.log test_out

test: build
	@echo "Testing DXT1 Decoder..."
	@# Find the first .tex file in tmp/materials/ to test
	@FILE=$$(find tmp/materials -name "*.tex" | head -n 1); \
	if [ -z "$$FILE" ]; then \
		echo "No .tex files found in tmp/materials to test. Please ensure tmp directory exists."; \
		exit 1; \
	fi; \
	$(ENGINE_TARGET) decode $$FILE