#include "libft.h"

int	ft_atoi(const char *str)
{
	long	result;
	long	int_max;
	long	int_min;
	int		sign;

	result = 0;
	sign = 1;
	int_max = 2147483647L;
	int_min = -int_max - 1;
	while (*str == ' ' || (*str >= '\t' && *str <= '\r'))
		str++;
	if (*str == '-' || *str == '+')
	{
		if (*str == '-')
			sign = -1;
		str++;
	}
	while (ft_isdigit(*str) && result <= int_max + 1)
	{
		result = result * 10 + (*str - '0');
		str++;
	}
	if (result * sign > int_max)
		return ((int)int_max);
	if (result * sign < int_min)
		return ((int)int_min);
	return ((int)(result * sign));
}
