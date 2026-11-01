/* Differing debug-layout and bytes for real symbol and binary diagnostics. */
struct M153_Record {
    unsigned int count;
    unsigned int inserted_field;
    unsigned long code;
    char name[20];
};
struct M153_Record m153_record = {9, 42, 0x1235, "M153_D_MARKER"};
int m153_add(int value) { return value + m153_record.count; }
int main(void) { return m153_add(7) == 16 ? 0 : 1; }
