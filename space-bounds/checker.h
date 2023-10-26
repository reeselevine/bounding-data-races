using namespace std;

void check_space(vector<uint32_t> results) {
  cout << "x=1, y=42 (seq): " << results[0] << "\n";
  cout << "x=42, y=42 (seq): " << results[1] << "\n";
  cout << "x=1, y=1 (not bounded): " << results[2] << "\n";
  cout << "x=42, y=1 (not bounded): " << results[3] << "\n";
  cout << "Other/error: " << results[4] << "\n\n";
}

void check_results(vector<uint32_t> results, string test_name) {
  if (test_name == "space") {
    check_space(results);
  }
}
