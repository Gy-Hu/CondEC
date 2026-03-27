SRCS = $(shell find . -maxdepth 1 -name "*.c*") 

OBJS = $(addsuffix .o, $(basename $(SRCS)))

EXEC = condec

LIBS = -g -lcadical -Lcadical/build
LIBS = -lcadical -Lcadical/build

CXXFLAGS = -g -Icadical -mavx2 -std=c++17 -march=native
# CXXFLAGS = -g -Ofast -Icadical -mavx2 -std=c++17 -march=native

$(EXEC): $(OBJS)
	$(CXX) -o $@ $^ $(CXXFLAGS) $(LIBS)

%.o: %.cpp
	$(CXX) -c $< -o $@ $(CXXFLAGS) $(LIBS)

clean:
	rm -f $(OBJS) $(EXEC)
	

