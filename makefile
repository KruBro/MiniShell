CC      = gcc
CFLAGS  = -Wall -Wextra -std=gnu11
SRCS    = main.c minishell.c job_control.c signals.c handle_builtin_commands.c handle_ext_commands.c
OBJS    = $(SRCS:.c=.o)
TARGET  = minishell

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

%.o: %.c minishell.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)