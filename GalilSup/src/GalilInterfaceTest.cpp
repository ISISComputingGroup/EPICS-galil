#include "IGalil.h"

int main(int argc, char* argv[])
{
	Galil* g = new Galil("127.0.0.1");
	delete g;
	return 0;
}