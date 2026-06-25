# =============================================================================
# Makefile — 方式2逼近算法构建
# =============================================================================
# 赛题三: 矩阵连乘元素的逼近
# 第十一届(2026)全国高校密码数学挑战赛
# =============================================================================

CXX = g++
CXXFLAGS = -O3 -std=c++17 -Wall -Wextra -march=native

TARGET = approx_cor
SRC = src/main.cpp
HEADERS = src/hs_cipher.hpp src/approx_engine.hpp

# Default beam width (can be overridden)
BEAM ?= 200000

.PHONY: all clean run test bench info

all: $(TARGET)

$(TARGET): $(SRC) $(HEADERS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC) -I src

clean:
	rm -f $(TARGET)
	rm -rf results/*.txt

info: $(TARGET)
	./$(TARGET) info

# Quick smoke test
test: $(TARGET)
	@echo "=== Smoke Tests ==="
	@echo ""
	@echo "Test 1: r=1 single-active exact"
	./$(TARGET) exact 0x20000000 0x00000888 1
	@echo ""
	@echo "Test 2: r=2 correlation=1.0"
	./$(TARGET) exact 0x20000000 0x00000888 2
	@echo ""
	@echo "Test 3: r=2 correlation=0.75"
	./$(TARGET) exact 0x70000000 0x00000DDD 2
	@echo ""
	@echo "Test 4: r=3 correlation=0.125"
	./$(TARGET) exact 0x20000000 0xEEE00E0E 3
	@echo ""
	@echo "Test 5: Way-2 estimate (should match exact for simple case)"
	./$(TARGET) estimate 0x20000000 0x00000888 2
	@echo ""
	@echo "Test 6: Verify with Monte Carlo"
	./$(TARGET) verify 0x20000000 0x00000888 2 --mc 100000

# Run batch for all single-active inputs with r=1..3
run-r3: $(TARGET)
	mkdir -p results
	./$(TARGET) batch-all 3 --beam $(BEAM) --output results

# Run batch for all single-active inputs with r=1..5
run-r5: $(TARGET)
	mkdir -p results
	./$(TARGET) batch-all 5 --beam $(BEAM) --output results

# Run batch for a specific position
run-pos%: $(TARGET)
	mkdir -p results
	./$(TARGET) batch-position 3 $* --beam $(BEAM) --output results

# Run batch for a single input with r=1..4
run-r4: $(TARGET)
	mkdir -p results
	./$(TARGET) batch-all 4 --beam $(BEAM) --output results

# Score the results
score:
	python3 ../scripts/compute_scores.py results/valid_estimates.txt results/scores.txt
