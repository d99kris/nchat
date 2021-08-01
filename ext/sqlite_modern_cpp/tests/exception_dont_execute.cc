#include <iostream>
#include <string>
#include <memory>
#include <stdexcept>
#include <sqlite_modern_cpp.h>
using namespace sqlite;
using namespace std;


int main() {
	database db(":memory:");
	db << "CREATE TABLE person (id integer primary key not null, name TEXT not null);";

	try {
		auto stmt = db << "INSERT INTO person (id,name) VALUES (?,?)";
		throw 1;
	} catch (int) {
	}
	exit(EXIT_SUCCESS);
}
