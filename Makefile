CXX      ?= c++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wno-unused-parameter
PREFIX   ?= /usr/local
BUILD    := build

SOURCES  := $(wildcard src/*.cpp)
HEADERS  := $(wildcard src/*.hpp)
OBJECTS  := $(patsubst src/%.cpp,$(BUILD)/%.o,$(SOURCES))
TARGET   := $(BUILD)/berestac

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(BUILD)/%.o: src/%.cpp $(HEADERS) | $(BUILD)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD):
	mkdir -p $(BUILD)

install: $(TARGET)
	install -Dm755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/berestac

# Прогоняет примеры. Это не тесты, это демонстрация.
# «угадай.бер» интерактивный, «сайт.бер» работает вечно — в общий прогон не входят.
EXAMPLES := $(filter-out examples/угадай.бер examples/сайт.бер examples/websocket.бер,$(wildcard examples/*.бер))

examples: $(TARGET)
	@for file in $(EXAMPLES); do \
		echo "== $$file"; \
		./$(TARGET) run "$$file" || exit 1; \
	done

clean:
	rm -rf $(BUILD)

installer:
	cd installer && go build -trimpath -o ../$(BUILD)/beresta-install .

installer-windows:
	cd installer && GOOS=windows GOARCH=amd64 go build -trimpath -o ../$(BUILD)/beresta-install.exe .

launcher:
	cd cli && go build -trimpath -o ../$(BUILD)/beresta .

launcher-windows:
	cd cli && GOOS=windows GOARCH=amd64 go build -trimpath -o ../$(BUILD)/beresta.exe .

# Нативный компилятор под Windows (кросс-сборка MinGW).
berestac-windows:
	$(CROSS) -std=c++20 -O2 -static -o $(BUILD)/berestac.exe src/*.cpp -lshell32

CROSS ?= x86_64-w64-mingw32-g++

# Лаунчер с вложенным компилятором: один файл, внутри berestac.
launcher-bundle: $(TARGET)
	cp $(TARGET) cli/berestac
	cd cli && go build -tags bundle -trimpath -o ../$(BUILD)/beresta-bundle .
	rm -f cli/berestac

.PHONY: all install clean examples installer installer-windows launcher launcher-windows launcher-bundle
