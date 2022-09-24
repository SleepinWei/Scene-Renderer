#include<iostream>
#include<string>
using namespace std;

int main() {
  std::string s = "./a/b/c.png";
  int index = s.find_last_of("/");
  cout<<s.substr(index+1,s.length()-index-1-4)<<'\n';
}