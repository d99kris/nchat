#include <iostream>
#include <cstdlib>
#include <sqlite_modern_cpp.h>
using namespace  sqlite;
using namespace std;

int main() {
	try {

		database db(":memory:");

		auto pps = db << "select ?"; // get a prepared parsed and ready statment

		int test = 4;
		pps << test; // set a bound var

		pps >> test; // execute statement

		pps.reset();

		pps << 4; // bind a rvalue
		pps++; // and execute and reset

		pps << 8 >> test;

		auto pps2 = db << "select 1,2,3,4,5"; // multiple extract test

		pps2 >> [](int a, int b, int c, int d, int e) {
			std::cout << "L " << a << b << c << d << e << "\n"; // still works as intended
		};

		auto pps3 = db << "select ?,?,?";

		pps3 << 1 << test << 5 >> [](int a, int b, int, int c) {
			std::cout << "L2 " << a << b << c << "\n"; // still works as intended
		};

		db << "select ?,?" << test << 5 >> test; // and mow everything together

		db << "select ?, ?, ?" << 1 << test << 1 >> [](int a, int b, int, int c) {
			std::cout << "L3 " << a << b << c << "\n"; // still works as intended
		};

		db << "select ?" << test; 		// noVal		
		db << "select ?,?" << test << 1;
		db << "select ?,?" << 1 << test;
		db << "select ?,?" << 1 << 1;
		db << "select ?,?" << test << test;

		db << "select ?" << test >> test; 		// lVal		
		db << "select ?,?" << test << 1 >> test;
		db << "select ?,?" << 1 << test >> test;
		db << "select ?,?" << 1 << 1 >> test;
		db << "select ?,?" << test << test >> test;

		int q = 0;

		db << "select ?" << test >> [&](int t) { q = t++; }; 		// rVal		
		db << "select ?,?" << test << 1 >> [&](int t, int p) { q = t + p; };
		db << "select ?,?" << 1 << test >> [&](int t, int p) { q = t + p; };
		db << "select ?,?" << 1 << 1 >> [&](int t, int p) { q = t + p; };
		db << "select ?,?" << test << test >> [&](int t, int p) { q = t + p; };

		db << "select ?,?,?" << test << 1 << test; // mix
		db << "select ?,?,?" << 1 << test << 1;
		db << "select ?,?,?" << 1 << 1 << test;
		db << "select ?,?,?" << 1 << 1 << 1;
		db << "select ?,?,?" << test << test << test;

		{
			auto pps4 = db << "select ?,?,?"; // reuse

			(pps4 << test << 1 << test)++;
			(pps4 << 1 << test << 1)++;
			(pps4 << 1 << 1 << test)++;
			(pps4 << 1 << 1 << 1)++;
			(pps4 << test << test << test)++;
		}

		{
			auto prep = db << "select ?";

			prep << 5;

			prep.execute();
			try {
				prep.execute();
				exit(EXIT_FAILURE);
			} catch(errors::reexecution& ex) {
				// Thats ok here
			} catch(...) {
				exit(EXIT_FAILURE);
			}

			prep.reset();

			prep << 6;
			prep.execute();

		}


	} catch(sqlite_exception e) {
		cout << "Unexpected error " << e.what() << endl;
		exit(EXIT_FAILURE);
	} catch(...) {
		cout << "Unknown error\n";
		exit(EXIT_FAILURE);
	}

	cout << "OK\n";
	exit(EXIT_SUCCESS);
}
