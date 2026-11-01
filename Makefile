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
# «угадай.бер» интерактивный, поэтому в общий прогон не входит.
EXAMPLES := $(filter-out examples/угадай.бер,$(wildcard examples/*.бер))

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

.PHONY: all install clean examples installer installer-windows
