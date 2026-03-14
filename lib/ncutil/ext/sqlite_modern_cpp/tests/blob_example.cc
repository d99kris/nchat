#include <iostream>
#include <cstdlib>
#include <vector>
#include <string>
#include <sqlite_modern_cpp.h>
using namespace  sqlite;
using namespace std;

int main()
{
	try
	{
		database db(":memory:");

		db << "CREATE TABLE person (name TEXT, numbers BLOB);";
		db << "INSERT INTO person VALUES (?, ?)" << "bob" << vector<int> { 1, 2, 3, 4};
		db << "INSERT INTO person VALUES (?, ?)" << "jack" << vector<char> { '1', '2', '3', '4'};
		db << "INSERT INTO person VALUES (?, ?)" << "sara" << vector<double> { 1.0, 2.0, 3.0, 4.0};

		vector<int> numbers_bob;
		db << "SELECT numbers from person where name = ?;" << "bob" >> numbers_bob;

		if(numbers_bob.size() != 4 || numbers_bob[0] != 1
        || numbers_bob[1] != 2 || numbers_bob[2] != 3 || numbers_bob[3] != 4 ) {
			cout << "Bad result on line " << __LINE__ << endl;
			exit(EXIT_FAILURE);
		}
    //else { for(auto e : numbers_bob) cout << e << ' '; cout << endl; }

		vector<char> numbers_jack;
		db << "SELECT numbers from person where name = ?;" << "jack" >> numbers_jack;

		if(numbers_jack.size() != 4 || numbers_jack[0] != '1'
        || numbers_jack[1] != '2' || numbers_jack[2] != '3' || numbers_jack[3] != '4' ) {
			cout << "Bad result on line " << __LINE__ << endl;
			exit(EXIT_FAILURE);
		}
    //else { for(auto e : numbers_jack) cout << e << ' '; cout << endl; }

		vector<double> numbers_sara;
		db << "SELECT numbers from person where name = ?;" << "sara" >> numbers_sara;

		if(numbers_sara.size() != 4 || numbers_sara[0] != 1
        || numbers_sara[1] != 2 || numbers_sara[2] != 3 || numbers_sara[3] != 4 ) {
			cout << "Bad result on line " << __LINE__ << endl;
			exit(EXIT_FAILURE);
		}
		//else {
			//db << "SELECT numbers from person where name = ?;" << "sara" >> [](vector<double> numbers_sara){
			    //for(auto e : numbers_sara) cout << e << ' '; cout << endl;
			//};
		//}

	}
	catch(sqlite_exception e)
	{
		cout << "Unexpected error " << e.what() << endl;
		exit(EXIT_FAILURE);
	}
	catch(...)
	{
		cout << "Unknown error\n";
		exit(EXIT_FAILURE);
	}

	cout << "OK\n";
	exit(EXIT_SUCCESS);
}
