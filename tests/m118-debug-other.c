/* A genuinely different host-built object for eu-elfcmp negative controls. */
const char m118_unique_marker_alt[] = "M118_DISTINCT_DIFFERENTIAL_OBJECT";

int m118_answer(int value)
{
    return value + 43 + (m118_unique_marker_alt[0] - 'M');
}
