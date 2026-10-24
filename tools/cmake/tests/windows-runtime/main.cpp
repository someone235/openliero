extern "C" __declspec(dllimport) int bridge();
int main() { return bridge() == 42 ? 0 : 1; }
