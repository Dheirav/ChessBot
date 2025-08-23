# Compiler and flags
CXX = g++                             # The C++ compiler to use
CXXFLAGS = -std=c++17 -Wall -I./src   # Compiler flags: use C++17, show all warnings, add src to include path

# SFML libraries to link against
SFML_LIBS = -lsfml-graphics -lsfml-window -lsfml-system

# Source files (automatically find all .cpp files in src/, src/engine/, and src/gui/)
SRC = $(wildcard src/*.cpp src/engine/*.cpp src/gui/*.cpp)

# Object files generated from source files
OBJ = $(SRC:.cpp=.o)

# Name of the final executable
EXEC = chessbot

# Default target: build the executable
all: $(EXEC)

# Link object files to create the executable
$(EXEC): $(OBJ)
	$(CXX) $(OBJ) -o $@ $(SFML_LIBS)      

# Compile .cpp files to .o object files
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@        

# Clean up build files
clean:
	rm -f $(OBJ) $(EXEC)                  

# Clean and rebuild the project
remake:
	$(MAKE) clean
	$(MAKE) all

# Mark these targets as not actual files
.PHONY: all clean remake test_eval