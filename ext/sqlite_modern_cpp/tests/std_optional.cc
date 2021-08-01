#include <iostream>

#include <sqlite_modern_cpp.h>

using namespace sqlite;
using namespace std;

#ifdef MODERN_SQLITE_STD_OPTIONAL_SUPPORT
void insert(database& db, bool is_null) {
	int id = 1;
	std::optional<int> val;
	if(!is_null) val = 5;

	db << "delete from test where id = 1";
	db << "insert into test(id,val) values(?,?)" << id << val;
}

void select(database& db, bool should_be_null) {
	db << "select id,val from test" >> [&](long long, std::optional<int> val) {
		if(should_be_null) {
			if(val) exit(EXIT_FAILURE);
		} else {
			if(!val) exit(EXIT_FAILURE);
		}
	};
}

int main() {
	try {
		database db(":memory:");

		db << "drop table if exists test";
		db <<
			"create table if not exists test ("
			"   id integer primary key,"
			"   val int"
			");";

		insert(db, true);
		select(db, true);

		insert(db, false);
		select(db, false);

	} catch(exception& e) {
		cout << e.what() << endl;
		exit(EXIT_FAILURE);
	}
	exit(EXIT_SUCCESS);
}
#else
#pragma message "<optional> not found, test disabled."
int main() {
	exit(42);
}
#endif
