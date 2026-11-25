#include "minishell.h"

t_node	*parse_command(t_parser *p)
{
	return (parse_simple(p));
}

t_node	*parse_line(t_parser *p)
{
	return (parse_pipeline(p));
}
