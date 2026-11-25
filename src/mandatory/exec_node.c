#include "minishell.h"

int	exec_node(t_shell *sh, t_node *node)
{
	if (node->type == N_PIPE)
		return (exec_pipeline(sh, node));
	return (exec_command(sh, node));
}
