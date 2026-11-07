// Reads each turn's input and does nothing (diagnostic opponent).
#include <iostream>
#include <string>
int main() {
    int n; std::cin >> n;
    for (int i = 0; i < n; i++) { int a, b, c; std::cin >> a >> b >> c; }
    while (true) {
        int s1, s2; if (!(std::cin >> s1 >> s2)) return 0;
        for (int i = 0; i < n; i++) { int a, b, c; std::cin >> a >> b >> c; }
        int k; std::cin >> k; for (int i = 0; i < k; i++) { int a, b; std::cin >> a >> b; }
        std::cin >> k; for (int i = 0; i < k; i++) { int a, b, c, d, e, f; std::cin >> a >> b >> c >> d >> e >> f; }
        std::cin >> k; for (int i = 0; i < k; i++) { int a, b; std::cin >> a >> b; }
        std::cout << "WAIT" << std::endl;
    }
}
