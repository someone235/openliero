extern "C" __declspec(dllimport) int leaf();
extern "C" __declspec(dllexport) int bridge() { return leaf(); }
