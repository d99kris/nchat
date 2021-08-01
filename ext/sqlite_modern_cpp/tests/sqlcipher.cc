#ifdef ENABLE_SQLCIPHER_TESTS
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <sqlite_modern_cpp/sqlcipher.h>
using namespace sqlite;
using namespace std;

struct TmpFile
{
	string fname;

	TmpFile(): fname(tmpnam(nullptr)) {}

	~TmpFile()
	{
		remove(fname.c_str());
	}
};

int main()
{
	try
	{
		TmpFile file;
		sqlcipher_config config;
		{
		  config.key = "DebugKey";
		  sqlcipher_database db(file.fname, config);

		  db << "CREATE TABLE foo (a integer, b string);";
		  db << "INSERT INTO foo VALUES (?, ?)" << 1 << "hello";
		  db << "INSERT INTO foo VALUES (?, ?)" << 2 << "world";

		  string str;
		  db << "SELECT b from FOO where a=?;" << 2 >> str;

		  if(str != "world")
		  {
			  cout << "Bad result on line " << __LINE__ << endl;
			  exit(EXIT_FAILURE);
		  }
		}
		try {
		  config.key = "DebugKey2";
		  sqlcipher_database db(file.fname, config);
		  db << "INSERT INTO foo VALUES (?, ?)" << 3 << "fail";

		  cout << "Can open with wrong key";
		  exit(EXIT_FAILURE);
		} catch(errors::notadb) {
		  // Expected, wrong key
		}
		{
		  config.key = "DebugKey";
		  sqlcipher_database db(file.fname, config);
		  db.rekey("DebugKey2");
		}
		{
		  config.key = "DebugKey2";
		  sqlcipher_database db(file.fname, config);
		  db << "INSERT INTO foo VALUES (?, ?)" << 3 << "fail";
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
}
#else
int main() {
  return 42; //Skip test
}
#endif
