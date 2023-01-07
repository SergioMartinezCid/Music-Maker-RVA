ARTOOLKITDIR = ../ARToolKit
INC_DIR      = $(ARTOOLKITDIR)/include
LIB_DIR      = $(ARTOOLKITDIR)/lib

LIBS   = -lARgsub -lARvideo -lARMulti -lAR -lglut -lGLU -lGL -lm -lao -ldl -lsndfile

NAMEEXEC     = musicmaker

all: $(NAMEEXEC)

$(NAMEEXEC): $(NAMEEXEC).c 
	cc -I $(INC_DIR) -o $(NAMEEXEC) $(NAMEEXEC).c -L$(LIB_DIR) $(LIBS) 
clean:
	rm -f *.o $(NAMEEXEC) *~ *.*~

