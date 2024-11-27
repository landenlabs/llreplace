CXX = clang++
CXXFLAGS = -std=c++11

# define the C source files
SRCS = llreplace.cpp directory.cpp

OBJS = $(SRCS:.c=.o)

# define the executable file 
MAIN = llreplace

all: $(MAIN)
      
      
$(MAIN): $(OBJS) 
	$(CXX) $(CXXFLAGS) -o $(MAIN) $(OBJS) # $(LFLAGS) $(LIBS)
  

clean:
	rm -rf *.o* $(MAIN)


#depend: $(SRCS)
#    makedepend $(INCLUDES) $^

# DO NOT DELETE THIS LINE -- make depend needs it
