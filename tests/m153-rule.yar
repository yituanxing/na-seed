rule M153_Compiled {
  strings: $marker = "M153_C_MARKER"
  condition: $marker
}
