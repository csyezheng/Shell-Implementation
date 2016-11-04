OBJS = shell.cpp builtins.cpp
NAME = myshell

all: $(NAME)

myshell: $(OBJS)
	g++ -std=c++11 $(OBJS) -l readline -o $(NAME)

clean:
	rm -rf $(NAME)
