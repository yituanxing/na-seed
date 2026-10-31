/* Fixture compiled into three ELF/DWARF objects ON THE LINUX HOST only. */
const char m118_unique_marker[] = "M118_SIGNED_ELFUTILS_BINARY_SENTINEL";

int m118_answer(int value)
{
    return value + 42 + (m118_unique_marker[0] - 'M');
}
