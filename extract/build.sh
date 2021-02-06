clang++ --std=c++17 -O0 -g -fno-exceptions -Wall -Wextra -Werror \
    -Wno-unused-parameter \
    -Wno-unused-const-variable \
    -Wno-unused-variable \
    -Wno-sign-compare \
    -Wno-c++11-narrowing \
  voltool.cpp -o voltool
    # -fsanitize=address \
