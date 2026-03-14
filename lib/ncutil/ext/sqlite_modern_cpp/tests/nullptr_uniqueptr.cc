#include <iostream>
#include <string>
#include <vector>
#include <sqlite_modern_cpp.h>
using namespace std;
using namespace sqlite;

int main() {

  try {
    database db(":memory:");
    db << "CREATE TABLE tbl (id integer,age integer, name string, img blob);";
    db << "INSERT INTO tbl VALUES (?, ?, ?, ?);" << 1 << 24 << "bob" << vector<int> { 1, 2 , 3};
    unique_ptr<string> ptr_null;
    db << "INSERT INTO tbl VALUES (?, ?, ?, ?);" << 2 << nullptr << ptr_null << nullptr;

    db << "select age,name,img from tbl where id = 1" >> [](unique_ptr<int> age_p, unique_ptr<string> name_p, unique_ptr<vector<int>> img_p) {
      if(age_p == nullptr || name_p == nullptr || img_p == nullptr) {
        cerr << "ERROR: values should not be null" << std::endl;
        exit(EXIT_FAILURE);
      }

      cout << "age:" << *age_p << " name:" << *name_p << " img:";
      for(auto i : *img_p)
        cout << i << ",";
      cout << endl;
    };

    db << "select age,name,img from tbl where id = 2" >> [](unique_ptr<int> age_p, unique_ptr<string> name_p, unique_ptr<vector<int>> img_p) {
      if(age_p != nullptr || name_p != nullptr || img_p != nullptr) {
        cerr << "ERROR: values should be nullptr" << std::endl;
        exit(EXIT_FAILURE);
      }

      cout << "OK all three values are nullptr" << endl;
    };

  } catch(sqlite_exception e) {
    cout << "Sqlite error " << e.what() << endl;
    exit(EXIT_FAILURE);
  } catch(...) {
    cout << "Unknown error\n";
    exit(EXIT_FAILURE);
  }

  cout << "OK\n";
  exit(EXIT_SUCCESS);
  return 0;
}
