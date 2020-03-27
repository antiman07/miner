#include<stdio.h>
int* f1()
{
	int n = 100;
	return &n;
}
int main()
{
	int* p = f1();
	printf("add=%x\n",p);
	//printf("value=%d\n",*p);
}
