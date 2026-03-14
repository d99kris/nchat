#include <iostream>
#include <iomanip>
#include <string>
#include <memory>
#include <stdexcept>
#include <sqlite_modern_cpp.h>
using namespace sqlite;
using namespace std;


int main() {
  database db(":memory:");
  db << "CREATE TABLE person (id integer primary key not null, name TEXT);";
  bool expception_thrown = false;

  try {
    db << "INSERT INTO person (id,name) VALUES (?,?)" << 1 << "jack";
    // inserting again to produce error
    db << "INSERT INTO person (id,name) VALUES (?,?)" << 1 << "jack";
  } catch (errors::constraint_primarykey& e) {
    cerr << e.get_code() << '/' << e.get_extended_code() << ": " << e.what() << " during "
         << quoted(e.get_sql()) << endl;
    expception_thrown = true;
#if SQLITE_VERSION_NUMBER >= 3014000
    if(e.get_sql() != "INSERT INTO person (id,name) VALUES (1,'jack')") {
#else
    if(e.get_sql() != "INSERT INTO person (id,name) VALUES (?,?)") {
#endif
      cerr << "Wrong statement failed\n";
      exit(EXIT_FAILURE);
    }
  }

  if(!expception_thrown) {
    exit(EXIT_FAILURE);
  }
  
  exit(EXIT_SUCCESS);
}
