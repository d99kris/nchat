#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <sqlite_modern_cpp.h>
#include <endian.h>
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

#if __BYTE_ORDER == __BIG_ENDIAN
#define OUR_UTF16 "UTF-16be"
#else
#define OUR_UTF16 "UTF-16le"
#endif

int main()
{
	try
	{
		TmpFile file;
		sqlite::sqlite_config cfg;
		std::string enc;
		{
			database db(":memory:", cfg);
			db << "PRAGMA encoding;" >> enc;
			if(enc != "UTF-8") {
				cout << "Unexpected encoding on line " << __LINE__ << '\n';
				exit(EXIT_FAILURE);
			}
		}
		{
			database db(u":memory:", cfg);
			db << "PRAGMA encoding;" >> enc;
			if(enc != OUR_UTF16) {
				cout << "Unexpected encoding on line " << __LINE__ << '\n';
				exit(EXIT_FAILURE);
			}
		}
		{
			cfg.encoding = Encoding::UTF8;
			database db(u":memory:", cfg);
			db << "PRAGMA encoding;" >> enc;
			if(enc != "UTF-8") {
				cout << "Unexpected encoding on line " << __LINE__ << '\n';
				exit(EXIT_FAILURE);
			}
		}
		{
			cfg.encoding = Encoding::UTF16;
			database db(u":memory:", cfg);
			db << "PRAGMA encoding;" >> enc;
			if(enc != OUR_UTF16) {
				cout << "Unexpected encoding on line " << __LINE__ << '\n';
				exit(EXIT_FAILURE);
			}
		}
		{
			database db(file.fname, cfg);
			db << "PRAGMA encoding;" >> enc;
			if(enc != OUR_UTF16) {
				cout << "Unexpected encoding on line " << __LINE__ << '\n';
				exit(EXIT_FAILURE);
			}

			db << "CREATE TABLE foo (a string);";
			db << "INSERT INTO foo VALUES (?)" << "hello";
		}
		{
			cfg.flags = sqlite::OpenFlags::READONLY;
			database db(file.fname, cfg);

			string str;
			db << "SELECT a FROM foo;" >> str;

			if(str != "hello")
			{
				cout << "Bad result on line " << __LINE__ << endl;
				exit(EXIT_FAILURE);
			}

			try {
				db << "INSERT INTO foo VALUES (?)" << "invalid";
				cout << "Unexpected success on line " << __LINE__ << endl;
				exit(EXIT_FAILURE);
			} catch(errors::readonly&) {}
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
