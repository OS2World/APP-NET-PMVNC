using namespace std;
#include <iostream>
#include <stddef.h>
#include "omnithread.h"

omni_condition *c;

class test : public omni_thread
{
	private:
		void run(void *);
};

void test::run(void *)
{
		DosEnterCritSec();
		cout << '[' << id() << ']' << " waiting...\n";
		DosExitCritSec();
	/*for ( int i = 0 ; i < 3 ; i++ )
	{
		DosEnterCritSec();
		cout << '[' << id() << ']' << " run!\n";
		DosExitCritSec();
		sleep( 1 );
	}*/
	c->wait();	
		DosEnterCritSec();
		cout << '[' << id() << ']' << " exiting...\n";
		DosExitCritSec();
}

int main()
{
	omni_mutex m;
	c = new omni_condition(&m);
	
	omni_thread::stacksize( 32384 );
	
	try {
	
	test *q1 = new test;
	test *q2 = new test;
	test *q3 = new test;
	q1->start();
	//q1->set_priority( omni_thread::PRIORITY_LOW );
	q2->start();
	q3->start();
	omni_thread::self()->sleep( 1 );
	c->broadcast();
	
	omni_thread::self()->sleep( 10 );
	}
	catch ( ... ) { cout << "ˆªá¥¯è­!\n"; }
	return 0;
}

