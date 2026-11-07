/* HOST compiled for each target ISA; never compiled inside NaSeed. */
struct M153_Record {
    unsigned int count;
    unsigned long code;
    char name[20];
};
struct M153_Record m153_record = {7, 0x1234, "M153_C_MARKER"};
int m153_add(int value) { return value + m153_record.count; }
int main(void) { return m153_add(7) == 14 ? 0 : 1; }
