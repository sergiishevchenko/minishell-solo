NAME	= libft.a

CC		= cc
CFLAGS	= -Wall -Wextra -Werror
AR		= ar rcs

GREEN	= \033[1;32m
YELLOW	= \033[1;33m
CYAN	= \033[1;36m
BLUE	= \033[1;34m
RED		= \033[1;31m
GRAY	= \033[0;90m
RESET	= \033[0m

SRCS	= ft_atoi.c \
		  ft_ctype.c \
		  ft_itoa.c \
		  ft_lst.c \
		  ft_mem.c \
		  ft_put.c \
		  ft_split.c \
		  ft_str.c \
		  ft_strcmp.c

OBJS	= $(SRCS:.c=.o)

all: $(NAME)

$(NAME): $(OBJS)
	@$(AR) $(NAME) $(OBJS)
	@printf "$(BLUE)[libft]$(RESET) $(GREEN)$(NAME) built$(RESET)\n"

%.o: %.c libft.h
	@printf "$(BLUE)[libft]$(RESET) $(CYAN)compiling$(RESET) $(GRAY)%s$(RESET)\n" $<
	@$(CC) $(CFLAGS) -c $< -o $@

clean:
	@rm -f $(OBJS)
	@printf "$(BLUE)[libft]$(RESET) $(YELLOW)object files removed$(RESET)\n"

fclean: clean
	@rm -f $(NAME)
	@printf "$(BLUE)[libft]$(RESET) $(RED)$(NAME) removed$(RESET)\n"

re: fclean all

.PHONY: all clean fclean re
