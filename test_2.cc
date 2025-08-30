#include<cstdint>

int64_t return_64bits() {
    int64_t x = 1;
    return x;
}

void take_32_bits_param(float x) {
    x += 1;
}

int main() {
    int32_t y = return_64bits();
    (void)y;

    take_32_bits_param(return_64bits());

    return 0;
}
