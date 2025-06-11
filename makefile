SRCS = $(shell find . -maxdepth 1 -name "*.c*") 

OBJS = $(addsuffix .o, $(basename $(SRCS)))

EXEC = condec_test

LIBS = -g -lkissat -Lkissat_extras/build
LIBS = -lkissat -Lkissat_extras/build

CXXFLAGS = -g -IhKis -mavx2 -std=c++17 -march=native
# CXXFLAGS = -g -Ofast -IhKis -mavx2 -std=c++17 -march=native

$(EXEC): $(OBJS)
	$(CXX) -o $@ $^ $(CXXFLAGS) $(LIBS)

%.o: %.cpp
	$(CXX) -c $< -o $@ $(CXXFLAGS) $(LIBS)

clean:
	rm -f $(OBJS) $(EXEC)
	

