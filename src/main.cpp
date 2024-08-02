#include <iostream>
#include <fmt/format.h>

int main() {
  std::cout << "Test!\n";
  fmt::print("Fmt Test! {}\n", 2);

  return 0;
}
