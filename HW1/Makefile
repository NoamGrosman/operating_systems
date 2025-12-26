CC   = gcc
CXX  = g++
CFLAGS   = -std=c99   -Wall -Werror -pedantic-errors -pthread -DNDEBUG
CXXFLAGS = -std=c++11 -Wall -Werror -pedantic-errors -pthread -DNDEBUG

C_SRCS   = $(wildcard *.c)
CPP_SRCS = $(wildcard *.cpp)

C_OBJS   = $(C_SRCS:.c=.o)
CPP_OBJS = $(CPP_SRCS:.cpp=.o)
OBJS     = $(C_OBJS) $(CPP_OBJS) my_system_call.o

TARGET = smash

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(TARGET)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(TARGET) $(C_OBJS) $(CPP_OBJS)