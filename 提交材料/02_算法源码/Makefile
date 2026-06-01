CXX = g++
CXXFLAGS = -O3 -std=c++17 -Wall -march=native

TARGET = approx_cor
SRC = approx_cor.cpp

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)

run: $(TARGET)
	./$(TARGET)

.PHONY: all clean run
