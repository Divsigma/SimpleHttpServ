#include <cstdio>
#include <cstdint>

void CheckByteOrder() {
  union {
      int16_t num;
      char num_char[2];
  } ele;

  ele.num = 0x0201;

  if (ele.num_char[0] == 0x01 && ele.num_char[1] == 0x02) {
      printf("little endian\n");
  } else if (ele.num_char[0] == 0x02 && ele.num_char[1] == 0x01) {
      printf("big endian\n");
  } else {
      printf("unknown endian ...\n");
  }

}

int main(int argc, const char *argv[]) {
  CheckByteOrder();
  return 0;
}

