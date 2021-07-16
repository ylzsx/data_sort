#include <fstream>
#include <cstdlib>

#define TEST_SIZE 10000000

int main() {
  std::fstream src_data;
  src_data.open("source_data.dat", std::fstream::out);
  for(int i=0; i<TEST_SIZE; i++)
    src_data << rand() << " " << i << "M" << rand() << std::endl;
  src_data.close();
  return 0;
}
