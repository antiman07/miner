#include<stdio.h>
#include<math.h>
struct B
{
 int b;
};

struct B* f1()
{
	struct B b;
	b.b = 11;
	printf("f1 add=%x\n",&b);
	return &b;
}

struct B f2()
{
	struct B b;
	printf("f2 add=%x\n",&b);
	b.b = 22;
	return b;
}
void test1()
{
	struct B* b = f1();
	printf("test1 add=%x\n",b);
	//printf("test1 b=%d\n",b->b);
	
}
void test2()
{
	struct B b = f2();
	printf("test2 add=%x\n",&b);
	printf("test2 b=%d\n",b.b);
	
}
int main()
{
	test1();
	test2();
}
