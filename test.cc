#include <cstdint>

// Function declarations for testing
void takes_32bit_integer(int32_t i) { (void)i; }
void takes_32bit_float(float f) { (void)f; }

int32_t returns_32bit_from_64bit() {
    int64_t i64 = 1;
    return i64; // WARNING
}

float returns_32bit_float_from_64bit() {
    double d64 = 1.0;
    return d64; // WARNING
}

int32_t returns_32bit_from_64bit_2() {
    double d64 = 1.0;
    return d64; // WARNING
}

float returns_32bit_float_from_64bit_2() {
    int64_t i64 = 1;
    return i64; // WARNING
}

void test_implicit_conversion() {
    int64_t i64 = 1;
    int32_t i32;

    double d64 = 1.0;
    float f32 = d64;  // WARNING: Implicit narrowing conversion for float

    int32_t no_warn_i32 = 10;
    int64_t no_warn_i64 = no_warn_i32; // OK: Widening conversion
    i32 = i64;          // WARNING: Implicit narrowing conversion for integer
}

void test_explicit_cast() {
    int64_t i64 = 2;
    int32_t i32 = static_cast<int32_t>(i64);  // WARNING: Explicit static_cast
    int32_t i32_c_style = (int32_t)i64;      // WARNING: Explicit C-style cast
    
    double d64 = 2.0;
    float f32 = static_cast<float>(d64);  // WARNING: Explicit static_cast
    float f32_c_style = (float)d64;      // WARNING: Explicit C-style cast
}

void test_function_arguments() {
    int64_t i64 = 3;
    takes_32bit_integer(i64); // WARNING

    double d64 = 3.0;
    takes_32bit_float(d64); // WARNING

    takes_32bit_float(i64); // WARNING
    takes_32bit_integer(d64); // WARNING

    int32_t i32 = 3;
    takes_32bit_integer(i32); // OK
}

void test_function_returns() {
    int32_t i32_val = returns_32bit_from_64bit();
    float f32_val = returns_32bit_float_from_64bit();
    (void)i32_val;
    (void)f32_val;

    int32_t i32_val_2 = returns_32bit_from_64bit_2();
    float f32_val_2 = returns_32bit_float_from_64bit_2();
    (void)i32_val_2;
    (void)f32_val_2;

}

// New tests for lossy cross-type conversions
void test_int_to_float_conversion() {
    int64_t i64 = 9007199254740992LL; // Value that will lose precision in a float
    float f32 = i64; // WARNING
}

void test_float_to_int_conversion() {
    double d64 = 2147483648.5; // Value that is out of range for int32_t
    int32_t i32 = d64; // WARNING
}


int main() {
    test_implicit_conversion();
    test_explicit_cast();
    test_function_arguments();
    test_function_returns();
    test_int_to_float_conversion();
    test_float_to_int_conversion();
    return 0;
}

