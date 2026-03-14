#include <iostream>
#include <vector>
#include <algorithm>
#include <string>
#include <sqlite_modern_cpp.h>

using namespace sqlite;
using namespace std;

struct tbl_functor {
  explicit tbl_functor(vector<pair<int, string> > &vec_) : vec(vec_) { }

  void operator() ( int id, string name) {
    vec.push_back(make_pair(id, move(name)));
  }
  vector<pair<int,string> > &vec;
};

int main() {

  try {
    database db(":memory:");
    db << "CREATE TABLE tbl (id integer, name string);";
    db << "INSERT INTO tbl VALUES (?, ?);" << 1 << "hello";
    db << "INSERT INTO tbl VALUES (?, ?);" << 2 << "world";

    vector<pair<int,string> > vec;
    db << "select id,name from tbl;" >> tbl_functor(vec);

    if(vec.size() != 2) {
      cout << "Bad result on line " << __LINE__ << endl;
      exit(EXIT_FAILURE);
    }

    vec.clear();

    tbl_functor functor(vec);
    db << "select id,name from tbl;" >> functor;

    if(vec.size() != 2 || vec[0].first != 1 || vec[0].second != "hello") {
      cout << "Bad result on line " << __LINE__ << endl;
      exit(EXIT_FAILURE);
    }

  }
  catch(sqlite_exception e) {
    cout << "Unexpected error " << e.what() << endl;
    exit(EXIT_FAILURE);
  }
  catch(...) {
    cout << "Unknown error\n";
    exit(EXIT_FAILURE);
  }

  cout << "OK\n";
  exit(EXIT_SUCCESS);
}
