ENGINE_SRC = src/*.go
ENGINE_TARGET = bin/linux-wallpaperengine

build:
	mkdir -p bin
	go build -o $(ENGINE_TARGET) $(ENGINE_SRC)

dev: clean build
	$(ENGINE_TARGET) $(ARGS)

run:
	$(ENGINE_TARGET) $(ARGS)

clean:
	rm -rf bin tmp debug.log test_out converted

test-texture: build
	@echo "Testing DXT1 Decoder..."
	@FILE=$$(find test/* -name "*.tex" | head -n 1); \
	if [ -z "$$FILE" ]; then \
		echo "No .tex files found in test directory."; \
		exit 1; \
	fi; \
	echo "Decoding file: $$FILE"; \
	$(ENGINE_TARGET) decode $$FILE $(ARGS)

test-sine: build
	@echo "Testing Sine Wave generator (Forcing Pulse/PipeWire)..."
	OTO_LINUX_BACKEND=pulse $(ENGINE_TARGET) -test-sine