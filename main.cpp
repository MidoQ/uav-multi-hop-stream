#include <iostream>
#include "utils.h"
#include "dsr_route.h"

using namespace std;

int main(int, char**) {
    std::cout << "Hello, world!\n";

    unsigned long a = 0x1233f4ea0a21bb89;

    unsigned long b = hton64(a);

    unsigned long c = ntoh64(b);
    
    return 0;
}
