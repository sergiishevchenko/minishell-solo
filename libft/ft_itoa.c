#include "libft.h"

char	*ft_itoa(int n)
{
	char	buf[12];
	long	nb;
	int		i;

	nb = n;
	if (nb < 0)
		nb = -nb;
	i = 11;
	buf[i] = '\0';
	while (1)
	{
		i--;
		buf[i] = '0' + nb % 10;
		nb /= 10;
		if (nb == 0)
			break ;
	}
	if (n < 0)
	{
		i--;
		buf[i] = '-';
	}
	return (ft_strdup(buf + i));
}
