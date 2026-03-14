#include <iostream>
#include <cstdlib>
#include <sqlite_modern_cpp.h>
using namespace sqlite;
using namespace std;

int main()
{
#ifdef MODERN_SQLITE_STD_VARIANT_SUPPORT
	try
	{
		database db(":memory:");

		db << "CREATE TABLE foo (a);";
		std::variant<std::string, int, std::optional<float>> v;
		v = 1;
		db << "INSERT INTO foo VALUES (?)" << v;
		v = "a";
		db << "INSERT INTO foo VALUES (?)" << v;

		db << "SELECT a FROM foo WHERE a=?;" << 1 >> v;

		if(v.index() != 1 || std::get<1>(v) != 1)
		{
			cout << "Bad result on line " << __LINE__ << endl;
			exit(EXIT_FAILURE);
		}

		db << "SELECT NULL" >> v;
		if(std::get<2>(v)) {
			cout << "Bad result on line " << __LINE__ << endl;
			exit(EXIT_FAILURE);
		}

		db << "SELECT 0.0" >> v;
		if(!std::get<2>(v)) {
			cout << "Bad result on line " << __LINE__ << endl;
			exit(EXIT_FAILURE);
		}
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
#else
	exit(42);
#endif
}
