CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++17 -O2 -I.

# Target executables
SERVER = server
CLIENT = client

# Source files
SERVER_SRCS = server.cpp \
              helpers/helper.cpp \
              hashtable/hashtable.cpp \
              zset/zset.cpp \
              AVLtrees/avl.cpp \
              heap/heap.cpp \
              threadPool/threadPool.cpp

CLIENT_SRCS = client.cpp

# Object files
SERVER_OBJS = $(SERVER_SRCS:.cpp=.o)
CLIENT_OBJS = $(CLIENT_SRCS:.cpp=.o)

all: $(SERVER) $(CLIENT)

$(SERVER): $(SERVER_OBJS)
	$(CXX) $(CXXFLAGS) -o $(SERVER) $(SERVER_OBJS)

$(CLIENT): $(CLIENT_OBJS)
	$(CXX) $(CXXFLAGS) -o $(CLIENT) $(CLIENT_OBJS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(SERVER) $(CLIENT) $(SERVER_OBJS) $(CLIENT_OBJS)

.PHONY: all clean
